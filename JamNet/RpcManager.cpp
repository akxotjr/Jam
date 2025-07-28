#include "pch.h"
#include "RpcManager.h"



namespace jam::net
{
	void RpcManager::Dispatch(Sptr<Session> session, uint16 rpcId, uint32 requestId, uint8 flags, BYTE* payload, uint32 payloadLen)
	{
        if (requestId != 0) 
        {
            auto it = m_resHandlers.find(rpcId);
            if (it != m_resHandlers.end()) 
            {
                it->second(payload, payloadLen, requestId);
            }
            else 
            {
                ResumeAwait(requestId, payload, payloadLen);
            }
            return;
        }

        auto it = m_reqHandlers.find(rpcId);
        if (it != m_reqHandlers.end()) 
        {
            it->second(session, payload, payloadLen, requestId);
        }
	}

    void RpcManager::RegisterAwait(uint32 requestId, AwaitCallback callback)
    {
        WRITE_LOCK
		m_callbacks[requestId] = std::move(callback);
    }

    void RpcManager::ResumeAwait(uint32 requestId, const BYTE* payload, uint32 len)
    {
        AwaitCallback cb;
        {
            WRITE_LOCK

            auto it = m_callbacks.find(requestId);
            if (it == m_callbacks.end()) 
                return;

            cb = std::move(it->second);
            m_callbacks.erase(it);
        }
        cb(payload, len);
    }
}
