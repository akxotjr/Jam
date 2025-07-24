#pragma once
#include "RpcHandler.h"
#include "Session.h"
#include "BufferWriter.h"
#include "SendBuffer.h"
#include "UdpSession.h"

namespace jam::net
{
	class Service;

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

		template<typename T>
		void Call(Sptr<Session> session, const T& msg, bool reliable = true);
		template<typename Req, typename Res>
		Res CallWithResponse(Sptr<Session> session, const Req& req);

		void Dispatch(Sptr<Session> session, uint16 rpcId, uint32 requestId, uint8 flags, BYTE* payload, uint32 payloadLen);

	private:
		void RegisterAwait(uint32 requestId, AwaitCallback callback);
		void ResumeAwait(uint32 requestId, const BYTE* payload, uint32 len);

	private:
		USE_LOCK

		xumap<uint16, RequestHandler> m_reqHandlers;
		xumap<uint16, ResponseHandler> m_resHandlers;

		xumap<uint32, AwaitCallback> m_callbacks;	// key : requestId

		Atomic<uint32> m_requestIdGen = 1;
	};



//#define REGISTER_RPC_HANDLER(MsgType, Fn) \
//		RpcManager::Instance().Register<MsgType>(Fn);
//
//#define DECLARE_RPC_HANDLER(MsgType, ClassType, Method) \
//		RpcManager::Instance().Register<MsgType>([](Sptr<Session> session, const MsgType& msg) { \
//			ClassType::Instance().Method(session, msg); \
//		});

	template<typename T>
	inline void RpcManager::RegisterReqHandler(std::function<void(Sptr<Session>, const T&, uint32)> handler)
	{
		uint16 rpcId = T::identifier;

		m_reqHandlers[rpcId] = [handler](Sptr<Session> session, const char* data, size_t len, uint32 requestId) {
				flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(data), len);
				if (!verifier.VerifyBuffer<T>(nullptr)) 
					return;
				const T* msg = flatbuffers::GetRoot<T>(data);
				handler(session, *msg, requestId);
			};
	}

	template<typename T>
	inline void RpcManager::RegisterResHandler(std::function<void(const T&, uint32)> handler)
	{
		uint16 rpcId = T::identifier;

		m_resHandlers[rpcId] = [handler](const char* data, size_t len, uint32 requestId) {
				flatbuffers::Verifier verifier(reinterpret_cast<const uint8*>(data), len);
				if (!verifier.VerifyBuffer<T>(nullptr)) return;
				const T* msg = flatbuffers::GetRoot<T>(data);
				handler(*msg, requestId);
			};
	}

	template<typename T>
	inline void RpcManager::Call(Sptr<Session> session, const T& msg, bool reliable)
	{
		flatbuffers::FlatBufferBuilder fbb;
		auto offset = T::Pack(fbb, &msg);
		T::Finish(fbb, offset);

		uint16 rpcId = T::identifier;
		uint32 requestId = 0;

		uint16 totalSize = sizeof(PacketHeader) + sizeof(RpcHeader) + fbb.GetSize();
		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(totalSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader* pktHeader = bw.Reserve<PacketHeader>();
		pktHeader->sizeAndflags = MakeSizeAndFlags(totalSize, 0);
		pktHeader->type = static_cast<uint8>(ePacketType::RPC);

		RpcHeader* rpcHeader = bw.Reserve<RpcHeader>();
		rpcHeader->rpcId = rpcId;
		rpcHeader->requestId = requestId;
		rpcHeader->flags = 0;
		bw.Write(fbb.GetBufferPointer(), fbb.GetSize());

		buf->Close(totalSize);

		if (reliable)
		{
			auto rudpSession = static_pointer_cast<UdpSession>(session);
			rudpSession->SendReliable(buf);
		}
		else
		{
			session->Send(buf);
		}
	}

	template<typename Req, typename Res>
	inline Res RpcManager::CallWithResponse(Sptr<Session> session, const Req& req)
	{
		flatbuffers::FlatBufferBuilder fbb;
		auto offset = Req::Pack(fbb, &req);
		Req::Finish(fbb, offset);

		uint16 rpcId = Req::identifier;
		uint32 requestId = m_requestIdGen.fetch_add(1);

		uint16 totalSize = sizeof(PacketHeader) + sizeof(RpcHeader) + fbb.GetSize();
		Sptr<SendBuffer> buf = SendBufferManager::Instance().Open(totalSize);
		BufferWriter bw(buf->Buffer(), buf->AllocSize());

		PacketHeader* pktHeader = bw.Reserve<PacketHeader>();
		pktHeader->sizeAndflags = MakeSizeAndFlags(totalSize, 0);
		pktHeader->type = static_cast<uint8>(ePacketType::RPC);

		RpcHeader* rpcHeader = bw.Reserve<RpcHeader>();
		rpcHeader->rpcId = rpcId;
		rpcHeader->requestId = requestId;
		rpcHeader->flags = 0;

		bw.Write(fbb.GetBufferPointer(), fbb.GetSize());

		buf->Close(totalSize);

		Res result;

		Fiber* currentFiber = tl_Worker->GetScheduler()->CurrentFiber();

		RegisterAwait(requestId, [currentFiber, &result](const char* data, size_t len) {
			const Res* res = flatbuffers::GetRoot<Res>(data);
			result = *res;

			JobRef job = Job::Make([currentFiber]() {
				currentFiber->SwitchTo();
				});
			tl_Worker->GetJobQueue()->Push(job);
			});

		session->SendReliable(buffer);
		Fiber::Yield();
		return result;
	}



}

