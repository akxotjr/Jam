#pragma once
#include "IocpCore.h"

namespace jam::net
{
    class Service;
    class UpdSession;

    class UdpReceiver : public IocpObject
    {
        enum { BUFFER_SIZE = 0x10000 };

    public:
        UdpReceiver();

        bool                        Start(Sptr<Service> service);
        virtual void                OnRecv(Sptr<Session>& session, BYTE* buffer, int32 len) = 0;

        SOCKET                      GetSocket() const { return m_socket; }

        /* IocpObject interface impl */
        virtual HANDLE              GetHandle() override;
        virtual void                Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

    private:
        void                        RegisterRecv();
        bool                        ProcessRecv(int32 numOfBytes, Sptr<UdpSession> session);
        int32                       IsParsingPacket(BYTE* buffer, const int32 len, Sptr<UdpSession> session);

    private:
        SOCKET                      m_socket = INVALID_SOCKET;
        RecvBuffer                  m_recvBuffer;
        SOCKADDR_IN                 m_remoteAddr = {};	// is thread safe? 
        RecvEvent                   m_recvEvent;

        std::weak_ptr<Service>      m_service;
    };
}

