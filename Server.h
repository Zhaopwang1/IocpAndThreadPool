#ifndef IOCPANDTHREADPOOL_SERVER_H
#define IOCPANDTHREADPOOL_SERVER_H

#include <MSWSock.h>


#include <map>


#include "Thread.h"
#include "ThreadQueue.h"
#include "Tools.h"



// I/O 操作
enum IoOperator
    {
    IONone,
    IOAccept,
    IORecv,
    IOSend,
    IOError
    };


class Server;
class Client;
typedef std::shared_ptr<Client>  PTR_CLIENT;


// 重叠结构
class IoOverlapped \
        : public ThreadFuncBase
{
public:
    IoOverlapped() = default;
    virtual ~IoOverlapped() { m_client = nullptr; }
public:
    OVERLAPPED          m_overlapped;
    DWORD               m_operator;
    std::vector<char>   m_buffer;       // 缓冲区
    ThreadWorker        m_worker;       // 处理函数
    Server*             m_server;       // 服务器对象
    Client*             m_client;       // 客户端对象
    WSABUF              m_wsaBuffer;
};

template<IoOperator> \
class AcceptOverlapped; \
typedef AcceptOverlapped<IOAccept>  ACCEPTOVERLAPPED;

template<IoOperator> \
class RecvOverlapped; \
typedef RecvOverlapped<IORecv>      RECVOVERLAPPED;

template<IoOperator> \
class SendOverlapped; \
typedef SendOverlapped<IOSend>      SENDOVERLAPPED;


// 客户端
class Client \
        : public ThreadFuncBase
{
public:
    Client();
    ~Client();

    // 设置重叠结构
    void SetOverlapped(Client* ptr);

    operator SOCKET() { return m_sock; };
    operator PVOID() { return reinterpret_cast<PVOID>(m_buffer.data()); }
    operator LPOVERLAPPED();
    operator LPDWORD() { return &m_dwReceived; }

    LPWSABUF RecvWSABuffer();
    LPOVERLAPPED RecvOverlapped();

    LPWSABUF SendWSABuffer();
    LPOVERLAPPED SendOverlapped();

    DWORD& GetFlags() { return m_dwFlags; }
    sockaddr_in* GetLocalAddr() { return &m_laddr; }
    sockaddr_in* GetRemoteAddr() { return &m_raddr; }
    size_t GetBufferSize() const { return m_buffer.size(); }

    // 接收
    int Recv();

    // 发送
    int Send(void* buffer, size_t size);

    // 发送数据
    int SendData(std::vector<char>& data);

private:
    SOCKET                              m_sock;
    DWORD                               m_dwReceived;
    DWORD                               m_dwFlags;
    std::shared_ptr<ACCEPTOVERLAPPED>   m_ptrOverlapped;
    std::shared_ptr<RECVOVERLAPPED>     m_ptrRecv;
    std::shared_ptr<SENDOVERLAPPED>     m_ptrSend;
    std::vector<char>                   m_buffer;
    size_t                              m_usedBuf;      // 已经使用的缓冲区大小
    sockaddr_in                         m_laddr;        // local
    sockaddr_in                         m_raddr;        // remote
    bool                                m_isBusy;       // 是否在忙
    SendQueue<std::vector<char>>        m_vecSend;      // 发送数据队列
};



// Accept - Overlapped
template<IoOperator> \
class AcceptOverlapped \
        : public IoOverlapped \
{
public:
    AcceptOverlapped();
    virtual ~AcceptOverlapped() = default;
public:
    int AcceptWorker();
};



// Recv - Overlapped
template<IoOperator> \
class RecvOverlapped \
        : public IoOverlapped \
{
public:
    RecvOverlapped();
    virtual ~RecvOverlapped() = default;
public:
    int RecvWorker()
        {
        int ret = m_client->Recv();
        return ret;
        }
};


template<IoOperator> \
class SendOverlapped \
        : public IoOverlapped \
{
public:
    SendOverlapped();
    virtual ~SendOverlapped() = default;
public:
    int SendWorker()
        {
        // TODO: Send 可能不会立即完成
        return -1;
        }
};
typedef SendOverlapped<IOSend>   SENDOVERLAPPED;


template<IoOperator> \
class ErrorOverlapped \
        : public IoOverlapped \
{
public:
    ErrorOverlapped();
    virtual ~ErrorOverlapped() = default;
public:
    int ErrorWorker()
        {
        // TODO: 报错
        return -1;
        }
};
typedef ErrorOverlapped<IOError>    ERROROVERLAPPED;



class Server
        : public ThreadFuncBase
{
public:
    Server(const std::string& ip = "0.0.0.0", short port = 9527) \
        : m_pool(10) \
        {
        m_hIocp = INVALID_HANDLE_VALUE;
        m_sock = INVALID_SOCKET;
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = htons(port);
        m_addr.sin_addr.s_addr = inet_addr(ip.c_str());
        }

    ~Server();
public:
    // IOCP 流程函数，绑定 IOCP 并启动线程池
    bool StartServer();

    // 新连接
    bool NewAccept()
        {
        Client* pClient = new Client;
        pClient->SetOverlapped(pClient);
        m_client.insert(std::pair<SOCKET,Client*>(*pClient,pClient));
        if(!AcceptEx(m_sock,*pClient,*pClient,0,sizeof(sockaddr_in) + 16,sizeof(sockaddr_in) + 16,*pClient,*pClient))
            {
            if(WSAGetLastError() != ERROR_SUCCESS && (WSAGetLastError() != WSA_IO_PENDING))
                {
                std::cerr << "AcceptEx failed! [" << WSAGetLastError() \
                          << "] (" << Tools::GetErrorInfo(WSAGetLastError()).c_str() \
                          << ")" << std::endl;
                closesocket(m_sock);
                m_sock = INVALID_SOCKET;
                m_hIocp = INVALID_HANDLE_VALUE;

                return false;
                }
            }
        return true;
        }

    // 绑定新套接字
    void BindNewSocket(SOCKET s, ULONG_PTR ulKey);

private:
    // 创建套接字
    void CreateSocket()
        {
        WSADATA data;
        if(WSAStartup(MAKEWORD(2,2),&data) != 0)
            {
            return;
            }

        m_sock = WSASocket(PF_INET,SOCK_STREAM,0, nullptr,0,WSA_FLAG_OVERLAPPED);
        int opt = 1;
        setsockopt(m_sock,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&opt),sizeof(opt));
        }

    // IOCP 线程
    int ThreadIocp();
private:
    ThreadPool                  m_pool;
    HANDLE                      m_hIocp;
    SOCKET                      m_sock;
    sockaddr_in                 m_addr;
    std::map<SOCKET, Client*>   m_client;
};


#endif //IOCPANDTHREADPOOL_SERVER_H
