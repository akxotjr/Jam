#pragma once
#include "IocpCore.h"

namespace jam::net
{
    class UdpReceiver : public IocpObject
    {
        enum { BUFFER_SIZE = 0x10000 };

    public:
        UdpReceiver();

        bool                        Start(ServiceRef service);
        virtual void                OnRecv(SessionRef& session, BYTE* buffer, int32 len) = 0;

        SOCKET                      GetSocket() const { return _socket; }

        /* IocpObject interface impl */
        virtual HANDLE              GetHandle() override;
        virtual void                Dispatch(IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

    private:
        void                        RegisterRecv();
        bool                        ProcessRecv(int32 numOfBytes, UdpSessionRef session);
        int32                       IsParsingPacket(BYTE* buffer, const int32 len, UdpSessionRef session);

    private:
        SOCKET                      _socket = INVALID_SOCKET;
        RecvBuffer                  _recvBuffer;
        SOCKADDR_IN                 _remoteAddr = {};	// is thread safe? 
        RecvEvent                   _recvEvent;

        std::weak_ptr<Service>      _service;
    };
}

