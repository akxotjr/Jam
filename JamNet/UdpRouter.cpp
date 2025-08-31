#include "pch.h"
#include "UdpRouter.h"

#pragma warning(disable : 4996)

namespace jam::net
{
    UdpRouter::UdpRouter() : m_recvBuffer(BUFFER_SIZE)
    {
    }

    UdpRouter::~UdpRouter()
    {
    }

    bool UdpRouter::Start(Sptr<Service> service)
	{
        m_service = service;
        if (m_service.lock() == nullptr)
            return false;

        auto ser = m_service.lock();
        if (ser == nullptr) return false;

        m_socket = SocketUtils::CreateSocket(eProtocolType::UDP);
        if (m_socket == INVALID_SOCKET)
            return false;

        if (ser->GetIocpCore()->Register(shared_from_this()) == false)
            return false;

        if (SocketUtils::SetReuseAddress(m_socket, true) == false)
            return false;

        if (ser->m_peer == ePeerType::Client)
        {
            if (SocketUtils::BindAnyAddress(m_socket, 0) == false)
                return false;
        }
        else if (ser->m_peer == ePeerType::Server)
        {
            if (SocketUtils::Bind(m_socket, ser->GetLocalUdpNetAddress()) == false)
                return false;
        }

        //m_sendEvent.Init();
        //m_sendEvent.m_owner = shared_from_this();

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

        switch (iocpEvent->m_eventType)
        {
        case eEventType::Recv:
        {
            RecvEvent* recvEvent = static_cast<RecvEvent*>(iocpEvent);
            ProcessRecv(numOfBytes, recvEvent->remoteAddress);
            break;
        }
        case eEventType::Send:
        {
            //SendEvent* sendEvent = static_cast<SendEvent*>(iocpEvent);
            //ProcessSend(numOfBytes, sendEvent->remoteAddress);


            // 매 전송마다 새 이벤트를 썼으니 여기서 정리
            //auto* sendEvent = static_cast<SendEvent*>(iocpEvent);
            auto* sendEvent = utils::memory::ObjectPool<SendEvent>().Pop();
            ProcessSend(numOfBytes, sendEvent->remoteAddress);
            // sendEvent->sendBuffers 가 스코프를 떠나며 버퍼 생존 보장 끝
            /*delete sendEvent;*/
            utils::memory::ObjectPool<SendEvent>().Push(sendEvent);
            break;
        }
        default:
            break;
        }
    }

    void UdpRouter::RegisterSend(Sptr<SendBuffer> buf, const NetAddress& to)
    {
    //    WSABUF wsaBuf;
    //    wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
    //    wsaBuf.len = static_cast<ULONG>(sendBuffer->WriteSize());

    //    DWORD numOfBytes = 0;

    //    m_sendEvent.remoteAddress = remoteAddress;

    //    if (SOCKET_ERROR == ::WSASendTo(m_socket, &wsaBuf, 1, OUT &numOfBytes, 0, reinterpret_cast<SOCKADDR*>(&m_sendEvent.remoteAddress), sizeof(SOCKADDR_IN), &m_sendEvent, nullptr))
    //    {
    //        const int32 errorCode = ::WSAGetLastError();
    //        if (errorCode != WSA_IO_PENDING)
    //        {
				//HandleError(errorCode);
    //            m_sendEvent.sendBuffers.clear();
    //        }
    //    }

            // ★ 전송마다 독립 SendEvent 생성 → thread-safe
        //auto* ev = new SendEvent();

        auto* ev = ObjectPool<SendEvent>().Pop();
        ev->Init();
        ev->m_owner = shared_from_this();
        ev->remoteAddress = to;

        ev->use_gather = false;
        ev->sendBuffers.push_back(buf);

        ev->single.buf = reinterpret_cast<char*>(buf->Buffer());
        ev->single.len = static_cast<ULONG>(buf->WriteSize());

        DWORD sent = 0;
        int rc = ::WSASendTo(
            m_socket,
            &ev->single, 1,           // ★ 단일 WSABUF
            OUT & sent,
            0,
            reinterpret_cast<const SOCKADDR*>(&ev->remoteAddress),
            sizeof(SOCKADDR_IN),
            ev,
            nullptr);

        if (rc == SOCKET_ERROR) {
            const int err = ::WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                HandleError(err);
                delete ev;
            }
        }
    }

    void UdpRouter::RegisterSendMany(const xvector<Sptr<SendBuffer>>& bufs, const NetAddress& to)
    {
        //auto* ev = new SendEvent();

        auto* ev = ObjectPool<SendEvent>().Pop();
        ev->Init();
        ev->m_owner = shared_from_this();
        ev->remoteAddress = to;

        ev->use_gather = true;
        ev->sendBuffers = bufs;                  // 생존 보장
        ev->gather.clear();
        ev->gather.reserve(bufs.size());         // 작은 최적화

        for (auto& sb : bufs) {
            WSABUF w;
            w.buf = reinterpret_cast<char*>(sb->Buffer());
            w.len = static_cast<ULONG>(sb->WriteSize());
            ev->gather.push_back(w);
        }

        DWORD sent = 0;
        int rc = ::WSASendTo(
            m_socket,
            ev->gather.data(), static_cast<DWORD>(ev->gather.size()),
            OUT & sent,
            0,
            reinterpret_cast<const SOCKADDR*>(&ev->remoteAddress),
            sizeof(SOCKADDR_IN),
            ev,
            nullptr);

        if (rc == SOCKET_ERROR) {
            const int err = ::WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                HandleError(err);
                delete ev;
            }
        }
    }

    void UdpRouter::RegisterRecv()
    {
        int32 fromLen = sizeof(SOCKADDR_IN);

        WSABUF wsaBuf;
        wsaBuf.len = m_recvBuffer.FreeSize();
        wsaBuf.buf = reinterpret_cast<CHAR*>(m_recvBuffer.WritePos());

        DWORD numOfBytes = 0;
        DWORD flags = 0;

        if (SOCKET_ERROR == ::WSARecvFrom(m_socket, &wsaBuf, 1, OUT &numOfBytes, OUT &flags, reinterpret_cast<SOCKADDR*>(&m_recvEvent.remoteAddress.GetSockAddr()), OUT& fromLen, &m_recvEvent, nullptr))
        {
            const int errorCode = ::WSAGetLastError();
            if (errorCode != WSA_IO_PENDING)
            {
                HandleError(errorCode);
            }
        }
    }

    void UdpRouter::ProcessSend(int32 numOfBytes, const NetAddress& remoteAddress)
    {
        if (numOfBytes == 0)    
            return;

        auto udpSession = m_service.lock()->FindSessionInConnected(remoteAddress);
        if (udpSession == nullptr)
            return;

        udpSession->OnSend(numOfBytes);
    }

    void UdpRouter::ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddress)
    {
        if (numOfBytes == 0) return;

        // 기존: NetAddress from(m_remoteSockAddr);  // 불필요/오류 가능성
        m_service.lock()->ProcessUdpSession(remoteAddress, numOfBytes, m_recvBuffer);
        RegisterRecv();
    }

    void UdpRouter::HandleError(int32 errorCode)
    {
        switch (errorCode)
        {
        case WSAECONNRESET:
        case WSAECONNABORTED:
            break;
        default:
            cout << "Handle Error : " << errorCode << '\n';
            break;
        }
    }
}
