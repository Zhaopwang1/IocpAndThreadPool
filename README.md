# IocpAndThreadPool

## 1. How to use IOCP?

1. Use OVERLAPPED to create a socket
2. Use the address returned by the socket and pass in CreateIoCompletionPort() to create the completion port.
3. Bind the socket and the created completion port into CreateIoCompletionPort()
4. Bind IP port
5. Perform bind and listen
6. Call AcceptEx() and pass in the server socket and client socket. The client socket must be created before use. (asynchronous, will return soon)
7. You can use WSASend() and WSARecv() (asynchronous)
8. In the thread of the completion port, use while(true) to continuously call GetQueuedCompletionStatus() to obtain the I/O operation results, and the results will be placed in OVERLAPPED
9. Throw it into the queue according to the status returned in OVERLAPPED. This thread is only used for IOCP and does not handle specific business logic. For specific business logic processing, use the thread pool to create other threads or other devices for processing

