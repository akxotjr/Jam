#include "pch.h"
#include "UdpRouter.h"

namespace jam::net
{
	bool UdpRouter::Start(Sptr<Service> service)
	{
        m_service = service;
        if (m_service.lock() == nullptr)
            return false;

        m_socket = SocketUtils::CreateSocket(EProtocolType::UDP);
        if (m_socket == INVALID_SOCKET)
            return false;

        if (m_service.lock()->GetIocpCore()->Register(shared_from_this()) == false)
            return false;

        if (SocketUtils::SetReuseAddress(m_socket, true) == false)
            return false;

        if (SocketUtils::Bind(m_socket, m_service.lock()->GetLocalUdpNetAddress()) == false)
            return false;

        m_sendEvent.Init();
        m_sendEvent.m_owner = shared_from_this();

        m_recvEvent.Init();
        m_recvEvent.m_owner = shared_from_this();

        RegisterRecv();

        return true;
	}


    HANDLE UdpRouter::GetHandle()
    {
        return reinterpret_cast<HANDLE>(m_socket);
    }

    void UdpRouter::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
    {
        if (numOfBytes == 0)
            return;

        if (iocpEvent->m_eventType == EventType::Recv)
        {
            ProcessRecv(numOfBytes);
        }
        else if (iocpEvent->m_eventType == EventType::Send)
        {
            ProcessSend(numOfBytes);
        }
    }

    void UdpRouter::RegisterSend(Sptr<SendBuffer> sendBuffer, const NetAddress& remoteAddress)
    {
        WSABUF wsaBuf;
        wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
        wsaBuf.len = static_cast<ULONG>(sendBuffer->WriteSize());

        DWORD numOfBytes = 0;
        SOCKADDR_IN remoteAddr = remoteAddress.GetSockAddr();

        if (SOCKET_ERROR == ::WSASendTo(m_socket, &wsaBuf, 1, OUT &numOfBytes, 0, reinterpret_cast<SOCKADDR*>(&remoteAddr), sizeof(SOCKADDR_IN), &m_sendEvent, nullptr))
        {
            const int32 errorCode = ::WSAGetLastError();
            if (errorCode != WSA_IO_PENDING)
            {
               // HandleError(errorCode);
                m_sendEvent.sendBuffers.clear();
            }
        }
    }

    void UdpRouter::RegisterRecv()
    {
        int32 fromLen = sizeof(m_remoteSockAddr);

        WSABUF wsaBuf = {};
        wsaBuf.len = m_recvBuffer.FreeSize();
        wsaBuf.buf = reinterpret_cast<CHAR*>(m_recvBuffer.WritePos());

        DWORD numOfBytes = 0;
        DWORD flags = 0;

        if (SOCKET_ERROR == ::WSARecvFrom(m_socket, &wsaBuf, 1, OUT &numOfBytes, OUT &flags, reinterpret_cast<SOCKADDR*>(&m_remoteSockAddr), OUT & fromLen, &m_recvEvent, nullptr))
        {
            const int errorCode = ::WSAGetLastError();
            if (errorCode != WSA_IO_PENDING)
            {
                std::cout << "[UDP Receiver] RecvFrom failed : " << errorCode << std::endl;
            }
        }
    }

    void UdpRouter::ProcessSend(int32 numOfBytes)
    {
        if (numOfBytes == 0)    
            return;     // todo

        auto udpSession = m_service.lock()->FindUpdSession(NetAddress(m_remoteSockAddr));
        if (udpSession == nullptr)
            return;

        udpSession->ProcessSend(numOfBytes);
    }

    void UdpRouter::ProcessRecv(int32 numOfBytes)
    {
        NetAddress from(m_remoteSockAddr);

        m_service.lock()->ProcessUdpSession(from, numOfBytes, m_recvBuffer);
    }
}
