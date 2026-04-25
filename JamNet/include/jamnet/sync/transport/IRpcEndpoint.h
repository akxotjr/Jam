#pragma once
#include "jamnet/core/net/RPCAPI.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/runtime/schema/RPCSchemaIds.h"


namespace jam::net
{
	class Session;

	class IRpcEndpoint
	{
	public:
		virtual ~IRpcEndpoint() = default;


		// optional<T>
		template<typename Req, typename Res, typename C>
		void RPCCallAwaitNativeMember(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(std::optional<std::decay_t<Res>>))
		{
			DoRpcCallAwaitNativeMember<Req, Res>(userId, protocol, std::forward<Req>(req), opt, obj, mf);
		}

		// (bool, T)
		template<typename Req, typename Res, typename C>
		void RPCCallAwaitNativeMember(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(bool, std::decay_t<Res>))
		{
			DoRpcCallAwaitNativeMember<Req, Res>(userId, protocol, std::forward<Req>(req), opt, obj, mf);
		}

		// functor/람다 버전
		template<typename Req, typename Res, typename Cb>
		void RPCCallAwaitNative(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, Cb&& cb)
		{
			using ReqT = std::decay_t<Req>;
			using ResT = std::decay_t<Res>;
			using CbT = std::decay_t<Cb>;

			DoRpcCallOnSessionImpl(
				userId, protocol,
				[req = std::forward<Req>(req), opt, cb = std::forward<Cb>(cb)](Session* session) mutable
				{
					jam::net::RPCCallAsyncNative<ReqT, ResT>(session, std::move(req), opt, std::move(cb));
				});
		}

		template<typename ReqTable, typename ResTable, typename C>
		void RPCCallAwaitMember(uint64 userId, eProtocolType protocol, const void* flatBufferData, uint32 flatBufferSize, RPCCallOptions opt, C* obj, void (C::* mf)(std::optional<RPCTableRef<ResTable>>))
		{
			auto payload = std::make_shared<std::vector<BYTE>>(flatBufferSize);
			if (flatBufferData && flatBufferSize != 0)
				::memcpy(payload->data(), flatBufferData, flatBufferSize);

			DoRpcCallOnSessionImpl(
				userId, protocol,
				[payload, opt, obj, mf](Session* session) mutable
				{
					jam::net::RPCCallAsyncMember<ReqTable, ResTable>(session, payload->data(), static_cast<uint32>(payload->size()), opt, obj, mf);
				});
		}

	protected:
		virtual void DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(Session*)>& fn) = 0;

	private:
		template<typename Req, typename Res, typename C>
		void DoRpcCallAwaitNativeMember(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(std::optional<std::decay_t<Res>>))
		{
			using ReqT = std::decay_t<Req>;
			using ResT = std::decay_t<Res>;

			DoRpcCallOnSessionImpl(
				userId, protocol,
				[req = std::forward<Req>(req), opt, obj, mf](Session* session) mutable
				{
					jam::net::RPCCallAsyncNativeMember<ReqT, ResT>(session, std::move(req), opt, obj, mf);
				});
		}

		template<typename Req, typename Res, typename C>
		void DoRpcCallAwaitNativeMember(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(bool, std::decay_t<Res>))
		{
			using ReqT = std::decay_t<Req>;
			using ResT = std::decay_t<Res>;

			DoRpcCallOnSessionImpl(
				userId, protocol,
				[req = std::forward<Req>(req), opt, obj, mf](Session* session) mutable
				{
					jam::net::RPCCallAsyncNativeMember<ReqT, ResT>(session, std::move(req), opt, obj, mf);
				});
		}
	};
}
