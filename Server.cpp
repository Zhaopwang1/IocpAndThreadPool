#include "Server.h"

Client::Client() \
    : m_isBusy(false), \
      m_dwFlags(0), \
      m_ptrOverlapped(new ACCEPTOVERLAPPED), \
      m_ptrRecv(new RECVOVERLAPPED), \
      m_ptrSend(new SENDOVERLAPPED), \
      m_vecSend(this,reinterpret_cast<SEND_CALLBACK>(&Client::SendData)) \
    {
    printf("m_ptrOverlapped %08X\r\n",&m_ptrOverlapped);

    m_sock = WSASocket(PF_INET,SOCK_STREAM,0, nullptr,0,WSA_FLAG_OVERLAPPED);
    m_buffer.resize(1024);
    memset(&m_laddr,0,sizeof(m_laddr));
    memset(&m_raddr,0,sizeof(m_raddr));
    }


Client::~Client()
    {
    m_buffer.clear();
    closesocket(m_sock);
    }


// 接收
int Client::Recv()
    {
    int ret = recv(m_sock,m_buffer.data() + m_usedBuf,m_buffer.size() - m_usedBuf,0);
    if(ret <= 0)
        {
        return -1;
        }
    m_usedBuf += static_cast<size_t>(ret);

    // TODO: 解析数据
    Tools::Dump(reinterpret_cast<BYTE*>(m_buffer.data()),ret);

    return 0;
    }


// 发送
int Client::Send(void* buffer, size_t size)
    {
    std::vector<char> data(size);
    memcpy(data.data(),buffer,size);
    if(m_vecSend.PushBack(data))
        {
        return 0;
        }
    }


// 发送数据
int Client::SendData(std::vector<char>& data)
    {
    if(m_vecSend.Size() > 0)
        {
        int ret = WSASend(m_sock,SendWSABuffer(),1,&m_dwReceived,m_dwFlags,&m_ptrSend->m_overlapped, nullptr);
        if(ret != 0 && WSAGetLastError() != WSA_IO_PENDING)
            {
            std::cerr << "WSASend failed! [" << WSAGetLastError() \
                      << "] (" << Tools::GetErrorInfo(WSAGetLastError()).c_str() \
                      << ")" << std::endl;

            return ret;
            }
        }
    return 0;
    }


Client::operator LPOVERLAPPED() { return &m_ptrOverlapped->m_overlapped; }


LPWSABUF Client::RecvWSABuffer() { return &m_ptrRecv->m_wsaBuffer; }


LPOVERLAPPED Client::RecvOverlapped() { return &m_ptrRecv->m_overlapped; }


LPWSABUF Client::SendWSABuffer() { return &m_ptrSend->m_wsaBuffer; }


LPOVERLAPPED Client::SendOverlapped() { return &m_ptrSend->m_overlapped; }



template<IoOperator _Op> \
AcceptOverlapped<_Op>::AcceptOverlapped()
    {
    m_worker = ThreadWorker(this, reinterpret_cast<FUNCTYPE>(&AcceptOverlapped<_Op>::AcceptWorker));
    m_operator = IOAccept;
    memset(&m_overlapped,0,sizeof(m_overlapped));
    m_buffer.resize(1024);
    m_server = nullptr;
    }


template<IoOperator _Op> \
int AcceptOverlapped<_Op>::AcceptWorker()
    {
    printf("AcceptWorker this %08X\r\n", this);

    INT lLength = 0, rLength = 0;
    if(m_client->GetBufferSize() > 0)
        {
        // 本地地址，远程地址
        LPSOCKADDR pLocalAddr, pRemoteAddr;
        GetAcceptExSockaddrs(*m_client, \
            0, \
            sizeof(sockaddr_in) + 16, \
            sizeof(sockaddr_in) + 16, \
            reinterpret_cast<sockaddr**>(&pLocalAddr),/*本地地址*/ \
            &lLength, \
            reinterpret_cast<sockaddr**>(&pRemoteAddr),/*远程地址*/ \
            &rLength);

        memcpy(m_client->GetLocalAddr(),pLocalAddr,sizeof(sockaddr_in));
        memcpy(m_client->GetRemoteAddr(),pRemoteAddr,sizeof(sockaddr_in));

        m_server->BindNewSocket(*m_client,reinterpret_cast<ULONG_PTR>(m_client));

        int ret = WSARecv(static_cast<SOCKET>(*m_client), \
            m_client->RecvWSABuffer(), \
            1, \
            *m_client, \
            &m_client->GetFlags(), \
            m_client->RecvOverlapped(), \
            nullptr);

        if (SOCKET_ERROR == ret && (WSAGetLastError() != WSA_IO_PENDING))
            {
            // TODO: 报错
            std::cerr << "WSARecv failed! [" << WSAGetLastError() \
                      << "] (" << Tools::GetErrorInfo(WSAGetLastError()).c_str() \
                      << ")" << std::endl;
            }
        if (!m_server->NewAccept())
            {
            return -2;
            }
        }
    return -1;  // 必须返回 -1，否则循环不会终止！！！
    }


template<IoOperator _Op> \
RecvOverlapped<_Op>::RecvOverlapped()
    {
    m_operator = _Op;
    m_worker = ThreadWorker(this,reinterpret_cast<FUNCTYPE>(&RecvOverlapped<_Op>::RecvWorker));
    memset(&m_overlapped,0,sizeof(m_overlapped));
    m_buffer.resize(1024 * 256);
    }



template<IoOperator _Op> \
SendOverlapped<_Op>::SendOverlapped()
    {
    m_operator = _Op;
    m_worker = ThreadWorker(this,reinterpret_cast<FUNCTYPE>(&SendOverlapped<_Op>::SendWorker));
    memset(&m_overlapped,0,sizeof(m_overlapped));
    m_buffer.resize(1024 * 256);
    }



template<IoOperator _Op> \
ErrorOverlapped<_Op>::ErrorOverlapped()
    {
    m_operator = _Op;
    m_worker(this,&ErrorOverlapped::ErrorWorker);
    memset(&m_overlapped,0,sizeof(m_overlapped));
    m_buffer.resize(1024);
    }



Server::~Server()
    {
    std::map<SOCKET,Client*>::iterator it = m_client.begin();
    for(; it != m_client.end(); ++it)
        {
        delete it->second;
        it->second = nullptr;
        }
    m_client.clear();
    }



// IOCP 流程函数，绑定 IOCP 并启动线程池
bool Server::StartServer()
    {
    // 创建套接字
    CreateSocket();

    // 绑定
    if(-1 == bind(m_sock,reinterpret_cast<sockaddr*>(&m_addr),sizeof(m_addr)))
        {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
        }

    // 监听
    if(-1 == listen(m_sock, 5))
        {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return false;
        }

    // 创建 IOCP
    m_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,nullptr,0,4);
    if(!m_hIocp)
        {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        m_hIocp = INVALID_HANDLE_VALUE;
        return false;
        }

    // 将 IOCP 和套接字绑定在一起
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_sock),m_hIocp,reinterpret_cast<ULONG_PTR>(this),0);

    // 启动线程池
    m_pool.Invoke();

    // 分发线程工作
    m_pool.DispatchWorker(ThreadWorker(this,reinterpret_cast<FUNCTYPE>(&Server::ThreadIocp)));

    // 创建新连接
    if(!NewAccept())
        {
        return false;
        }

    return true;
    }



// 绑定新套接字
void Server::BindNewSocket(SOCKET s, ULONG_PTR ulKey)
    {
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(s),m_hIocp,ulKey,0);
    }



// IOCP 线程
int Server::ThreadIocp()
    {
    DWORD dwTranferred = 0;
    ULONG_PTR ulCompletionKey = 0;
    OVERLAPPED* lpOverlapped = nullptr;
    if(GetQueuedCompletionStatus(m_hIocp,&dwTranferred,&ulCompletionKey,&lpOverlapped,INFINITE))
        {
        if(ulCompletionKey)
            {
            IoOverlapped* pOver = CONTAINING_RECORD(lpOverlapped,IoOverlapped,m_overlapped);
            pOver->m_server = this;
            std::cout << "Operator is " << pOver->m_operator << std::endl;

            switch(pOver->m_operator)
                {
            case IOAccept:
                {
                ACCEPTOVERLAPPED* pAcceptOver = reinterpret_cast<ACCEPTOVERLAPPED*>(pOver);
                printf("pAcceptOver %08X\r\n",pAcceptOver);
                m_pool.DispatchWorker(pAcceptOver->m_worker);
                }
            break;
            case IORecv:
                {
                RECVOVERLAPPED* pRecvOver = reinterpret_cast<RECVOVERLAPPED*>(pOver);
                m_pool.DispatchWorker(pRecvOver->m_worker);
                }
            break;
            case IOSend:
                {
                SENDOVERLAPPED* pSendOver = reinterpret_cast<SENDOVERLAPPED*>(pOver);
                m_pool.DispatchWorker(pSendOver->m_worker);
                }
            break;
            case IOError:
                {
                ERROROVERLAPPED* peErrOver = reinterpret_cast<ERROROVERLAPPED*>(pOver);
                m_pool.DispatchWorker(peErrOver->m_worker);
                }
            break;
                }
            }
        else
            {
            return -1;
            }
        }
    return 0;
    }
