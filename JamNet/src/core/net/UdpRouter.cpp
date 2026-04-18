#include "pch.h"
#include "jamnet/core/net/UdpRouter.h"

#include "jamnet/core/memory/ObjectPool.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/UdpSession.h"
#include "jamnet/core/net/SocketUtils.h"
#include "jamnet/core/utils/Clock.h"

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
        case eEventType::UdpRecv:
        {
            auto* ev = static_cast<UdpRecvEvent*>(iocpEvent);
            if (ev->packet.IsValid() && numOfBytes > 0)
            {
                auto& s = ev->packet.Get();
                if (s.TryAppendPayload(static_cast<uint32>(numOfBytes)))
                {
                    s.CloseWithCommit(static_cast<uint32>(numOfBytes));
                    const uint64 ingress = NOW_NS();
                    ProcessRecv(numOfBytes, ev->remoteAddr, ev->packet, ingress);
                }
            }

            ev->packet.Reset();
            ObjectPool<UdpRecvEvent>::Push(ev);
            RegisterRecv();
            break;
        }
        case eEventType::UdpSend:
        {
			auto* ev = static_cast<UdpSendEvent*>(iocpEvent);

            if (numOfBytes > 0)
                ProcessSend(numOfBytes, ev->remoteAddr);
            ev->chain.Clear();
            ev->wsaBufs.clear();
			ObjectPool<UdpSendEvent>::Push(ev);
			break;
        }
        default:
            break;
        }
    }


    void UdpRouter::RegisterSend(std::vector<PacketChain>&& chains, const NetAddress& to)
    {
        for (auto& chain : chains)
        {
            if (chain.Empty()) continue;

            if (chain.Count() == 1)
            {
                const BufferSlice& first = chain[0];
                PacketHeaderView v = PacketHeaderView::Parse(first.Head(), first.Size());
                if (v.IsValid() && v.Type() == ePacketType::SYSTEM)
                {
                    const uint64 wireNow_ns = NOW_NS();

                    if (U2E(eSystemPacketId, v.Id()) == eSystemPacketId::PING && v.PayloadSize() >= sizeof(PING_DATA))
                    {
                        auto* ping = reinterpret_cast<PING_DATA*>(v.Payload());
                        ping->t1Wire_ns = wireNow_ns;
                    }
                    else if (U2E(eSystemPacketId, v.Id()) == eSystemPacketId::PONG && v.PayloadSize() >= sizeof(PONG_DATA))
                    {
                        auto* pong = reinterpret_cast<PONG_DATA*>(v.Payload());
                        pong->t3Wire_ns = wireNow_ns;
                    }
                }
            }

            auto* ev = ObjectPool<UdpSendEvent>::Pop();
            ev->Init();
            ev->m_owner    = shared_from_this();
            ev->remoteAddr = to;
            ev->chain      = std::move(chain);
            ev->wsaBufs.clear();
            ev->wsaBufs.reserve(ev->chain.Count());

            for (const BufferSlice& part : ev->chain.Parts())
            {
                if (!part.IsValid() || part.Size() == 0)
                    continue;

                WSABUF w;
                w.buf = reinterpret_cast<char*>(part.Head());
                w.len = static_cast<ULONG>(part.Size());
                ev->wsaBufs.push_back(w);
            }

            if (ev->wsaBufs.empty())
            {
                ev->chain.Clear();
                ObjectPool<UdpSendEvent>::Push(ev);
                continue;
            }

            if (SOCKET_ERROR == ::WSASendTo(m_socket, ev->wsaBufs.data(), static_cast<DWORD>(ev->wsaBufs.size()), nullptr, 0, ev->remoteAddr.GetSockAddrPtr(), sizeof(SOCKADDR_IN), ev, nullptr))
            {
                const int32 ec = ::WSAGetLastError();
                if (ec != WSA_IO_PENDING)
                {
                    HandleError(ec);
                    ev->chain.Clear();
                    ev->wsaBufs.clear();
                    ObjectPool<UdpSendEvent>::Push(ev);
                }
            }
        }
    }

    void UdpRouter::RegisterRecv()
    {
        eNetBufferPoolKind kind = m_service->GetServiceType() == eServiceType::CLIENT ?
            eNetBufferPoolKind::UdpClientIo : eNetBufferPoolKind::UdpServerIo;

        BufWriter writer(GetNetBufferPool(kind));
        BufferSlice slice = writer.OpenForPayload(JAMNET_MTU, alignof(PacketHeader));

        auto* ev  = ObjectPool<UdpRecvEvent>::Pop();
        ev->Init();
        ev->m_owner         = shared_from_this();
        ev->packet          = MakeOwned(slice);
        ev->wsaBuf.buf      = reinterpret_cast<char*>(ev->packet->Head());
        ev->wsaBuf.len      = ev->packet->Capacity();
        ev->remoteAddrLen   = sizeof(SOCKADDR_IN);


        if (SOCKET_ERROR == ::WSARecvFrom(m_socket, &ev->wsaBuf, 1, nullptr, &ev->flags, ev->remoteAddr.GetSockAddrPtr(), OUT &ev->remoteAddrLen, ev, nullptr))
        {
            const int32 error = ::WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                HandleError(error);
                ev->packet.Reset();
                ObjectPool<UdpRecvEvent>::Push(ev);
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

    void UdpRouter::ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddress, Packet packet, uint64 ingressRecvTime_ns)
    {
        if (numOfBytes == 0 || !m_service) return;

        m_service->ProcessUdpSession(remoteAddress, numOfBytes, packet, ingressRecvTime_ns);
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
