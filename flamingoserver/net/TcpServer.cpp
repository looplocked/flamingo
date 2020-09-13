#include "TcpServer.h"

#include <stdio.h>  // snprintf
#include <functional>

#include "../base/Platform.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "Sockets.h"

using namespace net;

TcpServer::TcpServer(EventLoop* loop,
    const InetAddress& listenAddr,
    const std::string& nameArg,
    Option option)
    : loop_(loop),
    hostport_(listenAddr.toIpPort()),
    name_(nameArg),
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
    //threadPool_(new EventLoopThreadPool(loop, name_)),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    started_(0),
    nextConnId_(1)
{
    LOGD("set acceptor::connectioncallback as TcpServer::newConnection");
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    loop_->assertInLoopThread();
    LOGD("TcpServer::~TcpServer [%s] destructing", name_.c_str());

    stop();
}

//void TcpServer::setThreadNum(int numThreads)
//{
//  assert(0 <= numThreads);
//  threadPool_->setThreadNum(numThreads);
//}

void TcpServer::start(int workerThreadCount/* = 4*/)
{
    LOGD("TcpServer::start!!, Thread count is %d", workerThreadCount);
    if (started_ == 0)
    {
        eventLoopThreadPool_.reset(new EventLoopThreadPool());
        eventLoopThreadPool_->init(loop_, workerThreadCount);
        eventLoopThreadPool_->start();
        
        //threadPool_->start(threadInitCallback_);
        //assert(!acceptor_->listenning());
        LOGD("Acceptor::listen run in loop");
        // TcpServer不曾拥有过Acceptor，却能调用Acceptor的成员函数，值得注意！
        // listen中会调用enablereading,从而将acceptor::channel添加进mainloop的poll中
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
        started_ = 1;
    }
}

void TcpServer::stop()
{
    if (started_ == 0)
        return;

    for (ConnectionMap::iterator it = connections_.begin(); it != connections_.end(); ++it)
    {
        TcpConnectionPtr conn = it->second;
        it->second.reset();
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
        conn.reset();
    }

    eventLoopThreadPool_->stop();

    started_ = 0;
}

void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
    LOGD("TcpServer::newConnection in!!!");
    loop_->assertInLoopThread();
    EventLoop* ioLoop = eventLoopThreadPool_->getNextLoop();
    char buf[32];
    snprintf(buf, sizeof buf, ":%s#%d", hostport_.c_str(), nextConnId_);
    ++nextConnId_;
    string connName = name_ + buf;

    LOGD("TcpServer::newConnection [%s] - new connection [%s] from %s", name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    InetAddress localAddr(sockets::getLocalAddr(sockfd));
    // FIXME poll with zero timeout to double confirm the new connection
    // FIXME use make_shared if necessary
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
    connections_[connName] = conn;
    LOGD("new connection set callback start!");
    // 这里指向ChatServer::OnConnection，有实际内容
    conn->setConnectionCallback(connectionCallback_);
    // 这里只是传入一个空的函数指针，真正的函数内容在ChatServer::OnConnection被调用时传入
    conn->setMessageCallback(messageCallback_);
    // 此处似乎无用，反正后面调用connectionCallback也会指向OnRead
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)); // FIXME: unsafe
    //该线程分离完io事件后，立即调用TcpConnection::connectEstablished
    LOGD("TcpConnection::connectEstablished run in ioloop, ioloop is 0x%x", ioLoop);
    // 此处这个runInLoop是在主线程中运行，只有在判断到主线程号与loop线程号对不上，才丢进另一个线程处理
    // connectEstablished会调用connectionCallback，进而调用onConnection，将conn的messageCallback指向实际的XXSession::onRead
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
    // FIXME: unsafe
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
    loop_->assertInLoopThread();
    LOGD("TcpServer::removeConnectionInLoop [%s] - connection %s", name_.c_str(), conn->name().c_str());
    size_t n = connections_.erase(conn->name());
    //(void)n;
    //assert(n == 1);
    if (n != 1)
    {
        //出现这种情况，是TcpConneaction对象在创建过程中，对方就断开连接了。
        LOGD("TcpServer::removeConnectionInLoop [%s] - connection %s, connection does not exist.", name_.c_str(), conn->name().c_str());
        return;
    }

    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}

