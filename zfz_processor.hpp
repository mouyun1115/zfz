/*********************************************************** 
* Date: 2017-05-16 
* 
* Author: nick 
* 
* Email: nick1115@163.com 
* 
* Module: 流水线单元 
* 
* Brief: 
* 
* Note: 
* 
* CodePage: UTF-8 
************************************************************/ 
#ifndef __ZFZ_PROCESSOR_HPP_BY_NICK_2017_05_16__
#define __ZFZ_PROCESSOR_HPP_BY_NICK_2017_05_16__

#include <string>
#include <list>
#include <memory>
#include <thread>
#include <chrono>
#include <functional>

#include "zfz_sfinae.hpp"
#include "zfz_semaphore.hpp"
#include "zfz_event.hpp"

namespace zfz
{

static inline void sleep_s(int s)
{
    std::this_thread::sleep_for(std::chrono::seconds(s));
}

static inline void sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static inline void sleep_us(int us)
{
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

static inline void sleep_ns(int ns)
{
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

class ThreadWrapper
{
public:
    ThreadWrapper() {}
    ~ThreadWrapper()
    {
        /// < thread_wrapper析构前，用户应停止线程，这里的代码是为异常情况准备的 
        if (thread_ != nullptr && thread_->joinable())
        {
            set_quit_flag();
            std::this_thread::yield();
            thread_->detach(); /// < 不要使用 join() 
            thread_.reset();
        }
    }

private:
    ThreadWrapper(const ThreadWrapper&) = delete;
    ThreadWrapper(ThreadWrapper&&) = delete;
    ThreadWrapper& operator=(const ThreadWrapper&) = delete;

public:
    std::shared_ptr<std::thread> thread_; /// < 线程对象 
    volatile int inited_flag_ = 0; /// < spinlock,在创建者与线程之间同步,保证创建者正常完成thread_wrapper的创建 
    volatile int quit_flag_ = 0; /// < 线程退出标志 

public:
    inline void set_quit_flag()
    {
        quit_flag_ = 1;
    }
    inline void reset_quit_flag()
    {
        quit_flag_ = 0;
    }
    inline bool is_thread_quit() const
    {
        return quit_flag_ != 0;
    }
};

enum
{
    ZFZ_PROCESSOR_UNKNOWN_ERROR = -2,
    ZFZ_PROCESSOR_FAIL = -1,
    ZFZ_PROCESSOR_SUCCESS = 0,
    ZFZ_PROCESSOR_TIME_OUT = 1,
    ZFZ_PROCESSOR_QUEUE_FULL = 2,
    ZFZ_PROCESSOR_QUEUE_EMPTY = 3,
    ZFZ_PROCESSOR_NO_WORKING_THREAD = 4
};

template<typename T>
class Processor
{
public:
    typedef std::shared_ptr<ThreadWrapper> THREAD_WRAPPER_POINTER;
    typedef std::shared_ptr<T> TASK;
    typedef std::list<std::shared_ptr<T>> TASK_LIST;
    typedef void* TLS; // TLS == thread local resource 

    using TASK_HANDLER = std::function<void(TASK_LIST&, void*)>;
    using TIMEOUT_HANDLER = std::function<void(void*)>;
    using TLS_CREATOR = std::function<TLS()>;
    using TLS_DESTROYER = std::function<void(TLS)>;

public:
    Processor() {}

    virtual ~Processor()
    {
        end_all_threads();
    }

    static inline bool compare_shared_ptr_t(const std::shared_ptr<T> &t1, const std::shared_ptr<T> &t2)
    {
        return SFINAE::compare_t(*t1, *t2);
    }

private:
    Processor(const Processor&) = delete;
    Processor(Processor&&) = delete;
    Processor& operator=(const Processor&) = delete;

protected:
    std::list<std::shared_ptr<T>> task_queue_;
    volatile int current_queue_size_ = 0; // we maintain the queue size, not use std::list::size() in gcc stl
    volatile int queue_full_flag_ = 0; // flag whether current_queue_size_ has reached to max_queue_size_ or not
    volatile int queue_need_sort_flag_ = 0; // 标志队列任务是否需要排序 
    std::mutex queue_lock_;
    zfz::Semaphore task_semaphore_;
    volatile int max_queue_size_ = 1024;
    volatile int queue_ordered_flag_ = 0; // 标志队列中的任务是否已排序 

    volatile int batch_size_ = 1; // 线程从队列取任务时,每次可以取出的最大数量,默认设为1,设为负值表示一次性取出队列中的所有任务 
    volatile int thread_wait_time_ms_ = 200; // 线程等待任务的超时时间(毫秒),200ms接近正常人的反应极限 

    std::list<THREAD_WRAPPER_POINTER> thread_list_;
    std::mutex thread_lock_;
    volatile int max_thread_size_ = 1024;
    zfz::Event thread_create_event_;

    std::list<Processor<T>*> next_processors_;

    // 函数指针比虚函数有更好的灵活性 
    TASK_HANDLER task_handler_{nullptr};
    TIMEOUT_HANDLER timeout_handler_{nullptr};

    TLS_CREATOR tls_creator_{nullptr};
    TLS_DESTROYER tls_destroyer_{nullptr};

    int processor_id_ = 0;
    std::string processor_name_;

protected:
    void remove_thread(const THREAD_WRAPPER_POINTER &thread)
    {
        std::lock_guard<std::mutex> guard(thread_lock_);
        for (auto itr = thread_list_.begin(); itr != thread_list_.end(); ++itr)
        {
            if ((*itr) == thread)
            {
                thread_list_.erase(itr);
                return;
            }
        }
    }

    void end_one_thread(THREAD_WRAPPER_POINTER &thread, bool sync = true)
    {
        if (thread != nullptr && thread->thread_ != nullptr && thread->thread_->joinable())
        {
            thread->set_quit_flag();
            std::this_thread::yield();

            if (sync)
            {
                thread->thread_->join();
            }
            else
            {
                thread->thread_->detach();
            }

            thread->thread_.reset();
        }
    }

protected:
    virtual void handle_task(TASK_LIST &tasks, TLS tls)
    {
        return;
    }

    virtual void handle_timeout(TLS tls)
    {
        return;
    }

protected:
    virtual int fan_out(TASK_LIST &tasks)
    {
        int result = ZFZ_PROCESSOR_SUCCESS;
        for (auto &processor : this->next_processors_)
        {
            result = processor->push_task(tasks);
            if (result != ZFZ_PROCESSOR_SUCCESS)
            {
                int handle_result = handle_fan_out_error(result, processor, tasks);
                if (handle_result != ZFZ_PROCESSOR_SUCCESS)
                {
                    return handle_result;
                }
            }
        }

        return ZFZ_PROCESSOR_SUCCESS;
    }

    virtual int handle_fan_out_error(int push_result, Processor<T> *p, TASK_LIST &tasks)
    {
        return ZFZ_PROCESSOR_SUCCESS;
    }

public:
    // NOTE: 下面这种检查是否工作的方法理论上是不安全的,但由于目前的使用方式均为创建时启动所有需要的线程，
    //       运行过程中不会进行线程的开启和关闭,因此实际上是安全。若以后有业务需要在运行过程中开关线程，需要修改这个方法
    inline bool is_working() const
    {
        return !thread_list_.empty();
    }

    inline void set_max_queue_size(const int max_size)
    {
        max_queue_size_ = max_size;
    }
    inline int get_max_queue_size() const
    {
        return max_queue_size_;
    }

    inline int get_batch_size() const
    {
        return batch_size_;
    }
    inline void set_batch_size(const int batch_size)
    {
        batch_size_ = batch_size;
    }

    int add_next_processor(Processor<T> *p) // current, it's not thread safe 
    {
        if (p == nullptr)
        {
            return ZFZ_PROCESSOR_FAIL;
        }

        /// <  keep unique 
        for (auto &cur_processor : next_processors_)
        {
            if (cur_processor == p)
            {
                return ZFZ_PROCESSOR_FAIL;
            }
        }

        next_processors_.push_back(p);

        return ZFZ_PROCESSOR_SUCCESS;
    }

    int remove_next_processor(Processor<T> *p) // current, it's not thread safe 
    {
        for (auto itr = next_processors_.begin(); itr != next_processors_.end(); ++itr)
        {
            if (*itr == p)
            {
                next_processors_.erase(itr);
                return ZFZ_PROCESSOR_SUCCESS;
            }
        }

        return ZFZ_PROCESSOR_FAIL;
    }

    int get_all_processors_count() const
    {
        int count = 1; // include self 
        for (auto &p : next_processors_)
        {
            count += p->get_all_processors_count();
        }
        return count;
    }

    inline void set_processor_id(const int id)
    {
        processor_id_ = id;
    }
    inline int get_processor_id() const
    {
        return processor_id_;
    }

    inline void set_processor_name(const std::string &name)
    {
        processor_name_ = name;
    }
    inline const std::string& get_processor_name() const
    {
        return processor_name_;
    }

public:
    inline void set_task_handler(TASK_HANDLER handler)
    {
        task_handler_ = handler;
    }
    inline void set_timeout_handler(TIMEOUT_HANDLER handler)
    {
        timeout_handler_ = handler;
    }

    inline void set_tls_creator(TLS_CREATOR creator)
    {
        tls_creator_ = creator;
    }
    inline void set_tls_destroyer(TLS_DESTROYER destroyer)
    {
        tls_destroyer_ = destroyer;
    }

protected:
    void thread_funciton(THREAD_WRAPPER_POINTER ThreadWrapper)
    {
        if (ThreadWrapper == nullptr)
        {
            return;
        }

        // spinlock, wait for thread object complete 
        while (ThreadWrapper->inited_flag_ == 0)
        {
            std::this_thread::yield();
        }

        int wait_time_ms = this->thread_wait_time_ms_;
        int batch_size = this->batch_size_;
        TLS tls = nullptr;

        if (this->tls_creator_ != nullptr)
        {
            tls = this->tls_creator_();
        }

        this->thread_create_event_.set();

        std::list<std::shared_ptr<T>> tasks;
        int result = 0;

        while (true)
        {
            result = this->pop_task(tasks, batch_size, wait_time_ms);
            if (ThreadWrapper->is_thread_quit())
            {
                break;
            }

            if (result == ZFZ_PROCESSOR_SUCCESS)
            {
                if (this->task_handler_ != nullptr) // 优先使用函数指针 
                {
                    this->task_handler_(tasks, tls);
                }
                else
                {
                    this->handle_task(tasks, tls);
                }

                this->fan_out(tasks);
                tasks.clear();
            }
            else if (result == ZFZ_PROCESSOR_TIME_OUT)
            {
                if (this->timeout_handler_ != nullptr)
                {
                    this->timeout_handler_(tls);
                }
                else
                {
                    this->handle_timeout(tls);
                }
            }
            else
            {
                // queue empty, ignore it 
            }
        }

        if (tls != nullptr && this->tls_destroyer_ != nullptr)
        {
            this->tls_destroyer_(tls);
            tls = nullptr;
        }

        if (!ThreadWrapper->is_thread_quit())
        {
            // thread quits itself, not notified, clean thread wrapper 
            if (ThreadWrapper->thread_ != nullptr)
            {
                ThreadWrapper->thread_->detach();
            }
            this->remove_thread(ThreadWrapper);
        }
    }

public:
    int begin_thread(const int count = 1)
    {
        if (count <= 0)
        {
            return 0;
        }

        std::lock_guard<std::mutex> auto_lock(thread_lock_);

        int cur_count = static_cast<int>(thread_list_.size());
        int create_count = 0;

        for (; create_count < count;)
        {
            if (cur_count >= max_thread_size_)
            {
                break;
            }

            try
            {
                thread_create_event_.reset();

                auto ThreadWrapper(std::make_shared<ThreadWrapper>());
                ThreadWrapper->thread_ = std::make_shared<std::thread>(std::bind(&Processor<T>::thread_funciton, this, ThreadWrapper));
                ThreadWrapper->inited_flag_ = 1; // spinlock,通知被创建的线程thread_wrapper已准备就绪 

                thread_create_event_.wait();

                thread_list_.emplace_back(ThreadWrapper);

                ++cur_count;
                ++create_count;
            }
            catch (...)
            {
                return create_count;
            }
        }

        return create_count;
    }

    int end_thread(const int count = 1, bool sync = true)
    {
        if (count <= 0)
        {
            return 0;
        }

        int tar_end_count = count;
        int real_ended_count = 0;

        while (tar_end_count-- > 0)
        {
            thread_lock_.lock();

            if (thread_list_.empty())
            {
                thread_lock_.unlock();
                break;
            }

            auto thread(std::move(thread_list_.front()));
            thread_list_.pop_front();

            thread_lock_.unlock();

            end_one_thread(thread, sync);

            ++real_ended_count;
        }

        return real_ended_count;
    }

    void end_all_threads(bool sync = true)
    {
        std::unique_lock<std::mutex> auto_lock(thread_lock_);

        if (thread_list_.empty())
        {
            return;
        }

        auto temp_thread_list(std::move(thread_list_)); // 取出所有线程 

        auto_lock.unlock(); // 手动解锁，防止join()时死锁 

        // 停止这些线程 
        for (auto & thread : temp_thread_list)
        {
            end_one_thread(thread, sync);
        }
    }

public:
    virtual int push_task(std::shared_ptr<T> &task)
    {
        std::list<std::shared_ptr<T>> tasks;
        tasks.push_back(task);
        return push_task(tasks);
    }
    virtual int push_task(TASK_LIST &tasks)
    {
        auto task_size = tasks.size();
        if (task_size == 0)
        {
            return ZFZ_PROCESSOR_SUCCESS;
        }

        std::lock_guard<std::mutex> auto_lock(queue_lock_);

        if (current_queue_size_ + task_size > max_queue_size_)
        {
            if (queue_full_flag_ == 0)
            {
                queue_full_flag_ = 1;
            }
            return ZFZ_PROCESSOR_QUEUE_FULL;
        }
        else
        {
            auto task_copy(tasks);
            task_queue_.splice(task_queue_.end(), task_copy);
            current_queue_size_ += task_size;
            task_semaphore_.signal(task_size);
            queue_ordered_flag_ = 0;
            return ZFZ_PROCESSOR_SUCCESS;
        }
    }

protected:
    virtual int pop_task(TASK_LIST &tasks, const int batch_size = 1, const int wait_time_ms = (-1))
    {
        if (task_semaphore_.wait(wait_time_ms) != zfz::ZFZ_SEMPHORE_SUCCESS)
        {
            return ZFZ_PROCESSOR_TIME_OUT;
        }

        std::lock_guard<std::mutex> lg(queue_lock_);

        if (queue_full_flag_ != 0)
        {
            queue_full_flag_ = 0;
        }

        if (current_queue_size_ <= 0)
        {
            return ZFZ_PROCESSOR_QUEUE_EMPTY;
        }

        // 检查是否需要对任务排序 
        if (queue_need_sort_flag_ != 0 && queue_ordered_flag_ == 0)
        {
            this->task_queue_.sort(compare_shared_ptr_t);
            queue_ordered_flag_ = 1;
        }

        int poped_count = 0;
        if (batch_size > 0 && current_queue_size_ >= batch_size)
        {
            auto end = task_queue_.begin();
            std::advance(end, batch_size);
            tasks.splice(tasks.end(), task_queue_, task_queue_.begin(), end);
            current_queue_size_ -= batch_size;
            poped_count = batch_size;
        }
        else // batch_size为负值或不足batch_size个任务时，将任务全部取出 
        {
            tasks.splice(tasks.end(), task_queue_);
            current_queue_size_ = 0;
            poped_count = current_queue_size_;
        }

        if (poped_count > 1) // semphore::wait()释放掉了一个信号，所以这里释放掉poped_count - 1个信号
        {
            task_semaphore_.release(poped_count - 1);
        }

        return ZFZ_PROCESSOR_SUCCESS;
    }
}; // class Processor<T> 

} // namespace zfz 

#endif
