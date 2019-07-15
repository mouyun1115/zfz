/*********************************************************** 
* Date: 2018-09-30 
* 
* Author: nick 
* 
* Email: nick1115@163.com 
* 
* Module: timer 
* 
* Brief: 
* 
* Note: 
* 
* CodePage: UTF-8 
************************************************************/ 
#ifndef __ZFZ_TIMER_H_BY_NICK_2018_09_30__
#define __ZFZ_TIMER_H_BY_NICK_2018_09_30__

#include <chrono>

namespace zfz
{

class Timer
{
public:
    using HRC = std::chrono::high_resolution_clock;
    using TP = HRC::time_point;

    Timer() { reset(); }
    ~Timer() {}
    Timer(const Timer &other) : time_point_(other.time_point_) {}
    Timer& operator=(const Timer &other)
    {
        if (&other != this)
        {
            time_point_ = other.time_point_;
        }
        return *this;
    }

public:
    inline void reset()
    {
        time_point_ = HRC::now();
    }

    inline int64_t tell_s()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(HRC::now() - time_point_).count();
    }

    inline int64_t tell_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(HRC::now() - time_point_).count();
    }

    inline int64_t tell_us()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(HRC::now() - time_point_).count();
    }

    inline int64_t tell_ns()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(HRC::now() - time_point_).count();
    }

private:
    TP time_point_;
};

}

#endif