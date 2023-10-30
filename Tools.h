#ifndef IOCPANDTHREADPOOL_TOOLS_H
#define IOCPANDTHREADPOOL_TOOLS_H


#include <Windows.h>


#include <iostream>
#include <string>


class Tools
{
public:
    Tools() = delete;
    ~Tools() = delete;
public:
    // dump 内存
    static void Dump(BYTE* pData, size_t size)
        {
        std::string out;
        for(size_t i = 0; i != size; ++i)
            {
            char buf[8] = "";
            if(i > 0 && (0 == i%16))
                {
                out += "\n";
                snprintf(buf,sizeof(buf),"%02X",pData[i] & 0xFF);
                out += buf;
                }
            }
        out += "\n";
        std::cout << out.c_str() << std::endl;
        }

    // 获取错误信息
    static std::string GetErrorInfo(int wsaErrCode)
        {
        std::string ret;
        LPVOID lpMsgBuf = nullptr;
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, \
            nullptr, \
            wsaErrCode, \
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), \
            reinterpret_cast<LPTSTR>(lpMsgBuf), \
            0, \
            nullptr);

        ret = reinterpret_cast<char*>(lpMsgBuf);
        LocalFree(lpMsgBuf);

        return ret;
        }


};


#endif //IOCPANDTHREADPOOL_TOOLS_H
