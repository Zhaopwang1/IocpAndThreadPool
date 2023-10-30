#ifndef IOCPANDTHREADPOOL_THREADQUEUE_H
#define IOCPANDTHREADPOOL_THREADQUEUE_H


#include <list>


#include "Thread.h"


/*++
    线程安全的队列，利用 IOCP 实现
--*/


template<typename T>
class ThreadQueue
{
public:
    enum
        {   // TQ ThreadQueue 缩写
        TQNone,
        TQPush,
        TQPop,
        TQSize,
        TQClear
        };

    // Post Parameter 用于投递信息的结构体
    typedef struct IocpParam
        {
        size_t  sOperator;      // 操作
        T       sData;          // 数据
        HANDLE  sEvent;         // pop 需要
        IocpParam(int op, const T& data, HANDLE hEve = nullptr)
            {
            sOperator = op;
            sData = data;
            sEvent = hEve;
            }
        IocpParam()
            {
            sOperator = TQNone;
            }
        }PPARAM;    // Post Parameter

public:
    ThreadQueue()
        {
        m_lock = false;
        m_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL,1);
        m_hThread = INVALID_HANDLE_VALUE;
        if(m_hIocp)
            {
            m_hThread = reinterpret_cast<HANDLE>(_beginthread(&ThreadQueue<T>::ThreadEntry,0,this));
            }
        }


    virtual ~ThreadQueue()
        {
        if(m_lock)
            {
            return;
            }
        m_lock = true;

        PostQueuedCompletionStatus(m_hIocp,0,NULL,NULL);
        WaitForSingleObject(m_hThread,INFINITE);
        if(m_hIocp)
            {
            HANDLE hTmp = m_hIocp;
            m_hIocp = nullptr;
            CloseHandle(hTmp);
            }
        }

    // 放入队列
    bool PushBack(const T& data)
        {
        IocpParam* pParam = new IocpParam(TQPush,data);
        if(m_lock)
            {
            delete pParam;
            return false;
            }
        bool ret = PostQueuedCompletionStatus(m_hIocp,sizeof(PPARAM),reinterpret_cast<ULONG_PTR>(pParam),NULL);
        if(!ret)
            {
            delete pParam;
            }

        return ret;
        }

    // 弹出
    virtual bool PopFront(T& data)
        {
        HANDLE hEvent = CreateEvent(NULL,TRUE,FALSE,NULL);
        IocpParam Param(TQPop,data,hEvent);

        if(m_lock)
            {
            if(hEvent)
                {
                CloseHandle(hEvent);
                }
            return false;
            }

        bool ret = PostQueuedCompletionStatus(m_hIocp,sizeof(PPARAM),reinterpret_cast<ULONG_PTR>(&Param),NULL);
        if(!ret)
            {
            CloseHandle(hEvent);
            return false;
            }
        ret = WaitForSingleObject(hEvent,INFINITE) == WAIT_OBJECT_0;
        if(ret)
            {
            data = Param.sData;
            }

        return ret;
        }

    // 大小
    size_t Size()
        {
        HANDLE hEvent = CreateEvent(NULL,TRUE,FALSE,NULL);
        IocpParam Param(TQSize,T(),hEvent);
        if(m_lock)
            {
            if(hEvent)
                {
                CloseHandle(hEvent);
                return -1;
                }
            }

        bool ret = PostQueuedCompletionStatus(m_hIocp,sizeof(PPARAM),reinterpret_cast<ULONG_PTR>(&Param),NULL);
        if(!ret)
            {
            CloseHandle(hEvent);
            return -1;
            }
        ret = WaitForSingleObject(hEvent,INFINITE) == WAIT_OBJECT_0;
        if(ret)
            {
            return Param.sOperator;
            }

        return -1;
        }

    // 清理
    bool Clear()
        {
        if(m_lock)
            {
            return false;
            }

        IocpParam* pParam = new IocpParam(TQClear,T());
        bool ret = PostQueuedCompletionStatus(m_hIocp,sizeof(PPARAM),reinterpret_cast<ULONG_PTR>(pParam),NULL);
        if(!ret)
            {
            delete pParam;
            }

        return ret;
        }

protected:
    // 线程入口
    static void ThreadEntry(void* arg)
        {
        ThreadQueue<T>* thiz = reinterpret_cast<ThreadQueue<T>*>(arg);
        thiz->ThreadMain();
        _endthread();
        }

    // 处理操作
    virtual void DealParam(PPARAM* pParam)
        {
        switch(pParam->sOperator)
            {
        case TQPush:
            m_lstData.push_back(pParam->sData);
            delete pParam;
            break;
        case TQPop:
            if(m_lstData.size() > 0)
                {
                pParam->sData = m_lstData.front();
                m_lstData.pop_front();
                }
            if(pParam->sEvent)
                {
                SetEvent(pParam->sEvent);
                }
            break;
        case TQSize:
            pParam->sOperator = m_lstData.size();
            if(pParam->sEvent)
                {
                SetEvent(pParam->sEvent);
                }
            break;
        case TQClear:
            m_lstData.clear();
            delete pParam;
            break;
        default:
            std::cout << "unknown operator!" << std::endl;
            break;
            }
        }

    // IOCP 主要线程
    virtual void ThreadMain()
        {
        DWORD dwTransferred = 0;
        PPARAM* pParam = nullptr;
        ULONG_PTR ulComletionKey = 0;
        OVERLAPPED* pOverlapped = nullptr;

        // 循环获取 I/O 操作
        while(GetQueuedCompletionStatus(m_hIocp,&dwTransferred,&ulComletionKey,&pOverlapped,INFINITE))
            {
            if(!dwTransferred || !ulComletionKey)
                {
                std::cout << "thread is prepare to exit!" << std::endl;
                break;
                }

            pParam = reinterpret_cast<PPARAM*>(ulComletionKey);
            DealParam(pParam);
            }

        // double check
        while(GetQueuedCompletionStatus(m_hIocp,&dwTransferred,&ulComletionKey,&pOverlapped,0))
            {
            if(!dwTransferred || !ulComletionKey)
                {
                std::cout << "thread is prepare to exit!" << std::endl;
                continue;
                }

            pParam = reinterpret_cast<PPARAM*>(ulComletionKey);
            DealParam(pParam);
            }

        HANDLE hTmp = m_hIocp;
        m_hIocp = nullptr;
        CloseHandle(hTmp);
        }

protected:
    std::list<T>        m_lstData;
    HANDLE              m_hIocp;
    HANDLE              m_hThread;
    std::atomic<bool>   m_lock;     // 队列正在析构
};



// 发送队列
template<typename T>
class SendQueue
        : public ThreadQueue<T>,
          public ThreadFuncBase
{
public:
    typedef int (ThreadFuncBase::*THREAD_CALLBACK)(T& data);

public:
    SendQueue(ThreadFuncBase* obj, THREAD_CALLBACK callback) \
        : ThreadQueue<T>(), \
          m_base(obj), \
          m_callback(callback) \
        {
        m_thread.Start();
        m_thread.UpdateWorker(::ThreadWorker(this,reinterpret_cast<FUNCTYPE>(&SendQueue<T>::ThreadTick)));
        }

    virtual ~SendQueue()
        {
        m_base = nullptr;
        m_callback = nullptr;
        }

protected:
    virtual bool PopFront(T& data)
        {
        return false;
        }

    bool PopFront()
        {
        typename ThreadQueue<T>::IocpParam* pParam = new typename ThreadQueue<T>::IocpParam(ThreadQueue<T>::TQPop,T());
        if(ThreadQueue<T>::m_lock)
            {
            delete pParam;
            return false;
            }

        bool ret = PostQueuedCompletionStatus( \
                ThreadQueue<T>::m_hIocp, \
                sizeof(typename ThreadQueue<T>::PPARAM), \
                reinterpret_cast<ULONG_PTR>(&pParam), \
                nullptr);
        if(!ret)
            {
            delete pParam;
            return false;
            }

        return ret;
        }


    //
    int ThreadTick()
        {
        if(ThreadQueue<T>::m_lstData.size() > 0)
            {
            PopFront();
            }
        Sleep(1);
        return 0;
        }

    // 处理操作
    virtual void DealParam(typename ThreadQueue<T>::PPARAM* pParam)
        {
        switch(pParam->sOperator)
            {
        case ThreadQueue<T>::TQPush:
            ThreadQueue<T>::m_lstData.push_back(pParam->sData);
            delete pParam;
            break;
        case ThreadQueue<T>::TQPop:
            if(ThreadQueue<T>::m_lstData.size() > 0)
                {
                pParam->sData = ThreadQueue<T>::m_lstData.front();
                if(0 == (m_base->*m_callback)(pParam->sData))
                    {
                    ThreadQueue<T>::m_lstData.pop_front();
                    }
                delete pParam;
                break;
                }
        case ThreadQueue<T>::TQSize:
            pParam->sOperator = ThreadQueue<T>::m_lstData.size();
            if(pParam->sEvent)
                {
                SetEvent(pParam->sEvent);
                }
            break;
        case ThreadQueue<T>::TQClear:
            ThreadQueue<T>::m_lstData.clear();
            delete pParam;
            break;
        default:
            std::cout << "unknown operator!" << std::endl;
            break;
            }
        }
private:
    ThreadFuncBase*     m_base;
    THREAD_CALLBACK     m_callback;
    Thread              m_thread;
};

typedef SendQueue<std::vector<char>>::THREAD_CALLBACK    SEND_CALLBACK;


#endif //IOCPANDTHREADPOOL_THREADQUEUE_H
