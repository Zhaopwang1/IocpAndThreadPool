#ifndef IOCPANDTHREADPOOL_THREAD_H
#define IOCPANDTHREADPOOL_THREAD_H


#include <Windows.h>
#include <process.h>


#include <iostream>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>


/*++
    线程池
        为了更好的控制线程，比如线程的开启和关闭，这里使用了 Windows 创建线程的方式，而不是 std::thread
--*/



class ThreadFuncBase{};
typedef int (ThreadFuncBase::*FUNCTYPE)();


class ThreadWorker
{
public:
    ThreadWorker() \
        : m_thiz(nullptr), \
          m_func(nullptr) \
          {  }

    ThreadWorker(void* obj,FUNCTYPE f) \
        : m_thiz(reinterpret_cast<ThreadFuncBase*>(obj)), \
          m_func(f) \
          {  }

    ThreadWorker(const ThreadWorker& worker)
        {
        m_thiz = worker.m_thiz;
        m_func = worker.m_func;
        }

    ThreadWorker& operator=(const ThreadWorker& worker)
        {
        if(this != &worker)
            {
            m_thiz = worker.m_thiz;
            m_func = worker.m_func;
            }
        return *this;
        }

    int operator()()
        {
        if(IsValid())
            {
            return (m_thiz->*m_func)();
            }
        }

    // 是否有效
    bool IsValid() const
        { return (m_thiz != nullptr) && (m_func != nullptr); }

private:
    ThreadFuncBase* m_thiz;
    FUNCTYPE        m_func;
};


/*++
    管理线程状态
--*/
class Thread
{
public:
    Thread()
        {
        m_hThread = nullptr;
        m_bStatus = false;
        }

    ~Thread()
        { Stop(); }

    // 启动线程,true or false
    bool Start()
        {
        m_bStatus = true;
        m_hThread = reinterpret_cast<HANDLE>(_beginthread(&Thread::ThreadEntry,0,this));
        if(!IsValid())
            {
            m_bStatus = false;
            }
        return m_bStatus;
        }

    // 是否有效，true 表示有效，false 表示线程异常或已经终止
    bool IsValid()
        {
        if(!m_hThread || (INVALID_HANDLE_VALUE == m_hThread))
            {
            return false;
            }
        return WaitForSingleObject(m_hThread,0) == WAIT_TIMEOUT;
        }

    // 停止线程
    bool Stop()
        {
        if(!m_bStatus)
            {
            return true;
            }
        m_bStatus = false;
        bool ret = WaitForSingleObject(m_hThread,INFINITE) == WAIT_OBJECT_0;
        UpdateWorker();
        return ret;
        }

    // 更新线程
    void UpdateWorker(const ::ThreadWorker& worker = ::ThreadWorker())
        {
        if(m_worker.load() && (m_worker.load() != &worker))
            {
            ::ThreadWorker* pWorker = m_worker.load();  // .load 用于读取 std::atomic 对象的值，同时保证操作的原子性
            m_worker.store(nullptr);
            delete pWorker;
            }
        if(!worker.IsValid())
            {
            m_worker.store(nullptr);
            return;
            }
        m_worker.store(new ::ThreadWorker(worker));
        }

    // 线程是否是闲置的。true表示空闲，false表示已经分配了工作
    bool IsIdle()
        {
        if(!m_worker)
            {
            return true;
            }
        return !m_worker.load()->IsValid();
        }

private:
    // 工作线程
    void ThreadWorker()
        {
        while(m_bStatus)
            {
            if(!m_worker)
                {
                Sleep(1);
                continue;
                }
            ::ThreadWorker worker = *m_worker.load();
            if(worker.IsValid())
                {
                int ret = worker();
                if(ret)
                    {
                    std::string str;
                    str = "thread found warning code " + std::to_string(ret);
                    std::cerr << str << std::endl;
                    }
                if(ret < 0)
                    {
                    m_worker.store(nullptr);
                    }
                }
            else
                {
                Sleep(1);
                }
            }
        }

    // 线程入口
    static void ThreadEntry(void* arg)
        {
        Thread* thiz = reinterpret_cast<Thread*>(arg);
        if(thiz)
            {
            thiz->ThreadWorker();
            }
        _endthread();
        }
private:
    HANDLE                          m_hThread;
    bool                            m_bStatus;      // 线程的状态。true 表示该线程正在运行，false 表示线程将要关闭
    std::atomic<::ThreadWorker*>    m_worker;       // 原子操作
};


/*++
    线程池
--*/
class ThreadPool
{
public:
    ThreadPool(size_t size)
        {
        m_threads.resize(size);
        for(size_t i = 0; i != size; ++i)
            {
            m_threads[i] = new Thread;
            }
        }

    ThreadPool() = default;

    ~ThreadPool()
        {
        Stop();
        for(size_t i = 0;i != m_threads.size(); ++i)
            {
            Thread* pThread = m_threads[i];
            m_threads[i] = nullptr;
            delete pThread;
            }
        m_threads.clear();
        }

    // 启动
    bool Invoke()
        {
        bool ret = true;
        for(size_t i = 0; i != m_threads.size(); ++i)
            {
            if(!m_threads[i]->Start())
                {
                ret = false;
                break;
                }
            }
        if(!ret)
            {
            for(size_t i = 0; i != m_threads.size(); ++i)
                {
                m_threads[i]->Stop();
                }
            }
        return ret;
        }

    // 停止
    void Stop()
        {
        for(size_t i = 0; i != m_threads.size(); ++i)
            {
            m_threads[i]->Stop();
            }
        }

    // 分发线程。
    // 返回 -1 表示分配失败，所有线程都在忙。
    // >= 0 表示分配第 n 个线程来做这个事
    int DispatchWorker(const ThreadWorker& worker)
        {
        int index = -1;
        std::lock_guard<std::mutex> guard(m_lock);
        for(size_t i = 0; i != m_threads.size(); ++i)
            {
            if(m_threads[i] && m_threads[i]->IsIdle())
                {
                m_threads[i]->UpdateWorker(worker);
                index = i;
                break;
                }
            }
        return index;
        }

        // 检查线程是否有效
        bool CheckThreadValid(size_t index)
            {
            if(index < m_threads.size())
                {
                return m_threads[index]->IsValid();
                }
            return false;
            }
private:
    std::mutex              m_lock;
    std::vector<Thread*>    m_threads;
};



#endif //IOCPANDTHREADPOOL_THREAD_H
