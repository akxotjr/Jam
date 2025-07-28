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

		void Dispatch(Sptr<Session> session, uint16 rpcId, const char* data, size_t len);

		template<typename T>
		uint16 GetRpcId();


	private:
		xumap<uint16, std::function<void(Sptr<Session>, const char*, size_t)>> m_handlers;
	};






	template <typename T>
	void RpcManager::Register(std::function<void(Sptr<Session>, const T&)> handler)
	{
		uint16 rpcId = GetRpcId<T>();
		m_handlers[rpcId] = [handler](Sptr<Session> session, const char* data, size_t len)
			{
				T msg;
				if (!msg.ParseFromArray(data, static_cast<int>(len)))
				{
					std::cout << "Failed to parse RPC message : " << typeid(T).name() << '\n';
					return;
				}
				handler(session, msg);
			};
	}

	template <typename T>
	void RpcManager::Call(Sptr<Session> session, const T& message, bool reliable)
	{
		uint16 rpcId = GetRpcId<T>();

		std::string serialized;
		message.SerializeToString(&serialized);

		// 패킷 구성
		RpcPacketHeader header;
		header.id = rpcId;
		header.size = sizeof(header) + serialized.size();

		auto sendBuffer = SendBufferManager::Instance().Open(header.size);
		memcpy(sendBuffer->Buffer(), &header, sizeof(header));
		memcpy(sendBuffer->Buffer() + sizeof(header), serialized.data(), serialized.size());
		sendBuffer->Close(header.size);

		if (reliable)
			session->SendReliable(sendBuffer);
		else
			session->Send(sendBuffer);
	}

	template <typename T>
	uint16 RpcManager::GetRpcId()
	{
		static const uint16 id = static_cast<uint16>(std::hash<std::string>{}(typeid(T).name()) % 0xFFFF);
		return id;
	}

	inline void RpcManager::Dispatch(Sptr<Session> session, uint16 rpcId, const char* data, size_t len)
	{
		auto it = m_handlers.find(rpcId);
		if (it == m_handlers.end()) 
		{
			std::cout << "Unhandled RPC id: " << rpcId << '\n';
			return;
		}

		it->second(session, data, len);
	}
}

