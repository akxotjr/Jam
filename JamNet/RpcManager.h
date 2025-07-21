#pragma once

namespace jam::net
{
	class RpcManager
	{
		DECLARE_SINGLETON(RpcManager)
	public:
		template<typename T>
		void Register(std::function<void(Sptr<Session>, const T&)> handler);

		template<typename T>
		void Call(Sptr<Session> session, const T& message, bool reliable = true);

		void Dispatch(Sptr<Session> session, uint16 rpcId, uint32 requestId, uint8 flags, BYTE* payload, uint32 payloadLen);


	private:
		xumap<uint16, std::function<void(Sptr<Session>, const char*, size_t)>> m_handlers;
	};






	template <typename T>
	void RpcManager::Register(std::function<void(Sptr<Session>, const T&)> handler)
	{

	}

	template <typename T>
	void RpcManager::Call(Sptr<Session> session, const T& message, bool reliable)
	{
	}

	inline void RpcManager::Dispatch(Sptr<Session> session, uint16 rpcId, uint32 requestId, uint8 flags, BYTE* payload, uint32 payloadLen)
	{
	}
}

