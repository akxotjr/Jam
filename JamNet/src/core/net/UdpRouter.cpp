#include "pch.h"
#include "jamnet/core/net/UdpRouter.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/SocketUtils.h"

#pragma warning(disable : 4996)

namespace jam::net
{
    bool UdpRouter::Start(Service* service)
	{
        m_service = service;
        if (!m_service) return false;

        m_socket = SocketUtils::CreateSocket(eProtocolType::UDP);
        if (m_socket == INVALID_SOCKET)
            return false;

        if (m_service->GetIocpCore()->Register(shared_from_this()) == false)
            return false;

        if (SocketUtils::SetReuseAddress(m_socket, true) == false)
            return false;

        if (m_service->GetServiceType() == eServiceType::CLIENT)
        {
            if (SocketUtils::BindAnyAddress(m_socket, 0) == false)
                return false;
        }
        else if (m_service->GetServiceType() == eServiceType::SERVER)
        {
            if (SocketUtils::Bind(m_socket, m_service->GetLocalUdpNetAddress()) == false)
                return false;
        }

        for (int8 i = 0; i < OUTSTANDING_RECVS; ++i)
        {
            RegisterRecv();
        }

        return true;
	}

    void UdpRouter::CloseSocket()
    {
        SocketUtils::Close(m_socket);
    }


    HANDLE UdpRouter::GetHandle()
    {
        return reinterpret_cast<HANDLE>(m_socket);
    }

    void UdpRouter::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
    {
        switch (iocpEvent->m_eventType)
        {
        case eEventType::RECV:
        {
            auto* ev = static_cast<RecvEvent*>(iocpEvent);

            if (ev->recvBuffer != nullptr)
            {
                if (numOfBytes > 0)
					ProcessRecv(numOfBytes, ev->remoteAddress, *ev->recvBuffer);
                ObjectPool<jam::net::RecvBuffer>::Push(ev->recvBuffer);
                ev->recvBuffer = nullptr;
            }
            ObjectPool<net::RecvEvent>::Push(ev);

            RegisterRecv();
            break;
        }
        case eEventType::SEND:
        {
			auto* sendEvent = static_cast<SendEvent*>(iocpEvent);

        	if (numOfBytes > 0)
        		ProcessSend(numOfBytes, sendEvent->remoteAddress);
            sendEvent->sendBuffers.clear();
			ObjectPool<net::SendEvent>::Push(sendEvent);
			break;
        }
        default:
            break;
        }
    }


    void UdpRouter::RegisterSend(const std::vector<std::shared_ptr<SendBuffer>>& bufs, const NetAddress& to)
    {
        for (auto& buf : bufs)
        {
            if (!buf || !buf->Buffer()) continue;

            auto* ev = ObjectPool<SendEvent>::Pop();
            ev->Init();
            ev->m_owner         = shared_from_this();
            ev->remoteAddress   = to;

            ev->use_gather      = false;                // 단일 datagram
            ev->sendBuffers.clear();
            ev->sendBuffers.push_back(buf);        // 생존 보장용
            ev->single.buf      = reinterpret_cast<char*>(buf->Buffer());
            ev->single.len      = static_cast<ULONG>(buf->WriteSize());

            DWORD sent = 0;
            if (SOCKET_ERROR == ::WSASendTo(m_socket, &ev->single, 1, OUT &sent, 0, reinterpret_cast<const SOCKADDR*>(&ev->remoteAddress.GetSockAddr()), sizeof(SOCKADDR_IN), ev, nullptr))
            {
                const int32 ec = ::WSAGetLastError();
                if (ec != WSA_IO_PENDING)
                {
                    HandleError(ec);
                    ObjectPool<SendEvent>::Push(ev);
                }
            }
        }
    }

    void UdpRouter::RegisterRecv()
    {
        auto* ev  = ObjectPool<RecvEvent>::Pop();
        auto* buf = ObjectPool<RecvBuffer>::Pop();
        buf->Init(1500, 1);

        ev->Init();
        ev->m_owner     = shared_from_this();
        ev->fromLen     = sizeof(SOCKADDR_IN);
        ev->recvBuffer  = buf;

        WSABUF wsaBuf;
        wsaBuf.len = buf->FreeSize();
        wsaBuf.buf = reinterpret_cast<CHAR*>(buf->WritePos());

        DWORD numOfBytes = 0;
        DWORD flags = 0;

        if (SOCKET_ERROR == ::WSARecvFrom(m_socket, &wsaBuf, 1, OUT &numOfBytes, OUT &flags, reinterpret_cast<SOCKADDR*>(&ev->remoteAddress.GetSockAddr()), OUT &ev->fromLen, ev, nullptr))
        {
            const int32 errorCode = ::WSAGetLastError();
            if (errorCode != WSA_IO_PENDING)
            {
                HandleError(errorCode);

                ObjectPool<RecvBuffer>::Push(buf);
                ObjectPool<RecvEvent>::Push(ev);
            }
        }
    }

    void UdpRouter::ProcessSend(int32 numOfBytes, const NetAddress& remoteAddress)
    {
        if (numOfBytes == 0 || !m_service) return;

        auto udpSession = m_service->FindSessionInConnected(remoteAddress);
        if (udpSession == nullptr)
            return;

        udpSession->OnSend(numOfBytes);
    }

    void UdpRouter::ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddress, RecvBuffer& buf)
    {
        if (numOfBytes == 0 || !m_service) return;

        m_service->ProcessUdpSession(remoteAddress, numOfBytes, buf);
    }

    void UdpRouter::HandleError(int32 errorCode)
    {
        switch (errorCode)
        {
        case WSAECONNRESET:
        case WSAECONNABORTED:
            break;
        default:
	        std::cout << "Handle Error : " << errorCode << '\n';
            break; 
        }
    }
}
