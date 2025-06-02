#include "pch.h"
#include "UdpReceiver.h"

namespace jam::net
{
    UdpReceiver::UdpReceiver() : m_recvBuffer(BUFFER_SIZE)
    {
        memset(&m_remoteAddr, 0, sizeof(m_remoteAddr));
    }

    bool UdpReceiver::Start(Sptr<Service> service)
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

        RegisterRecv();

        return true;
    }

    HANDLE UdpReceiver::GetHandle()
    {
        return reinterpret_cast<HANDLE>(m_socket);
    }

    void UdpReceiver::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
    {
        if (numOfBytes == 0)
            return;

        if (iocpEvent->m_eventType != EventType::Recv)
            return;

        NetAddress from(m_remoteAddr);
        auto service = m_service.lock();
        if (service == nullptr)
            return;

        //auto session = service->FindOrCreateUdpSession(from);
        //ProcessRecv(numOfBytes, session);

    }

    void UdpReceiver::RegisterRecv()
    {
        int32 fromLen = sizeof(m_remoteAddr);

        m_recvEvent.Init();
        m_recvEvent.m_owner = shared_from_this();

        WSABUF wsaBuf = {};
        wsaBuf.len = m_recvBuffer.FreeSize();

        wsaBuf.buf = reinterpret_cast<CHAR*>(m_recvBuffer.WritePos());

        DWORD numOfBytes = 0;
        DWORD flags = 0;

        if (SOCKET_ERROR == ::WSARecvFrom(m_socket, &wsaBuf, 1, OUT & numOfBytes, OUT & flags, reinterpret_cast<SOCKADDR*>(&m_remoteAddr), OUT & fromLen, &m_recvEvent, nullptr))
        {
            const int errorCode = ::WSAGetLastError();
            if (errorCode != WSA_IO_PENDING)
            {
                std::cout << "[UDP Receiver] RecvFrom failed : " << errorCode << std::endl;
            }
        }
    }

    bool UdpReceiver::ProcessRecv(int32 numOfBytes, Sptr<UdpSession> session)
    {
        if (!session)
        {
            std::cout << "[ProcessRecv] session is nullptr or expired!\n";
            return false;
        }

        if (m_recvBuffer.OnWrite(numOfBytes) == false)
        {
            std::cout << "[ProcessRecv] OnWrite failed! FreeSize: " << m_recvBuffer.FreeSize() << ", numOfBytes: " << numOfBytes << "\n";
            return false;
        }

        BYTE* buf = m_recvBuffer.ReadPos();
        if (!buf)
        {
            std::cout << "[ProcessRecv] buffer is null\n";
            return false;
        }

        int32 dataSize = m_recvBuffer.DataSize();
        int32 processLen = IsParsingPacket(buf, dataSize, session);

        if (processLen < 0 || dataSize < processLen || m_recvBuffer.OnRead(processLen) == false)
        {
            std::cout << "[ProcessRecv] Invalid processLen: " << processLen << ", dataSize: " << dataSize << "\n";
            return false;
        }

        m_recvBuffer.Clean();
        RegisterRecv();
        return true;
    }

    int32 UdpReceiver::IsParsingPacket(BYTE* buffer, const int32 len, Sptr<UdpSession> session)
    {
        int32 processLen = 0;

        while (true)
        {
            int32 dataSize = len - processLen;

            if (dataSize < sizeof(UdpPacketHeader))
                break;

            UdpPacketHeader header = *reinterpret_cast<UdpPacketHeader*>(&buffer[processLen]);

            if (dataSize < header.size || header.size < sizeof(UdpPacketHeader))
                break;

            if (processLen + header.size > len)
                break;

            auto baseSession = static_pointer_cast<Session>(session);
            OnRecv(baseSession, &buffer[0], header.size);

            processLen += header.size;
        }

        return processLen;
    }
}
