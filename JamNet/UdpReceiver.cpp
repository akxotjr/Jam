#include "pch.h"
#include "UdpReceiver.h"

namespace jam::net
{
    UdpReceiver::UdpReceiver() : _recvBuffer(BUFFER_SIZE)
    {
        memset(&_remoteAddr, 0, sizeof(_remoteAddr));
    }

    bool UdpReceiver::Start(ServiceRef service)
    {
        _service = service;
        if (_service.lock() == nullptr)
            return false;

        _socket = SocketUtils::CreateSocket(ProtocolType::PROTOCOL_UDP);
        if (_socket == INVALID_SOCKET)
            return false;

        if (_service.lock()->GetIocpCore()->Register(shared_from_this()) == false)
            return false;

        if (SocketUtils::SetReuseAddress(_socket, true) == false)
            return false;

        if (SocketUtils::Bind(_socket, _service.lock()->GetUdpNetAddress()) == false)
            return false;

        RegisterRecv();


        return true;
    }

    HANDLE UdpReceiver::GetHandle()
    {
        return reinterpret_cast<HANDLE>(_socket);
    }

    void UdpReceiver::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
    {
        if (numOfBytes == 0)
            return;

        if (iocpEvent->eventType != EventType::Recv)
            return;

        NetAddress from(_remoteAddr);
        auto service = _service.lock();
        if (service == nullptr)
            return;

        auto session = service->FindOrCreateUdpSession(from);
        ProcessRecv(numOfBytes, session);
    }

    void UdpReceiver::RegisterRecv()
    {
        int32 fromLen = sizeof(_remoteAddr);

        _recvEvent.Init();
        _recvEvent.owner = shared_from_this();

        WSABUF wsaBuf = {};
        wsaBuf.len = _recvBuffer.FreeSize();

        wsaBuf.buf = reinterpret_cast<CHAR*>(_recvBuffer.WritePos());

        DWORD numOfBytes = 0;
        DWORD flags = 0;

        if (SOCKET_ERROR == ::WSARecvFrom(_socket, &wsaBuf, 1, OUT & numOfBytes, OUT & flags, reinterpret_cast<SOCKADDR*>(&_remoteAddr), OUT & fromLen, &_recvEvent, nullptr))
        {
            const int errorCode = ::WSAGetLastError();
            if (errorCode != WSA_IO_PENDING)
            {
                std::cout << "[UDP Receiver] RecvFrom failed : " << errorCode << std::endl;
            }
        }
    }

    bool UdpReceiver::ProcessRecv(int32 numOfBytes, UdpSessionRef session)
    {

        if (!session)
        {
            std::cout << "[ProcessRecv] session is nullptr or expired!\n";
            return false;
        }

        if (_recvBuffer.OnWrite(numOfBytes) == false)
        {
            std::cout << "[ProcessRecv] OnWrite failed! FreeSize: " << _recvBuffer.FreeSize() << ", numOfBytes: " << numOfBytes << "\n";
            return false;
        }

        BYTE* buf = _recvBuffer.ReadPos();
        if (!buf)
        {
            std::cout << "[ProcessRecv] buffer is null\n";
            return false;
        }

        int32 dataSize = _recvBuffer.DataSize();
        int32 processLen = IsParsingPacket(buf, dataSize, session);

        if (processLen < 0 || dataSize < processLen || _recvBuffer.OnRead(processLen) == false)
        {
            std::cout << "[ProcessRecv] Invalid processLen: " << processLen << ", dataSize: " << dataSize << "\n";
            return false;
        }

        _recvBuffer.Clean();
        RegisterRecv();
        return true;
    }

    int32 UdpReceiver::IsParsingPacket(BYTE* buffer, const int32 len, UdpSessionRef session)
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
