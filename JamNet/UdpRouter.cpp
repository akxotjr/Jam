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

        switch (iocpEvent->m_eventType)
        {
        case EventType::Recv:
        {
            RecvEvent* recvEvent = static_cast<RecvEvent*>(iocpEvent);
            ProcessRecv(numOfBytes, recvEvent->remoteAddress);
            break;
        }
        case EventType::Send:
        {
            SendEvent* sendEvent = static_cast<SendEvent*>(iocpEvent);
            ProcessSend(numOfBytes, sendEvent->remoteAddress);
            break;
        }
        default:
            break;
        }
    }

    void UdpRouter::RegisterSend(Sptr<SendBuffer> sendBuffer, const NetAddress& remoteAddress)
    {
        WSABUF wsaBuf;
        wsaBuf.buf = reinterpret_cast<char*>(sendBuffer->Buffer());
        wsaBuf.len = static_cast<ULONG>(sendBuffer->WriteSize());

        //debug
       // cout << "[RegisterSend] to : " << remoteAddress.GetSockAddr().sin_family << " , " << remoteAddress.GetSockAddr().sin_addr.s_addr << ", " << remoteAddress.GetSockAddr().sin_port << endl;

    	//wstring ip = remoteAddress.GetIpAddress();
     //   uint16 port = remoteAddress.GetPort();

        //wcout << L"[UdpRouter::Register] from : " <<  << L" , " << port << endl;
        //wcout << L"[UdpRouter::Register] to : " << ip << L" , " << port << endl;

        DWORD numOfBytes = 0;
       // SOCKADDR_IN remoteAddr = remoteAddress.GetSockAddr();

        m_sendEvent.remoteAddress = remoteAddress;

        //cout << "WSABUF size: " << wsaBuf.len << endl;
        //cout << "WSABUF ptr : " << static_cast<void*>(wsaBuf.buf) << endl;
        //cout << "remoteAddr port: " << ntohs(remoteAddr.sin_port) << endl;
        //cout << "remoteAddr ip: " << inet_ntoa(remoteAddr.sin_addr) << endl;
        //cout << "m_socket: " << m_socket << endl;


        if (SOCKET_ERROR == ::WSASendTo(m_socket, &wsaBuf, 1, OUT &numOfBytes, 0, reinterpret_cast<SOCKADDR*>(&m_sendEvent.remoteAddress), sizeof(SOCKADDR_IN), &m_sendEvent, nullptr))
        {
            const int32 errorCode = ::WSAGetLastError();
            if (errorCode != WSA_IO_PENDING)
            {
				HandleError(errorCode);
                m_sendEvent.sendBuffers.clear();
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

        udpSession->ProcessSend(numOfBytes);
    }

    void UdpRouter::ProcessRecv(int32 numOfBytes, const NetAddress& remoteAddress)
    {
        NetAddress from(m_remoteSockAddr);

        //wstring ip = from.GetIpAddress();
        //uint16 port = from.GetPort();

        //wcout << L"[UdpRouter::ProcessRecv] from : " << ip << L" , " << port << endl;

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
