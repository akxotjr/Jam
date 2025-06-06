#include "pch.h"
#include "SessionManager.h"

void SessionManager::Add(Sptr<Session> session)
{
    uint32 id = session->GetId();
    SessionBundle& bundle = _sessionMap[id];

    WRITE_LOCK

    if (session->IsTcp())
    {
        bundle.tcpSession = static_pointer_cast<GameTcpSession>(session);
    }
    else if (session->IsUdp())
    {
        bundle.udpSession = static_pointer_cast<GameUdpSession>(session);
    }
}

void SessionManager::Remove(Sptr<Session> session)
{
    uint32 id = session->GetId();
    SessionBundle& bundle = _sessionMap[id];

    WRITE_LOCK

        if (session->IsTcp())
        {
            bundle.tcpSession = nullptr;
        }
        else if (session->IsUdp())
        {
            bundle.udpSession = nullptr;
        }

    if (!bundle.tcpSession && !bundle.udpSession)
    {
        _sessionMap.erase(id);
    }
}

void SessionManager::Multicast(EProtocolType protocol, Sptr<jam::net::SendBuffer> sendBuffer, bool reliable)
{
    WRITE_LOCK

    for (auto& bundle : _sessionMap | views::values)
    {
        if (protocol == EProtocolType::TCP)
        {
            bundle.tcpSession->Send(sendBuffer);
        }
        else if (protocol == EProtocolType::UDP)
        {
            if (reliable)
            {
                double timestamp = TimeManager::Instance().GetCurrentTime();
                bundle.udpSession->SendReliable(sendBuffer, timestamp);
            }
            else
            {
                bundle.udpSession->Send(sendBuffer);
            }
        }
    }
}

Sptr<Session> SessionManager::GetSessionByUserId(EProtocolType type, uint32 userId)
{
    WRITE_LOCK

        if (type == EProtocolType::TCP)
        {
            return _sessionMap[userId].tcpSession;
        }
        else if (type == EProtocolType::UDP)
        {
            return _sessionMap[userId].udpSession;
        }

    return nullptr;
}