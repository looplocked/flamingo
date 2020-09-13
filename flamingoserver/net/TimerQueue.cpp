#include "TimerQueue.h"

#include <functional>

#include "../base/Platform.h"
#include "../base/AsyncLog.h"
#include "EventLoop.h"
#include "Timer.h"
#include "TimerId.h"

namespace net
{
}

using namespace net;
//using namespace net::detail;

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
    /*timerfd_(createTimerfd()),
    timerfdChannel_(loop, timerfd_),*/
    timers_()
    //callingExpiredTimers_(false)
{

}

TimerQueue::~TimerQueue()
{
    for (TimerList::iterator it = timers_.begin(); it != timers_.end(); ++it)
    {
        delete it->second;
    }
}

TimerId TimerQueue::addTimer(const TimerCallback& cb, Timestamp when, int64_t interval, int64_t repeatCount)
{
    Timer* timer = new Timer(cb, when, interval);
    loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());
}

TimerId TimerQueue::addTimer(TimerCallback&& cb, Timestamp when, int64_t interval, int64_t repeatCount)
{
    Timer* timer = new Timer(std::move(cb), when, interval, repeatCount);
    loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());
}

void TimerQueue::removeTimer(TimerId timerId)
{
    loop_->runInLoop(std::bind(&TimerQueue::removeTimerInLoop, this, timerId));
}

void TimerQueue::cancel(TimerId timerId, bool off)
{
    loop_->runInLoop(std::bind(&TimerQueue::cancelTimerInLoop, this, timerId, off));
}

void TimerQueue::doTimer()
{
    loop_->assertInLoopThread();
    
    Timestamp now(Timestamp::now());

    for (auto iter = timers_.begin(); iter != timers_.end(); )
    {
        //if (iter->first <= now)
        if (iter->second->expiration() <= now)
        {
            //LOGD("time: %lld", iter->second->expiration().microSecondsSinceEpoch());
            iter->second->run();
            if (iter->second->getRepeatCount() == 0)
            {
                iter = timers_.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
        else
        {
            break;
        }           
    }

}

void TimerQueue::addTimerInLoop(Timer* timer)
{
    loop_->assertInLoopThread();
}

void TimerQueue::removeTimerInLoop(TimerId timerId)
{
    loop_->assertInLoopThread();
    //assert(timers_.size() == activeTimers_.size());
    //ActiveTimer timer(timerId.timer_, timerId.sequence_);
    //ActiveTimerSet::iterator it = activeTimers_.find(timer);

    Timer* timer = timerId.timer_;
    for (auto iter = timers_.begin(); iter != timers_.end(); ++iter)
    {
        if (iter->second == timer)
        {
            timers_.erase(iter);
            break;
        }
    }  
}

void TimerQueue::cancelTimerInLoop(TimerId timerId, bool off)
{
    loop_->assertInLoopThread();

    Timer* timer = timerId.timer_;
    for (auto iter = timers_.begin(); iter != timers_.end(); ++iter)
    {
        if (iter->second == timer)
        {
            iter->second->cancel(off);
            break;
        }
    }
}

void TimerQueue::insert(Timer* timer)
{
    loop_->assertInLoopThread();
    //assert(timers_.size() == activeTimers_.size());
    bool earliestChanged = false;
    Timestamp when = timer->expiration();
    //TimerList::iterator it = timers_.begin();
    //if (it == timers_.end() || when < it->first)
    //{
    //    earliestChanged = true;
    //}
    //{
        /*std::pair<TimerList::iterator, bool> result = */timers_.insert(Entry(when, timer));
}