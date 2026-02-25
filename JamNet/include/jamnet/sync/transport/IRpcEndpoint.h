#pragma once


namespace jam::net
{
	class Session;

	class IRpcEndpoint
	{
	public:
		virtual ~IRpcEndpoint() = default;


		// optional<T>
		template<typename Req, typename Res, typename C>
		void RPCCallAwaitMember(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(std::optional<std::decay_t<Res>>))
		{
			DoRpcCallAwaitMember<Req, Res>(userId, protocol, std::forward<Req>(req), opt, obj, mf);
		}

		// (bool, T)
		template<typename Req, typename Res, typename C>
		void RPCCallAwaitMember(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(bool, std::decay_t<Res>))
		{
			DoRpcCallAwaitMember<Req, Res>(userId, protocol, std::forward<Req>(req), opt, obj, mf);
		}

		// functor/람다 버전
		template<typename Req, typename Res, typename Cb>
		void RPCCallAwait(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, Cb&& cb)
		{
			using ReqT = std::decay_t<Req>;
			using ResT = std::decay_t<Res>;
			using CbT = std::decay_t<Cb>;

			DoRpcCallOnSessionImpl(
				userId, protocol,
				[req = std::forward<Req>(req), opt, cb = std::forward<Cb>(cb)](std::weak_ptr<Session> weak) mutable
				{
					jam::net::RPCCallAsync<ReqT, ResT>(std::move(weak), std::move(req), opt, std::move(cb));
				});
		}

	protected:
		virtual void DoRpcCallOnSessionImpl(uint64 userId, eProtocolType protocol, const std::function<void(std::weak_ptr<Session>)>& fn) = 0;

	private:
		template<typename Req, typename Res, typename C>
		void DoRpcCallAwaitMember(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(std::optional<std::decay_t<Res>>))
		{
			using ReqT = std::decay_t<Req>;
			using ResT = std::decay_t<Res>;

			DoRpcCallOnSessionImpl(
				userId, protocol,
				[req = std::forward<Req>(req), opt, obj, mf](std::weak_ptr<Session> weak) mutable
				{
					jam::net::RPCCallAsyncMember<ReqT, ResT>(std::move(weak), std::move(req), opt, obj, mf);
				});
		}

		template<typename Req, typename Res, typename C>
		void DoRpcCallAwaitMember(uint64 userId, eProtocolType protocol, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(bool, std::decay_t<Res>))
		{
			using ReqT = std::decay_t<Req>;
			using ResT = std::decay_t<Res>;

			DoRpcCallOnSessionImpl(
				userId, protocol,
				[req = std::forward<Req>(req), opt, obj, mf](std::weak_ptr<Session> weak) mutable
				{
					jam::net::RPCCallAsyncMember<ReqT, ResT>(std::move(weak), std::move(req), opt, obj, mf);
				});
		}
	};
}
