#pragma once

#include "SessionComponents.h"

namespace jam::net
{
	class RecvBuffer;
	struct PacketView;

	template<typename T, typename = void>
	struct RPCKey { using type = T; };

	template<typename T>
	struct RPCKey<T, std::void_t<typename T::TableType>> { using type = typename T::TableType; };

	template<typename T>
    constexpr uint16_t TypeId16()
    {
        // FNV-1a 16bit compile-time hash
        constexpr const char* sig = __FUNCSIG__;
        uint32_t hash = 2166136261u;
        for (const char* p = sig; *p != '\0'; ++p)
        {
            hash = (hash ^ static_cast<uint8_t>(*p)) * 16777619u;
        }
        return static_cast<uint16_t>(hash & 0xFFFF);
    }

    template<typename T>
    constexpr uint16 RPCIdOf() { return TypeId16<typename RPCKey<T>::type>(); }

    struct RPCCallOptions
    {
        eChannelType    channel    = eChannelType::RELIABLE_ORDERED;
        uint64          timeout_ns = 0;
    };

    struct EntityRPCKey
    {
        entt::entity    entity;
        uint16          rpcId;

        bool operator==(const EntityRPCKey& other) const
        {
            return entity == other.entity && rpcId == other.rpcId;
        }
    };

    struct EntityRPCKeyHash
    {
        size_t operator()(const EntityRPCKey& key) const
        {
            return std::hash<uint32>()(static_cast<uint32>(key.entity)) ^ (std::hash<uint16>()(key.rpcId) << 1);
        }
    };

    struct RPCHandlersRegistry
    {
        using ReqHandler = std::function<void(entt::entity, const BYTE*, size_t, uint32)>;
        using ResHandler = std::function<void(entt::entity, const BYTE*, size_t, uint32)>;

        // 엔티티별 핸들러 (세션 특정)
        std::unordered_map<EntityRPCKey, ReqHandler, EntityRPCKeyHash> reqHandlers;
        std::unordered_map<EntityRPCKey, ResHandler, EntityRPCKeyHash> resHandlers;

        // 글로벌 핸들러 (모든 세션에 적용)
        std::unordered_map<uint16, ReqHandler> globalReqHandlers;
        std::unordered_map<uint16, ResHandler> globalResHandlers;
    };

    template<class C, class T>
    inline std::function<void(entt::entity, const T&, uint32)> BindRPC(C* obj, void (C::* mf)(entt::entity, const T&, uint32))
    {
        return [obj, mf](entt::entity e, const T& msg, uint32 reqId) { (obj->*mf)(e, msg, reqId); };
    }

    template<class C, class T>
    inline std::function<void(entt::entity, const T&, uint32)> BindRPC(const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
    {
        return [obj, mf](entt::entity e, const T& msg, uint32 reqId) { (obj->*mf)(e, msg, reqId); };
    }

    template<class C, class T>
    inline std::function<void(entt::entity, const T&, uint32)> BindRPC(std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        return [sp = std::move(sp), mf](entt::entity e, const T& msg, uint32 reqId) { (sp.get()->*mf)(e, msg, reqId); };
    }

    template<class C, class T>
    inline std::function<void(entt::entity, const T&, uint32)> BindRPC(std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        return [wp = std::move(wp), mf](entt::entity e, const T& msg, uint32 reqId) { if (auto sp = wp.lock()) (sp.get()->*mf)(e, msg, reqId); };
    }


    // fwd decl

    template<typename T>
    inline void RPCRegisterRequest(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn);
    template<typename T>
    inline void RPCRegisterRequest(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn);


    template<typename T, class C>
    inline void RPCRegisterRequest(entt::registry& R, entt::entity e, C* obj, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, e, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    inline void RPCRegisterRequest(entt::registry& R, entt::entity e, const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
    {
        RPCRegisterRequest<T>(R, e, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    inline void RPCRegisterRequest(entt::registry& R, entt::entity e, std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, e, BindRPC<C, T>(std::move(sp), mf));
    }

    template<typename T, class C>
    inline void RPCRegisterRequest(entt::registry& R, entt::entity e, std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, e, BindRPC<C, T>(std::move(wp), mf));
    }

    template<typename T, class C>
    inline void RPCRegisterRequest(entt::registry& R, C* obj, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    inline void RPCRegisterRequest(entt::registry& R, const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
    {
        RPCRegisterRequest<T>(R, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    inline void RPCRegisterRequest(entt::registry& R, std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, BindRPC<C, T>(std::move(sp), mf));
    }

    template<typename T, class C>
    inline void RPCRegisterRequest(entt::registry& R, std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, BindRPC<C, T>(std::move(wp), mf));
    }


    // fwd decl

    template<typename T>
    inline void RPCRegisterResponse(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn);
    template<typename T>
    inline void RPCRegisterResponse(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn);



    template<typename T, class C>
    inline void RPCRegisterResponse(entt::registry& R, entt::entity e, C* obj, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterResponse<T>(R, e, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    inline void RPCRegisterResponse(entt::registry& R, entt::entity e, const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
    {
        RPCRegisterResponse<T>(R, e, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    inline void RPCRegisterResponse(entt::registry& R, entt::entity e, std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterResponse<T>(R, e, BindRPC<C, T>(std::move(sp), mf));
    }

    template<typename T, class C>
    inline void RPCRegisterResponse(entt::registry& R, entt::entity e, std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterResponse<T>(R, e, BindRPC<C, T>(std::move(wp), mf));
    }

    template<typename T, class C>
    inline void RPCRegisterResponse(entt::registry& R, C* obj, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterResponse<T>(R, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    inline void RPCRegisterResponse(entt::registry& R, const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
    {
        RPCRegisterResponse<T>(R, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    inline void RPCRegisterResponse(entt::registry& R, std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterResponse<T>(R, BindRPC<C, T>(std::move(sp), mf));
    }

    template<typename T, class C>
    inline void RPCRegisterResponse(entt::registry& R, std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterResponse<T>(R, BindRPC<C, T>(std::move(wp), mf));
    }

    // 세션(엔티티) 종료 시 모든 RPC 핸들러 정리
    void RPCUnregisterAll(entt::registry& R, entt::entity e);

    // SessionSystems 파이프라인에서 호출할 수신 처리 엔트리
    struct RPC
    {
        static void EnsureRegistry(entt::registry& R);
        static void HandleIncomingPacket(entt::registry& R, entt::entity e, const PacketView& view, const std::shared_ptr<RecvBuffer>& buf);
    };


    template<typename T>
    inline void RPCRegisterRequest(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn)
    {
        RPC::EnsureRegistry(R);

        const uint16 rpcId = RPCIdOf<T>();
        EntityRPCKey key{ e, rpcId };
        auto& reg = R.ctx().get<RPCHandlersRegistry>();

        reg.reqHandlers[key] = [f = std::move(fn)](entt::entity e, const BYTE* data, size_t, uint32 requestId)
            {
                if (auto table = flatbuffers::GetRoot<typename T::TableType>(data))
                {
                    T obj;
                    table->UnPackTo(&obj);
                    f(e, obj, requestId);
                }
            };
    }

    template<typename T>
    inline void RPCRegisterRequest(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn)
    {
        RPC::EnsureRegistry(R);

        const uint16 rpcId = RPCIdOf<T>();
        auto& reg = R.ctx().get<RPCHandlersRegistry>();

        reg.globalReqHandlers[rpcId] = [f = std::move(fn)](entt::entity e, const BYTE* data, size_t, uint32 requestId)
            {
                if (auto table = flatbuffers::GetRoot<typename T::TableType>(data))
                {
                    T obj;
                    table->UnPackTo(&obj);
                    f(e, obj, requestId);
                }
            };
    }



    template<typename T>
    inline void RPCRegisterResponse(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn)
    {
        RPC::EnsureRegistry(R);

        const uint16 rpcId = RPCIdOf<T>();
        EntityRPCKey key{ e, rpcId };
        auto& reg = R.ctx().get<RPCHandlersRegistry>();

        reg.resHandlers[key] = [f = std::move(fn)](entt::entity e, const BYTE* data, size_t, uint32 requestId)
            {
                if (auto table = flatbuffers::GetRoot<typename T::TableType>(data))
                {
                    T obj;
                    table->UnPackTo(&obj);
                    f(e, obj, requestId);
                }
            };
    }


    template<typename T>
    inline void RPCRegisterResponse(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn)
    {
        RPC::EnsureRegistry(R);

        const uint16 rpcId = RPCIdOf<T>();
        auto& reg = R.ctx().get<RPCHandlersRegistry>();

        reg.globalResHandlers[rpcId] = [f = std::move(fn)](entt::entity e, const BYTE* data, size_t, uint32 requestId)
            {
                if (auto table = flatbuffers::GetRoot<typename T::TableType>(data))
                {
                    T obj;
                    table->UnPackTo(&obj);
                    f(e, obj, requestId);
                }
            };
    }


    template<typename Req>
    inline std::shared_ptr<SendBuffer> RPCBuildRequestPacket(const Req& req, const RPCCallOptions& opt, uint32 requestId)
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto offset = Req::TableType::Pack(fbb, &req);
        fbb.Finish(offset);

        const uint8_t* bufPtr = fbb.GetBufferPointer();
        const size_t bufSize = fbb.GetSize();

        const uint16 rpcId = RPCIdOf<Req>();
        constexpr auto rpcPktId = eRpcPacketId::FLATBUFFER_RPC;

        const uint32 payloadSize = sizeof(RpcHeader) + static_cast<uint32>(bufSize);
        auto open = PacketBuilder::OpenRpcPacket(rpcPktId, PacketFlags::NONE, opt.channel, payloadSize);
        if (!open.IsValid())
            return {};

        auto* rh = open.writer.Reserve<RpcHeader>();
        if (!rh)
            return {};

        rh->rpcId = rpcId;
        rh->requestId = requestId;
        rh->flags = RpcFlags::REQUEST;

        if (!open.writer.WriteBytes(bufPtr, static_cast<uint32>(bufSize)))
            return {};

        open.buf->Close(open.writer.WriteSize());
        return open.buf;
    }

    template<typename Res>
    inline std::shared_ptr<SendBuffer> RPCBuildResponsePacket(const Res& response, uint32 requestId, eChannelType channel)
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto offset = Res::TableType::Pack(fbb, &response);
        fbb.Finish(offset);

        const uint8_t* bufPtr = fbb.GetBufferPointer();
        const size_t bufSize = fbb.GetSize();

        constexpr auto rpcPktId = eRpcPacketId::FLATBUFFER_RPC;
        const uint32 payloadSize = sizeof(RpcHeader) + static_cast<uint32>(bufSize);

        auto open = PacketBuilder::OpenRpcPacket(rpcPktId, PacketFlags::NONE, channel, payloadSize);
        if (!open.IsValid())
            return {};

        auto* rh = open.writer.Reserve<RpcHeader>();
        if (!rh)
            return {};

        rh->rpcId = RPCIdOf<Res>();
        rh->requestId = requestId;
        rh->flags = RpcFlags::RESPONSE;

        if (!open.writer.WriteBytes(bufPtr, static_cast<uint32>(bufSize)))
            return {};

        open.buf->Close(open.writer.WriteSize());
        return open.buf;
    }

    template<typename Req>
    inline void RPCCall(entt::entity e, const Req& req, const RPCCallOptions& opt)
    {
        auto buf = RPCBuildRequestPacket(req, opt, 0);
        if (!buf)
            return;

        SendPacketToSession(e, buf);
    }

    template<typename Req, typename Res>
    inline std::optional<Res> RPCCallAwait(entt::entity e, const Req& req, const RPCCallOptions& opt)
    {
        auto& L = SHARD_LOCAL_CHECKED();
        auto& R = L.registry;
        auto* scheduler = L.scheduler;
        if (!scheduler)
            return std::nullopt;

        auto* rpcState = R.try_get<RpcState>(e);
        if (!rpcState)
        {
            R.emplace<RpcState>(e);
            rpcState = R.try_get<RpcState>(e);
            if (!rpcState)
                return std::nullopt;
        }

        const uint32 requestId = rpcState->GenerateRequestId();

        auto buf = RPCBuildRequestPacket(req, opt, requestId);
        if (!buf)
            return std::nullopt;

        std::optional<Res> out{};
        const auto awaitKey = (static_cast<uint64>(static_cast<uint32>(e)) << 32) | static_cast<uint64>(requestId);

        RpcState::AwaitState st{};
        st.onPayload = [&out](const BYTE* data, size_t)
            {
                if (auto table = flatbuffers::GetRoot<typename Res::TableType>(data))
                {
                    Res tmp;
                    table->UnPackTo(&tmp);
                    out = std::move(tmp);
                }
            };
        st.onDone = [scheduler, awaitKey](bool)
            {
                scheduler->PostResume(awaitKey);
            };

        uint64 deadline_ns = 0;
        if (opt.timeout_ns > 0)
        {
            st.hasDeadline = true;
            st.deadline_ns = NOW_NS() + opt.timeout_ns;
            deadline_ns = st.deadline_ns;
        }

        rpcState->RegisterRequest(requestId, std::move(st));

        SendPacketToSession(e, buf);

        const bool ok = scheduler->Suspend(awaitKey, deadline_ns);
        if (!ok)
        {
            if (auto* rpcState2 = R.try_get<RpcState>(e))
            {
                rpcState2->PopRequest(requestId);
            }
            return std::nullopt;
        }

        return out;
    }

    template<typename Res>
    inline void RPCSendResponse(entt::entity e, const Res& response, uint32 requestId, eChannelType channel)
    {
        auto buf = RPCBuildResponsePacket(response, requestId, channel);
        if (!buf)
            return;

        SendPacketToSession(e, buf);
    }




} // namespace jam::net
