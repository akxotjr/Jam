#pragma once
#include "Session.h"
#include "BufferWriter.h"
#include "Fiber.h"
#include "SendBuffer.h"
#include "UdpSession.h"
#include "PacketBuilder.h"
#include "Serializer.h"

namespace jam::net
{
	class Service;


	enum class eRpcPacketId : uint8
	{
		FLATBUFFER_RPC	= 1,      
		PROTOBUF_RPC	= 2,        
		JSON_RPC		= 3,           
		BINARY_RPC		= 4,          
	};

	constexpr eRpcPacketId ToRpcPacketId(eSerializationType type)
	{
		switch (type)
		{
		case eSerializationType::BINARY:     return eRpcPacketId::BINARY_RPC;
		case eSerializationType::FLATBUFFER: return eRpcPacketId::FLATBUFFER_RPC;
		case eSerializationType::PROTOBUF:   return eRpcPacketId::PROTOBUF_RPC;
		case eSerializationType::JSON:       return eRpcPacketId::JSON_RPC;
		}
		return eRpcPacketId::FLATBUFFER_RPC;
	}

	constexpr eSerializationType ToSerializationType(eRpcPacketId id)
	{
		switch (id)
		{
		case eRpcPacketId::BINARY_RPC:     return eSerializationType::BINARY;
		case eRpcPacketId::FLATBUFFER_RPC: return eSerializationType::FLATBUFFER;
		case eRpcPacketId::PROTOBUF_RPC:   return eSerializationType::PROTOBUF;
		case eRpcPacketId::JSON_RPC:       return eSerializationType::JSON;
		}
		return eSerializationType::FLATBUFFER;
	}

	namespace RpcFlags
	{
		constexpr uint8 NONE = 0x00;
		constexpr uint8 REQUEST = 0x01;
		constexpr uint8 RESPONSE = 0x02;
	}


#pragma pack(push, 1)
	struct RpcHeader
	{
		uint16 rpcId;        // 어떤 RPC인지
		uint32 requestId;    // 응답 매칭용
		uint8  flags;        // 예: isResponse, isReliable, isCompressed 등
	};
#pragma pack(pop)



	class RpcManager
	{
		using RequestHandler = std::function<void(Sptr<Session>, const BYTE*, size_t, uint32)>;
		using ResponseHandler = std::function<void(const BYTE*, size_t, uint32)>;
		using AwaitCallback = std::function<void(const BYTE*, size_t)>;

	public:
		RpcManager() = default;
		~RpcManager() = default;

		template<typename T>
		void RegisterReqHandler(std::function<void(Sptr<Session>, const T&, uint32)> handler);

		template<typename T>
		void RegisterResHandler(std::function<void(const T&, uint32)> handler);

		template<typename Req>
		void Call(Sptr<Session> session, const Req& req, bool reliable = true);

		template<typename Req, typename Res>
		Res CallWithResponse(Sptr<Session> session, const Req& req, bool reliable = true);

		void Dispatch(Sptr<Session> session, uint16 rpcId, uint32 requestId, uint8 flags, BYTE* payload, uint32 payloadLen);

		template<typename Res>
		void SendResponse(Sptr<Session> session, const Res& response, uint32 requestId);

	private:
		void RegisterAwait(uint32 requestId, AwaitCallback callback);
		void ResumeAwait(uint32 requestId, const BYTE* payload, uint32 len);

	private:
		USE_LOCK

		xumap<uint16, RequestHandler>	m_reqHandlers;	// key : rpcId
		xumap<uint16, ResponseHandler>	m_resHandlers;	// key : rpcId
		xumap<uint32, AwaitCallback>	m_callbacks;	// key : requestId

		Atomic<uint32>					m_requestIdGen = 1;
	};


	template<typename T>
	inline void RpcManager::RegisterReqHandler(std::function<void(Sptr<Session>, const T&, uint32)> handler)
	{
		uint16 rpcId = T::identifier;

		m_reqHandlers[rpcId] = [handler](Sptr<Session> session, const char* data, size_t len, uint32 requestId) {
				T obj;
				if (Serializer::Deserialize(data, static_cast<uint32>(len), obj))
				{
					handler(session, obj, requestId);
				}
			};
	}

	template<typename T>
	inline void RpcManager::RegisterResHandler(std::function<void(const T&, uint32)> handler)
	{
		uint16 rpcId = T::identifier;

		m_resHandlers[rpcId] = [handler](const char* data, size_t len, uint32 requestId) {
				T obj;
				if (Serializer::Deserialize(data, static_cast<uint32>(len), obj))
				{
					handler(obj, requestId);
				}
			};
	}

	template<typename Req>
	inline void RpcManager::Call(Sptr<Session> session, const Req& req, bool reliable)
	{
		auto serialized = Serializer::Serialize(req);
		if (!serialized.success)
			return;

		const uint16 rpcId = Req::identifier;
		const uint32 requestId = 0;

		constexpr auto serializedType = SerializationType<Req>::value;
		constexpr auto packetId = ToRpcPacketId(serializedType);

		uint32 payloadSize = sizeof(RpcHeader) + serialized.data.size();

		auto buf = PacketBuilder::CreateRpcPacket(
			packetId,
			reliable ? PacketFlags::RELIABLE : PacketFlags::NONE,
			nullptr,
			payloadSize
		);

		if (buf)
		{
			BufferWriter bw(buf->Buffer(), buf->AllocSize());

			RpcHeader* rpcHeader = bw.Reserve<RpcHeader>();
			rpcHeader->rpcId = rpcId;
			rpcHeader->requestId = requestId;
			rpcHeader->flags = 0;

			bw.WriteBytes(serialized.data.data(), serialized.data.size());
			buf->Close(bw.WriteSize());

			session->Send(buf);
		}
	}

	template<typename Req, typename Res>
	inline Res RpcManager::CallWithResponse(Sptr<Session> session, const Req& req, bool reliable)
	{
		auto serialized = Serializer::Serialize(req);
		if (!serialized.success) 
			return Res{};

		uint16 rpcId = Req::identifier;
		uint32 requestId = m_requestIdGen.fetch_add(1, std::memory_order_relaxed);

		constexpr auto serializedType = SerializationType<Req>::value;
		constexpr auto packetId = ToRpcPacketId(serializedType);

		uint32 payloadSize = sizeof(RpcHeader) + serialized.data.size();

		auto buf = PacketBuilder::CreateRpcPacket(
			packetId, 
			reliable ? PacketFlags::RELIABLE : PacketFlags::NONE, 
			nullptr, 
			payloadSize
		);

		if (buf) 
		{
			BufferWriter bw(buf->Buffer(), buf->AllocSize());

			RpcHeader* rpcHeader = bw.Reserve<RpcHeader>();
			rpcHeader->rpcId = rpcId;
			rpcHeader->requestId = requestId;
			rpcHeader->flags = 0;

			bw.WriteBytes(serialized.data.data(), serialized.data.size());
			buf->Close(bw.WriteSize());

			Res result{};
			utils::thrd::Fiber* currentFiber = utils::thrd::tl_Worker->GetScheduler()->GetCurrentFiber();

			RegisterAwait(requestId, [currentFiber, &result](const BYTE* data, size_t len) {
					if (Serializer::Deserialize(data, static_cast<uint32>(len), result)) {

						Sptr<utils::job::Job> job = utils::memory::MakeShared<utils::job::Job>([currentFiber]() {
								currentFiber->SwitchTo();
							});
						utils::thrd::tl_Worker->GetCurrentJobQueue()->Push(job);
					}
				});

			session->Send(buf);

			currentFiber->YieldJob();
		}

		return result;
	}


	template<typename Res>
	inline void RpcManager::SendResponse(Sptr<Session> session, const Res& response, uint32 requestId)
	{
		auto serialized = Serializer::Serialize(response);
		if (!serialized.success) return;

		constexpr auto serType = SerializationType<Res>::value;
		constexpr auto packetId = ToRpcPacketId(serType);

		auto buf = PacketBuilder::CreateRpcPacket(packetId, PacketFlags::NONE, nullptr,
			sizeof(RpcHeader) + serialized.data.size());
		if (buf) 
		{
			BufferWriter bw(buf->Buffer(), buf->AllocSize());

			RpcHeader* rpcHeader = bw.Reserve<RpcHeader>();
			rpcHeader->rpcId = Res::identifier;
			rpcHeader->requestId = requestId;  
			rpcHeader->flags = RpcFlags::RESPONSE;  

			bw.WriteBytes(serialized.data.data(), serialized.data.size());
			buf->Close(bw.WriteSize());

			session->Send(buf);
		}
	}
}


