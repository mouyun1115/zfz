/*********************************************************** 
* Date: 2016-07-08 
* 
* Author: nick 
* 
* Email: nick1115@163.com 
* 
* Module: 对象池 
* 
* Brief: 
* 
* Note: 
* 
* CodePage: UTF-8 
************************************************************/ 
#ifndef	__ZFZ_OBJECT_POOL_HPP_BY_NICK_2014_10_27__
#define	__ZFZ_OBJECT_POOL_HPP_BY_NICK_2014_10_27__

#include <type_traits>
#include <list>
#include <mutex>
#include <memory>
#include "zfz_sfinae.hpp"

namespace zfz
{

template<typename T>
class ObjectPoolImpl
{
public:
    ObjectPoolImpl() {}
    ObjectPoolImpl(size_t capacity) : capacity_(capacity) {}
    ~ObjectPoolImpl() { reset(); }

private:
    ObjectPoolImpl(const ObjectPoolImpl&) = delete;
    ObjectPoolImpl(ObjectPoolImpl&&) = delete;
    ObjectPoolImpl& operator=(const ObjectPoolImpl&) = delete;

private:
    std::list<T*> pool_;
    size_t pool_size_ = 0; // list::size() is inefficient, so we maintain pool size 
    size_t capacity_ = 8; // when push(), delete or hold, determined by this var, default value set to 8 
    size_t created_size_ = 0; // total size of T, in pool size and out pool size 
    std::mutex lock_;

public:
    T* pop()
    {
        std::lock_guard<std::mutex> lg(lock_);
        T *obj = nullptr;
        if (pool_size_ > 0)  // if pool is empty, create object 
        {
            obj = pool_.front();
            pool_.pop_front();
            --pool_size_;
        }
        else
        {
            obj = new T();
            ++created_size_;
        }
        return obj;
    }

    void push(T *obj)
    {
        if (obj == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lg(lock_);
        if (pool_size_ < capacity_)
        {
            SFINAE::clear_object(obj); // using template meta programm 
            pool_.push_back(obj);
            ++pool_size_;
        }
        else
        {
            delete obj; // if pool is full, delete the object 
            --created_size_;
        }
    }

    void reset()
    {
        std::lock_guard<std::mutex> lg(lock_);
        if (pool_size_ > 0)
        {
            for (auto &obj : pool_)
            {
                delete obj;
                --created_size_;
            }
            pool_.clear();
            pool_size_ = 0;
        }
    }

    void set_capacity(const size_t size)
    {
        std::lock_guard<std::mutex> lg(lock_);
        capacity_ = size;
        if (pool_size_ > capacity_)
        {
            auto itr = pool_.begin();
            std::advance(itr, pool_size_ - capacity_);
            std::list<T*> need_to_delete;
            need_to_delete.splice(need_to_delete.begin(), pool_, pool_.begin(), itr);
            for (auto &obj : need_to_delete)
            {
                delete obj;
                --created_size_;
            }
            pool_size_ = capacity_;
        }
    }
    
    inline size_t get_capacity() const
    {
        return capacity_;
    }
    inline size_t get_available_size() const
    {
        return pool_size_;
    }
    inline size_t get_created_size() const
    {
        return created_size_;
    }
};

template<typename T>
class ObjectPool
{
public:
    static inline T* pool_pop()
    {
        return pool_.pop();
    }

    static inline void pool_push(T *p)
    {
        pool_.push(p);
    }

    static inline void pool_reset()
    {
        pool_.reset();
    }

    static inline std::shared_ptr<T> pool_pop_sp()
    {
        return std::shared_ptr<T>(pool_pop(), pool_push);
    }

    static inline void pool_set_capacity(const size_t size)
    {
        pool_.set_capacity(size);
    }
    static inline size_t pool_get_capacity()
    {
        return pool_.get_capacity();
    }
    static inline size_t pool_get_available_size()
    { 
        return pool_.get_available_size();
    }
    static inline size_t pool_get_created_size()
    {
        return pool_.get_created_size();
    }

private:
    static ObjectPoolImpl<T> pool_;
};

template<typename T> typename zfz::ObjectPoolImpl<T> ObjectPool<T>::pool_; // this is wonderful 

}

#endif
