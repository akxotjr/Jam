#pragma once

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/FiberScheduler.h"
#include "jamnet/core/net/RPC.h"
#include "jamnet/core/net/Session.h"

namespace jam::net
{
	template<typename Fn>
	static void RunOnSessionJob(std::weak_ptr<Session> weak, Fn&& fn)
	{
		using FnT = std::decay_t<Fn>;

		auto fnsp = std::make_shared<FnT>(std::forward<Fn>(fn));

		auto s = weak.lock();
		if (!s) return;

		auto self = s->Self();
		self->Post(Job([weak, fnsp]() mutable
			{
				auto sp = weak.lock();
				if (!sp) return;
				const auto e = sp->GetEntity();
				if (e == entt::null) return;
				(*fnsp)(e);
			}));
	}

	template<typename T>
	static void RPCRegisterRequest(std::weak_ptr<Session> weak, std::function<void(entt::entity, const T&, uint32)> fn)
	{
		RunOnSessionJob(
			std::move(weak),
			[f = std::move(fn)](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterRequest<T>(R, e, std::move(f));
			});
	}

	template<typename T, class C>
	static void RPCRegisterRequest(std::weak_ptr<Session> weak, C* obj, void (C::* mf)(entt::entity, const T&, uint32))
	{
		RunOnSessionJob(
			std::move(weak),
			[obj, mf](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterRequest<T>(R, e, obj, mf);
			});
	}

	template<typename T, class C>
	static void RPCRegisterRequest(std::weak_ptr<Session> weak, const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
	{
		RunOnSessionJob(
			std::move(weak),
			[obj, mf](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterRequest<T>(R, e, obj, mf);
			});
	}

	template<typename T, class C>
	static void RPCRegisterRequest(std::weak_ptr<Session> weak, std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
	{
		RunOnSessionJob(
			std::move(weak),
			[sp = std::move(sp), mf](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterRequest<T>(R, e, std::move(sp), mf);
			});
	}

	template<typename T, class C>
	static void RPCRegisterRequest(std::weak_ptr<Session> weak, std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
	{
		RunOnSessionJob(
			std::move(weak),
			[wp = std::move(wp), mf](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterRequest<T>(R, e, std::move(wp), mf);
			});
	}

	// ---- RPCRegister (Response) ----

	template<typename T>
	static void RPCRegisterResponse(std::weak_ptr<Session> weak, std::function<void(entt::entity, const T&, uint32)> fn)
	{
		RunOnSessionJob(
			std::move(weak),
			[f = std::move(fn)](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterResponse<T>(R, e, std::move(f));
			});
	}

	template<typename T, class C>
	static void RPCRegisterResponse(std::weak_ptr<Session> weak, C* obj, void (C::* mf)(entt::entity, const T&, uint32))
	{
		RunOnSessionJob(
			std::move(weak),
			[obj, mf](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterResponse<T>(R, e, obj, mf);
			});
	}

	template<typename T, class C>
	static void RPCRegisterResponse(std::weak_ptr<Session> weak, const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
	{
		RunOnSessionJob(
			std::move(weak),
			[obj, mf](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterResponse<T>(R, e, obj, mf);
			});
	}

	template<typename T, class C>
	static void RPCRegisterResponse(std::weak_ptr<Session> weak, std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
	{
		RunOnSessionJob(
			std::move(weak),
			[sp = std::move(sp), mf](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterResponse<T>(R, e, std::move(sp), mf);
			});
	}

	template<typename T, class C>
	static void RPCRegisterResponse(std::weak_ptr<Session> weak, std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
	{
		RunOnSessionJob(
			std::move(weak),
			[wp = std::move(wp), mf](entt::entity e) mutable
			{
				auto& L = CurrentShardLocalChecked();
				auto& R = L.registry;
				RPCRegisterResponse<T>(R, e, std::move(wp), mf);
			});
	}

	// ---- RPCCall ----

	template<typename Req>
	static void RPCCallNative(std::weak_ptr<Session> weak, const Req& req, const RPCCallOptions& opt)
	{
		RunOnSessionJob(
			std::move(weak),
			[req, opt](entt::entity e) mutable
			{
				RPCCallNative(e, req, opt);
			}
		);
	}


	template <typename Fn, typename OnNull>
	static void RunOnSessionFiber(std::weak_ptr<Session> weak, Fn&& fn, OnNull&& onNull)
	{
		using FnT		= std::decay_t<Fn>;
		using OnNullT	= std::decay_t<OnNull>;

		auto fnsp		= std::make_shared<FnT>(std::forward<Fn>(fn));
		auto onNullsp	= std::make_shared<OnNullT>(std::forward<OnNull>(onNull));

		auto s = weak.lock();
		if (!s)
		{
			(*onNullsp)();
			return;
		}

		auto self = s->Self();
		self->Post(Job([weak, fnsp, onNullsp]() mutable
			{
				auto sp = weak.lock();
				if (!sp)
				{
					(*onNullsp)();
					return;
				}

				auto& L = CurrentShardLocalChecked();
				auto* scheduler = L.scheduler;
				if (!scheduler)
				{
					(*onNullsp)();
					return;
				}

				const auto e = sp->GetEntity();
				if (e == entt::null)
				{
					(*onNullsp)();
					return;
				}

				FiberDesc desc{};
				desc.name = "RPCAPI.RunOnSessionFiber";

				scheduler->PostSpawn([fnsp, e]() mutable { (*fnsp)(e); }, desc);

			}, eJobPriority::Control));
	}

	// Req/Res 타입만 알면, 파이버에서 RPCCallAwait 하고 cb를 호출하는 단일 진입점
	template <typename Req, typename Res, typename Cb>
	static void RPCCallAsyncNative(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, Cb&& cb)
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;
		using CbT  = std::decay_t<Cb>;

		auto cbsp = std::make_shared<CbT>(std::forward<Cb>(cb));

		RunOnSessionFiber(
			std::move(weak),
			[req = std::forward<Req>(req), opt, cbsp](entt::entity e) mutable {
				auto res = jam::net::RPCCallAwaitNative<ReqT, ResT>(e, req, opt);
				(*cbsp)(std::move(res));
			},
			[cbsp] {
				(*cbsp)(std::optional<ResT>{});
			});
	}

	template <typename ReqTable, typename ResTable, typename Cb>
	static void RPCCallAsync(std::weak_ptr<Session> weak, const void* flatBufferData, uint32 flatBufferSize, RPCCallOptions opt, Cb&& cb)
	{
		using CbT = std::decay_t<Cb>;

		auto cbsp = std::make_shared<CbT>(std::forward<Cb>(cb));
		auto payload = std::make_shared<std::vector<BYTE>>(flatBufferSize);
		if (flatBufferData && flatBufferSize != 0)
			::memcpy(payload->data(), flatBufferData, flatBufferSize);

		RunOnSessionFiber(
			std::move(weak),
			[payload, opt, cbsp](entt::entity e) mutable {
				auto res = jam::net::RPCCallAwait<ReqTable, ResTable>(e, payload->data(), static_cast<uint32>(payload->size()), opt);
				(*cbsp)(std::move(res));
			},
			[cbsp] {
				(*cbsp)(std::optional<RPCTableRef<ResTable>>{});
			});
	}

	template <typename ReqTable, typename ResTable, class C>
	static void RPCCallAsyncMember(std::weak_ptr<Session> weak, const void* flatBufferData, uint32 flatBufferSize, RPCCallOptions opt, C* obj, void (C::* mf)(std::optional<RPCTableRef<ResTable>>))
	{
		RPCCallAsync<ReqTable, ResTable>(
			std::move(weak),
			flatBufferData,
			flatBufferSize,
			opt,
			[obj, mf](std::optional<RPCTableRef<ResTable>> r) {
				(obj->*mf)(std::move(r));
			});
	}

	template <typename Req, typename Res, class C>
	static void RPCCallAsyncNativeMember(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(std::optional<std::decay_t<Res>>))
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[obj, mf](std::optional<ResT> r) {
				(obj->*mf)(std::move(r));
			});
	}

	template <typename Req, typename Res, class C>
	static void RPCCallAsyncNativeMember(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, const C* obj, void (C::* mf)(std::optional<std::decay_t<Res>>) const)
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[obj, mf](std::optional<ResT> r) {
				(obj->*mf)(std::move(r));
			});
	}

	template <typename Req, typename Res, class C>
	static void RPCCallAsyncNativeMember(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, std::shared_ptr<C> sp, void (C::* mf)(std::optional<std::decay_t<Res>>))
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[sp = std::move(sp), mf](std::optional<ResT> r) {
				(sp.get()->*mf)(std::move(r));
			});
	}

	template <typename Req, typename Res, class C>
	static void RPCCallAsyncNativeMember(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, std::weak_ptr<C> wp, void (C::* mf)(std::optional<std::decay_t<Res>>))
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[wp = std::move(wp), mf](std::optional<ResT> r) {
				if (auto sp = wp.lock())
					(sp.get()->*mf)(std::move(r));
			});
	}

	template <typename Req, typename Res, class C>
	static void RPCCallAsyncNativeMember(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, C* obj, void (C::* mf)(bool, std::decay_t<Res>))
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[obj, mf](std::optional<ResT> r) {
				if	 (r) (obj->*mf)(true, std::move(*r));
				else	 (obj->*mf)(false, ResT{});
			});
	}

	template <typename Req, typename Res, class C>
	static void RPCCallAsyncNativeMember(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, const C* obj, void (C::* mf)(bool, std::decay_t<Res>) const)
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[obj, mf](std::optional<ResT> r) {
				if	 (r) (obj->*mf)(true, std::move(*r));
				else	 (obj->*mf)(false, ResT{});
			});
	}

	template <typename Req, typename Res, class C>
	static void RPCCallAsyncNativeMember(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, std::shared_ptr<C> sp, void (C::* mf)(bool, std::decay_t<Res>))
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[sp = std::move(sp), mf](std::optional<ResT> r) {
				if	 (r) (sp.get()->*mf)(true, std::move(*r));
				else	 (sp.get()->*mf)(false, ResT{});
			});
	}

	template <typename Req, typename Res, class C>
	static void RPCCallAsyncNativeMember(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, std::weak_ptr<C> wp, void (C::* mf)(bool, std::decay_t<Res>))
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[wp = std::move(wp), mf](std::optional<ResT> r) {
				if (auto sp = wp.lock())
				{
					if	 (r) (sp.get()->*mf)(true, std::move(*r));
					else	 (sp.get()->*mf)(false, ResT{});
				}
			});
	}

	template <typename Req, typename Res, typename Fn>
	static void RPCCallAsyncNativeBR(std::weak_ptr<Session> weak, Req&& req, RPCCallOptions opt, Fn&& fn)
	{
		using ReqT = std::decay_t<Req>;
		using ResT = std::decay_t<Res>;
		using FnT  = std::decay_t<Fn>;

		auto f = std::make_shared<FnT>(std::forward<Fn>(fn));

		RPCCallAsyncNative<ReqT, ResT>(
			std::move(weak),
			std::forward<Req>(req),
			opt,
			[f](std::optional<ResT> r) {
				if	 (r) (*f)(true, std::move(*r));
				else 	 (*f)(false, ResT{});
			});
	}
}
