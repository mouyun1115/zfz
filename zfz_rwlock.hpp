/*********************************************************** 
* Date: 2019-03-27 
* 
* Author: nick 
* 
* Email: nick1115@163.com 
* 
* Module: 读写锁 
* 
* Brief: 写优先 
* 
* Note: 
* 
* CodePage: UTF-8 
************************************************************/ 
#ifndef __ZFZ_RW_LOCK_H_BY_NICK_2019_03_27__
#define __ZFZ_RW_LOCK_H_BY_NICK_2019_03_27__

#include <mutex>
#include <condition_variable>

namespace zfz
{

class RWLock
{
public:
    void rlock()
    {
        std::unique_lock<std::mutex> ul(mtx_);
        if (status_ < 0)
        {
            ++read_blocked_count_;
            cv_read_.wait(ul, [&]()->bool { return write_blocked_count_ == 0 && status_ >= 0; });
            --read_blocked_count_;
        }
        ++status_;
    }

    void wlock()
    {
        std::unique_lock<std::mutex> ul(mtx_);
        if (status_ != 0)
        {
            ++write_blocked_count_;
            cv_write_.wait(ul, [&]()->bool { return status_ == 0; });
            --write_blocked_count_;
        }
        status_ = -1;
    }
    void unlock()
    {
        std::lock_guard<std::mutex> lg(mtx_);
        if (status_ < 0)
        {
            status_ = 0;
        }
        else if (status_ > 0)
        {
            --status_;
        }
        else // status_ == 0 invalid call
        {
            return; 
        }

        if (status_ == 0)
        {
            if (write_blocked_count_ > 0)
            {
                cv_write_.notify_one();
            }
            else if (read_blocked_count_ > 0)
            {
                cv_read_.notify_all();
            }
        }
    }

private:
    volatile int read_blocked_count_ = 0;
    volatile int write_blocked_count_ = 0;

    // -1    : one writer
    // 0     : no reader and no writer
    // n > 0 : n reader
    volatile int status_ = 0;

    std::condition_variable cv_read_;
    std::condition_variable cv_write_;
    std::mutex mtx_;
};

// template <typename _RWLockable>
// class unique_writeguard
// {
// public:
//     explicit unique_writeguard(_RWLockable &rw_lockable)
//         : rw_lockable_(rw_lockable)
//     {
//         rw_lockable_.lock_write();
//     }
//     ~unique_writeguard()
//     {
//         rw_lockable_.release_write();
//     }
// private:
//     unique_writeguard() = delete;
//     unique_writeguard(const unique_writeguard&) = delete;
//     unique_writeguard& operator=(const unique_writeguard&) = delete;
// private:
//     _RWLockable &rw_lockable_;
// };

// template <typename _RWLockable>
// class unique_readguard
// {
// public:
//     explicit unique_readguard(_RWLockable &rw_lockable)
//         : rw_lockable_(rw_lockable)
//     {
//         rw_lockable_.lock_read();
//     }
//     ~unique_readguard()
//     {
//         rw_lockable_.release_read();
//     }
// private:
//     unique_readguard() = delete;
//     unique_readguard(const unique_readguard&) = delete;
//     unique_readguard& operator=(const unique_readguard&) = delete;
// private:
//     _RWLockable &rw_lockable_;
// };

} // namespace zfz 

#endif  
