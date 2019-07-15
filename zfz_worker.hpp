/*********************************************************** 
* Date: 2019-06-12 
* 
* Author: nick 
* 
* Email: nick1115@163.com 
* 
* Module: linux线程模块封装 
* 
* Brief: 比zfz_processor轻量易用 
* 
* Note: 
* 
* CodePage: UTF-8 
************************************************************/ 
#ifndef __ZFZ_WORER_H_BY_NICK_2019_03_08__
#define __ZFZ_WORER_H_BY_NICK_2019_03_08__


#include <memory>
#include <string>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <list>

#define ZFZ_ONLY_USE_PTHREAD 0
#if ZFZ_ONLY_USE_PTHREAD
#include <pthread.h>
#else // use C++11 thread 
#include <thread>
#endif

namespace zfz
{

enum
{
    WORKER_QUEUE_PUSH_BACK = 0,
    WORKER_QUEUE_PUSH_FRONT = 1
};

template<typename T, bool USE_SHARED_PTR> class WorkerTaskType {};
template<typename T> class WorkerTaskType<T, true>
{
public:
    typedef std::shared_ptr<T> WTT;
    typedef std::list<std::shared_ptr<T>> WTT_LIST;
    typedef std::function<void(WTT&)> SINGLE_TASK_HANDLER;
};
template<typename T> class WorkerTaskType<T, false>
{
public:
    typedef T WTT;
    typedef std::list<T> WTT_LIST;
    typedef std::function<void(WTT)> SINGLE_TASK_HANDLER;
};

template<typename T, bool USE_SHARED_PTR = true>
class Worker
{
private:
    static void* thread_function(void *param)
    {
        if (param != nullptr)
        {
            ((Worker*)param)->thread_loop();
        }
        return nullptr;
    }
public:
    typedef typename WorkerTaskType<T, USE_SHARED_PTR>::WTT TASK;
    typedef typename WorkerTaskType<T, USE_SHARED_PTR>::WTT_LIST TASKS;
    typedef typename WorkerTaskType<T, USE_SHARED_PTR>::SINGLE_TASK_HANDLER SINGLE_TASK_HANDLER;
    using MULTI_TASK_HANDLER = std::function<void(TASKS&)>;
    using TIMEOUT_HANDLER = std::function<void()>;
    using MONITOR_HANDLER = std::function<void(size_t, size_t)>; // (current_queue_size, discard size)

public:
    Worker() {}
    Worker(const std::string &name) : name_(name) {}
    Worker(const std::string &name, MONITOR_HANDLER mh, size_t mq_size) : name_(name), monitor_handler_(mh), monitor_queue_size_(mq_size) {}
    virtual ~Worker()
    {
        release();
    }

    int init(int worker_size, int wait_timeout_ms, 
             SINGLE_TASK_HANDLER single_task_handler, 
             MULTI_TASK_HANDLER multi_task_handler, 
             TIMEOUT_HANDLER timeout_handler)
    {
        if (worker_size <= 0)
        {
            return -1;
        }

        if (is_inited())
        {
            release();
        }

        single_task_handler_ = single_task_handler;
        multi_task_handler_ = multi_task_handler;
        timeout_handler_ = timeout_handler;
        wait_timeout_ms_ = wait_timeout_ms;

        for (int i = 0; i < worker_size; ++i)
        {
#if ZFZ_ONLY_USE_PTHREAD
            pthread_t tid = 0;
            if (pthread_create(&tid, nullptr, thread_function, this) == 0)
            {
                threads_.push_back(tid);
            }
            else
            {
                release();
                return -2;
            }
#else
            threads_.emplace_back(std::make_shared<std::thread>([this]()->void { this->thread_loop(); }));
#endif
        }
        return 0;
    }

    void release()
    {
        if (is_inited())
        {
            thread_quit_flag_ = true;
            for (auto &thr : threads_)
            {
#if ZFZ_ONLY_USE_PTHREAD
                pthread_join(thr, nullptr);
#else
                if (thr != nullptr && thr->joinable())
                {
                    thr->join();
                }
#endif
            }
            thread_quit_flag_ = false;
            threads_.clear();

            single_task_handler_ = nullptr;
            multi_task_handler_ = nullptr;
            timeout_handler_ = nullptr;
            wait_timeout_ms_ = 200;
        }
    }

    inline void set_monitor(MONITOR_HANDLER monitor_handler, size_t monitor_queue_size)
    {
        monitor_handler_ = monitor_handler;
        monitor_queue_size_ = monitor_queue_size;
    }
    inline void set_monitor_queue_size(size_t size)
    {
        monitor_queue_size_ = size;
    }
    inline size_t get_monitor_queue_size() const
    {
        return monitor_queue_size_;
    }

    inline bool is_inited() const
    {
        return !threads_.empty();
    }

    inline void set_name(const std::string &name)
    {
        name_ = name;
    }
    inline std::string get_name() const
    {
        return name_;
    }

    inline void set_max_queue_size(size_t max_queue_size)
    {
        max_queue_size_ = max_queue_size;
    }
    inline size_t get_max_queue_size() const
    {
        return max_queue_size_;
    }

    inline void set_wait_timeout_ms(int timeout_ms)
    {
        wait_timeout_ms_ = timeout_ms;
    }

public:
    template<typename U>
    int push_task(U &&task, int push_type = WORKER_QUEUE_PUSH_BACK)
    {
        task_lock_.lock();
        if (task_size_ >= max_queue_size_)
        {
            if (monitor_handler_ != nullptr)
            {
                monitor_handler_(task_size_, 1); // discard one task 
            }
            task_lock_.unlock();
            return -1;
        }
        if (push_type == WORKER_QUEUE_PUSH_BACK)
        {
            tasks_.emplace_back(task);
        }
        else
        {
            tasks_.emplace_front(task);
        }
        auto current_size = ++task_size_;
        task_cv_.notify_all();
        task_lock_.unlock();

        if (current_size > monitor_queue_size_ && monitor_handler_ != nullptr)
        {
            monitor_handler_(current_size, 0);
        }

        return 0;
    }

    template<typename U>
    int push_tasks(U &&tasks, int push_type = WORKER_QUEUE_PUSH_BACK)
    {
        auto size = tasks.size();
        task_lock_.lock();
        auto new_size = task_size_ + size;
        if (new_size > max_queue_size_)
        {
            if (monitor_handler_ != nullptr)
            {
                monitor_handler_(task_size_, size);
            }
            task_lock_.unlock();
            return -1;
        }

        if (push_type == WORKER_QUEUE_PUSH_BACK)
        {
            tasks_.splice(tasks_.end(), tasks);
        }
        else
        {
            tasks_.splice(tasks_.begin(), tasks);
        }
        task_size_ += size;
        task_cv_.notify_all();
        task_lock_.unlock();

        if (new_size > monitor_queue_size_ && monitor_handler_ != nullptr)
        {
            monitor_handler_(new_size, 0);
        }

        return 0;
    }

    int pop_task(TASK &task, int timeout_ms = 200)
    {
        std::unique_lock<std::mutex> task_guard(task_lock_);

        if (task_size_ == 0)
        {
            if (timeout_ms == 0)
            {
                return 1; // immediately timeout 
            }
            if (timeout_ms > 0)
            {
                std::chrono::milliseconds wait_time_ms(timeout_ms);
                if (!task_cv_.wait_for(task_guard, wait_time_ms, [&]{ return task_size_ > 0; }))
                {
                    return 1; // wait occured timeout 
                }
            }
            else
            {
                task_cv_.wait(task_guard, [&]{ return task_size_ > 0; });
            }
        }

        if (task_size_ > 0) // for more safe 
        {
            task = std::move(tasks_.front());
            tasks_.pop_front();
            --task_size_;
            return 0;
        }
        else
        {
            return 2;
        }
    }

    int pop_all_tasks(TASKS &all_tasks, int timeout_ms = 200)
    {
        std::unique_lock<std::mutex> task_guard(task_lock_);

        if (task_size_ == 0)
        {
            if (timeout_ms == 0)
            {
                return 1; // immediately timeout 
            }
            if (timeout_ms > 0)
            {
                std::chrono::milliseconds wait_time_ms(timeout_ms);
                if (!task_cv_.wait_for(task_guard, wait_time_ms, [&]{ return task_size_ > 0; }))
                {
                    return 1; // wait occured timeout 
                }
            }
            else
            {
                task_cv_.wait(task_guard, [&]{ return task_size_ > 0; });
            }
        }

        if (task_size_ > 0)
        {
            all_tasks = std::move(tasks_);
            task_size_ = 0;
            return 0;
        }
        else
        {
            return 2;
        }
    }

private:
    void thread_loop()
    {
        if (multi_task_handler_ != nullptr)
        {
            while (!thread_quit_flag_)
            {
                TASKS all_tasks;
                if (pop_all_tasks(all_tasks, wait_timeout_ms_) == 0)
                {
                    multi_task_handler_(all_tasks);
                }
                else
                {
                    if (timeout_handler_ != nullptr)
                    {
                        timeout_handler_();
                    }
                }
            }
        }
        else if (single_task_handler_ != nullptr)
        {
            while (!thread_quit_flag_)
            {
                TASK task;
                if (pop_task(task, wait_timeout_ms_) == 0)
                {
                    single_task_handler_(task);
                }
                else
                {
                    if (timeout_handler_ != nullptr)
                    {
                        timeout_handler_();
                    }
                }
            }
        }
        else
        {
            while (!thread_quit_flag_)
            {
                TASK task;
                if (pop_task(task, wait_timeout_ms_) == 0)
                {
                    // single_task_handler_(task);
                }
                else
                {
                    if (timeout_handler_ != nullptr)
                    {
                        timeout_handler_();
                    }
                }
            }
        }
    }

private:
    SINGLE_TASK_HANDLER single_task_handler_{nullptr};
    MULTI_TASK_HANDLER multi_task_handler_{nullptr};
    TIMEOUT_HANDLER timeout_handler_{nullptr};
    int wait_timeout_ms_ = 200;

    MONITOR_HANDLER monitor_handler_{nullptr};
    size_t monitor_queue_size_ = 32;
    size_t max_queue_size_ = 1024;

    volatile bool thread_quit_flag_ = false;
#if ZFZ_ONLY_USE_PTHREAD
    std::vector<pthread_t> threads_;
#else // use C++11 thread 
    std::vector<std::shared_ptr<std::thread>> threads_;
#endif
    TASKS tasks_;
    size_t task_size_ = 0;
    std::mutex task_lock_;
    std::condition_variable task_cv_;

    std::string name_;
};

} // namespace zfz 

#endif