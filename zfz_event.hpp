/*********************************************************** 
* Date: 2016-07-19 
* 
* Author: nick 
* 
* Email: nick1115@163.com 
* 
* Module: 事件 
* 
* Brief: 模拟windows的event,但不具备跨进程能力 
* 
* Note: 
* 
* CodePage: UTF-8 
************************************************************/ 
#ifndef __ZFZ_EVENT_HPP_BY_NICK_2016_07_19__
#define __ZFZ_EVENT_HPP_BY_NICK_2016_07_19__

#include <chrono>
#include <mutex>
#include <condition_variable>

namespace zfz
{

enum 
{
    ZFZ_EVENT_FAIL = (-1),
    ZFZ_EVENT_SUCCESS = 0,
    ZFZ_EVENT_TIME_OUT = 1
};

class Event
{
public:
    Event(bool init_signal = false, bool manual_reset = true) : 
        signal_(init_signal), 
        manual_reset_(manual_reset), 
        blocked_(0) 
    {
    }

    ~Event() {}

private:
    Event(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(const Event&) = delete;

public:
    int wait(const int time_out_ms = (-1))
    {
        std::unique_lock<std::mutex> ul(lock_);

        if (signal_)
        {
            if (!manual_reset_)
            {
                signal_ = false;
            }

            return ZFZ_EVENT_SUCCESS;
        }
        else
        {
            if (time_out_ms == 0)
            {
                return ZFZ_EVENT_TIME_OUT;
            }
            else
            {
                ++blocked_;
            }
        }

        if (time_out_ms >= 0)
        {
            std::chrono::milliseconds wait_time_ms(time_out_ms);
            auto result = cv_.wait_for(ul, wait_time_ms, [&]{ return signal_; });
            --blocked_;
            if (result)
            {
                if (!manual_reset_)
                {
                    signal_ = false;
                }
                return ZFZ_EVENT_SUCCESS;
            }
            else
            {
                return ZFZ_EVENT_TIME_OUT;
            }
        }
        else
        {
            cv_.wait(ul, [&]{return signal_;});
            --blocked_;
            if (!manual_reset_)
            {
                signal_ = false;
            }
            return ZFZ_EVENT_SUCCESS;
        }
    }

    void set()
    {
        std::lock_guard<std::mutex> lg(lock_);
        
        signal_ = true;
        if (blocked_ > 0)
        {
            if (manual_reset_)
            {
                cv_.notify_all();
            }
            else
            {
                cv_.notify_one();
            }
        }
    }

    void reset()
    {
        lock_.lock();
        signal_ = false;
        lock_.unlock();
    }

private:
    std::mutex lock_;
    std::condition_variable cv_;
    bool signal_ = false;
    bool manual_reset_ = true;
    int blocked_ = 0;
}; // class Event

} // namespace zfz

#endif