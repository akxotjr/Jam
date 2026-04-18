#pragma once

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/executor/ShardExecutor.h"

#include "jamnet/core/net/SessionComponents.h"
#include "jamnet/core/net/SessionSystems.h"
#include "jamnet/core/net/PacketBuilder.h"

#include <flatbuffers/flatbuffers.h>


namespace jam::net
{
	struct PacketHeaderView;

	template<typename T, typename = void>
	struct RPCKey { using type = T; };

	template<typename T>
	struct RPCKey<T, std::void_t<typename T::TableType>> { using type = typename T::TableType; };

    template<typename T, typename = void>
    struct IsFlatBufferNative : std::false_type {};

    template<typename T>
    struct IsFlatBufferNative<T, std::void_t<typename T::TableType>> : std::true_type {};

    template<typename T>
    inline constexpr bool IsFlatBufferNativeV = IsFlatBufferNative<T>::value;

    template<typename T>
    using RPCWireTableT = typename RPCKey<T>::type;

    template<typename>
    inline constexpr bool AlwaysFalseV = false;

    template<typename Table>
    struct RPCIdTraits
    {
        static constexpr bool registered = false;
    };

    template<typename Table>
    const Table* RPCGetVerifiedRoot(const BYTE* data, size_t size)
    {
        if (!data || size == 0)
            return nullptr;

        flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(data), size);
        const auto* table = flatbuffers::GetRoot<Table>(data);
        if (!table || !table->Verify(verifier))
            return nullptr;

        return table;
    }

    template<typename Table>
    struct RPCTableRef
    {
        Packet owner;
        const Table* table = nullptr;

        explicit operator bool() const { return owner.IsValid() && table; }
        const Table& operator*() const { return *table; }
        const Table* operator->() const { return table; }
    };

    template<typename T>
    constexpr uint16 RPCIdOf()
    {
        using Table = RPCWireTableT<T>;
        static_assert(RPCIdTraits<Table>::registered, "Missing RPCIdTraits specialization for this RPC table type.");
        return RPCIdTraits<Table>::value;
    }

    struct RPCCallOptions
    {
        eChannel    channel    = eChannel::RELIABLE_ORDERED;
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
    std::function<void(entt::entity, const T&, uint32)> BindRPC(C* obj, void (C::* mf)(entt::entity, const T&, uint32))
    {
        return [obj, mf](entt::entity e, const T& msg, uint32 reqId) { (obj->*mf)(e, msg, reqId); };
    }

    template<class C, class T>
    std::function<void(entt::entity, const T&, uint32)> BindRPC(const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
    {
        return [obj, mf](entt::entity e, const T& msg, uint32 reqId) { (obj->*mf)(e, msg, reqId); };
    }

    template<class C, class T>
    std::function<void(entt::entity, const T&, uint32)> BindRPC(std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        return [sp = std::move(sp), mf](entt::entity e, const T& msg, uint32 reqId) { (sp.get()->*mf)(e, msg, reqId); };
    }

    template<class C, class T>
    std::function<void(entt::entity, const T&, uint32)> BindRPC(std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        return [wp = std::move(wp), mf](entt::entity e, const T& msg, uint32 reqId) { if (auto sp = wp.lock()) (sp.get()->*mf)(e, msg, reqId); };
    }



    template<typename T>
    void RPCRegisterRequest(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn);
    template<typename T>
    void RPCRegisterRequest(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn);
    template<typename T>
    void RPCRegisterRequestNative(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn);
    template<typename T>
    void RPCRegisterRequestNative(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn);


    template<typename T, class C>
    void RPCRegisterRequest(entt::registry& R, entt::entity e, C* obj, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, e, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    void RPCRegisterRequest(entt::registry& R, entt::entity e, const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
    {
        RPCRegisterRequest<T>(R, e, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    void RPCRegisterRequest(entt::registry& R, entt::entity e, std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, e, BindRPC<C, T>(std::move(sp), mf));
    }

    template<typename T, class C>
    void RPCRegisterRequest(entt::registry& R, entt::entity e, std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, e, BindRPC<C, T>(std::move(wp), mf));
    }

    template<typename T, class C>
    void RPCRegisterRequest(entt::registry& R, C* obj, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    void RPCRegisterRequest(entt::registry& R, const C* obj, void (C::* mf)(entt::entity, const T&, uint32) const)
    {
        RPCRegisterRequest<T>(R, BindRPC<C, T>(obj, mf));
    }

    template<typename T, class C>
    void RPCRegisterRequest(entt::registry& R, std::shared_ptr<C> sp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, BindRPC<C, T>(std::move(sp), mf));
    }

    template<typename T, class C>
    void RPCRegisterRequest(entt::registry& R, std::weak_ptr<C> wp, void (C::* mf)(entt::entity, const T&, uint32))
    {
        RPCRegisterRequest<T>(R, BindRPC<C, T>(std::move(wp), mf));
    }


    // fwd decl

    template<typename T>
    inline void RPCRegisterResponse(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn);
    template<typename T>
    inline void RPCRegisterResponse(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn);
    template<typename T>
    inline void RPCRegisterResponseNative(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn);
    template<typename T>
    inline void RPCRegisterResponseNative(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn);



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
        static void HandleIncomingPacket(entt::registry& R, entt::entity e, const PacketHeaderView& view, Packet packet);
    };


    template<typename T>
    inline void RPCRegisterRequestNative(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn)
    {
        using Table = RPCWireTableT<T>;

        if constexpr (IsFlatBufferNativeV<T>)
        {
            RPCRegisterRequest<Table>(
                R,
                e,
                [f = std::move(fn)](entt::entity e, const Table& table, uint32 requestId) mutable
                {
                    T obj;
                    table.UnPackTo(&obj);
                    f(e, obj, requestId);
                });
        }
        else
        {
            RPCRegisterRequest<T>(R, e, std::move(fn));
        }
    }

    template<typename T>
    inline void RPCRegisterRequestNative(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn)
    {
        using Table = RPCWireTableT<T>;

        if constexpr (IsFlatBufferNativeV<T>)
        {
            RPCRegisterRequest<Table>(
                R,
                [f = std::move(fn)](entt::entity e, const Table& table, uint32 requestId) mutable
                {
                    T obj;
                    table.UnPackTo(&obj);
                    f(e, obj, requestId);
                });
        }
        else
        {
            RPCRegisterRequest<T>(R, std::move(fn));
        }
    }

    template<typename Table>
    inline void RPCRegisterRequest(entt::registry& R, entt::entity e, std::function<void(entt::entity, const Table&, uint32)> fn)
    {
        RPC::EnsureRegistry(R);

        const uint16 rpcId = RPCIdOf<Table>();
        EntityRPCKey key{ e, rpcId };
        auto& reg = R.ctx().get<RPCHandlersRegistry>();

        reg.reqHandlers[key] = [f = std::move(fn)](entt::entity e, const BYTE* data, size_t size, uint32 requestId) mutable
            {
                if (const auto* table = RPCGetVerifiedRoot<Table>(data, size))
                    f(e, *table, requestId);
            };
    }

    template<typename Table>
    inline void RPCRegisterRequest(entt::registry& R, std::function<void(entt::entity, const Table&, uint32)> fn)
    {
        RPC::EnsureRegistry(R);

        const uint16 rpcId = RPCIdOf<Table>();
        auto& reg = R.ctx().get<RPCHandlersRegistry>();

        reg.globalReqHandlers[rpcId] = [f = std::move(fn)](entt::entity e, const BYTE* data, size_t size, uint32 requestId) mutable
            {
                if (const auto* table = RPCGetVerifiedRoot<Table>(data, size))
                    f(e, *table, requestId);
            };
    }

    template<typename T>
    inline void RPCRegisterResponseNative(entt::registry& R, entt::entity e, std::function<void(entt::entity, const T&, uint32)> fn)
    {
        using Table = RPCWireTableT<T>;

        if constexpr (IsFlatBufferNativeV<T>)
        {
            RPCRegisterResponse<Table>(
                R,
                e,
                [f = std::move(fn)](entt::entity e, const Table& table, uint32 requestId) mutable
                {
                    T obj;
                    table.UnPackTo(&obj);
                    f(e, obj, requestId);
                });
        }
        else
        {
            RPCRegisterResponse<T>(R, e, std::move(fn));
        }
    }

    template<typename T>
    inline void RPCRegisterResponseNative(entt::registry& R, std::function<void(entt::entity, const T&, uint32)> fn)
    {
        using Table = RPCWireTableT<T>;

        if constexpr (IsFlatBufferNativeV<T>)
        {
            RPCRegisterResponse<Table>(
                R,
                [f = std::move(fn)](entt::entity e, const Table& table, uint32 requestId) mutable
                {
                    T obj;
                    table.UnPackTo(&obj);
                    f(e, obj, requestId);
                });
        }
        else
        {
            RPCRegisterResponse<T>(R, std::move(fn));
        }
    }

    template<typename Table>
    inline void RPCRegisterResponse(entt::registry& R, entt::entity e, std::function<void(entt::entity, const Table&, uint32)> fn)
    {
        RPC::EnsureRegistry(R);

        const uint16 rpcId = RPCIdOf<Table>();
        EntityRPCKey key{ e, rpcId };
        auto& reg = R.ctx().get<RPCHandlersRegistry>();

        reg.resHandlers[key] = [f = std::move(fn)](entt::entity e, const BYTE* data, size_t size, uint32 requestId) mutable
            {
                if (const auto* table = RPCGetVerifiedRoot<Table>(data, size))
                    f(e, *table, requestId);
            };
    }

    template<typename Table>
    inline void RPCRegisterResponse(entt::registry& R, std::function<void(entt::entity, const Table&, uint32)> fn)
    {
        RPC::EnsureRegistry(R);

        const uint16 rpcId = RPCIdOf<Table>();
        auto& reg = R.ctx().get<RPCHandlersRegistry>();

        reg.globalResHandlers[rpcId] = [f = std::move(fn)](entt::entity e, const BYTE* data, size_t size, uint32 requestId) mutable
            {
                if (const auto* table = RPCGetVerifiedRoot<Table>(data, size))
                    f(e, *table, requestId);
            };
    }

    inline Packet RPCBuildFlatBufferPacket(uint16 rpcId, uint32 requestId, uint8 rpcFlags, eChannel channel, const void* flatBufferData, uint32 flatBufferSize)
    {
        if (!flatBufferData || flatBufferSize == 0)
            return {};

        const RpcHeader rpc{ .rpcId = rpcId, .requestId = requestId, .flags = rpcFlags };

        return PacketBuilder::CreateRpcPacket(&rpc, flatBufferData, flatBufferSize, PacketFlags::NONE, channel);
    }

    template<typename Table>
    Packet RPCBuildRequestPacket(const void* flatBufferData, uint32 flatBufferSize, const RPCCallOptions& opt, uint32 requestId)
    {
        if (!RPCGetVerifiedRoot<Table>(reinterpret_cast<const BYTE*>(flatBufferData), flatBufferSize))
            return {};

        return RPCBuildFlatBufferPacket(RPCIdOf<Table>(), requestId, RpcFlags::REQUEST, opt.channel, flatBufferData, flatBufferSize);
    }

    template<typename Table>
    Packet RPCBuildResponsePacket(const void* flatBufferData, uint32 flatBufferSize, uint32 requestId, eChannel channel)
    {
        if (!RPCGetVerifiedRoot<Table>(reinterpret_cast<const BYTE*>(flatBufferData), flatBufferSize))
            return {};

        return RPCBuildFlatBufferPacket(RPCIdOf<Table>(), requestId, RpcFlags::RESPONSE, channel, flatBufferData, flatBufferSize);
    }

    template<typename Req>
    Packet RPCBuildNativeRequestPacket(const Req& req, const RPCCallOptions& opt, uint32 requestId)
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto offset = Req::TableType::Pack(fbb, &req);
        fbb.Finish(offset);

        const uint8_t* bufPtr  = fbb.GetBufferPointer();
        const size_t   bufSize = fbb.GetSize();

        return RPCBuildRequestPacket<typename Req::TableType>(bufPtr, static_cast<uint32>(bufSize), opt, requestId);
    }

    template<typename Res>
    Packet RPCBuildNativeResponsePacket(const Res& response, uint32 requestId, eChannel channel)
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto offset = Res::TableType::Pack(fbb, &response);
        fbb.Finish(offset);

        const uint8_t* bufPtr  = fbb.GetBufferPointer();
        const size_t   bufSize = fbb.GetSize();

        return RPCBuildResponsePacket<typename Res::TableType>(bufPtr, static_cast<uint32>(bufSize), requestId, channel);
    }

    template<typename Req>
    void RPCCallNative(entt::entity e, const Req& req, const RPCCallOptions& opt)
    {
        auto buf = RPCBuildNativeRequestPacket(req, opt, 0);
        if (!buf.IsValid())
            return;

        (void)SendPacketToSession(e, buf);
    }

    template<typename Table>
    bool RPCCall(entt::entity e, const void* flatBufferData, uint32 flatBufferSize, const RPCCallOptions& opt)
    {
        auto buf = RPCBuildRequestPacket<Table>(flatBufferData, flatBufferSize, opt, 0);
        if (!buf.IsValid())
            return false;

        return SendPacketToSession(e, buf);
    }

    template<typename Req, typename Res>
    std::optional<Res> RPCCallAwaitNative(entt::entity e, const Req& req, const RPCCallOptions& opt)
    {
        auto& L = CurrentShardLocalChecked();
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
        if (requestId == 0)
            return std::nullopt;

        Packet pkt = RPCBuildNativeRequestPacket(req, opt, requestId);
        if (!pkt.IsValid()) return std::nullopt;

        std::optional<Res> out{};
        const auto awaitKey = (static_cast<uint64>(static_cast<uint32>(e)) << 32) | static_cast<uint64>(requestId);

        RpcState::AwaitState st{};
        st.onPayload = [&out](const BYTE* data, size_t size, Packet)
            {
                if (auto table = RPCGetVerifiedRoot<typename Res::TableType>(data, size))
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

        if (!rpcState->RegisterRequest(requestId, std::move(st)))
            return std::nullopt;

        if (!SendPacketToSession(e, pkt))
        {
            if (auto* rpcState2 = R.try_get<RpcState>(e))
            {
                rpcState2->PopRequest(requestId);
            }
            return std::nullopt;
        }

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

    template<typename ReqTable, typename ResTable>
    inline std::optional<RPCTableRef<ResTable>> RPCCallAwait(entt::entity e, const void* flatBufferData, uint32 flatBufferSize, const RPCCallOptions& opt)
    {
        auto& L = CurrentShardLocalChecked();
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
        if (requestId == 0)
            return std::nullopt;

        Packet pkt = RPCBuildRequestPacket<ReqTable>(flatBufferData, flatBufferSize, opt, requestId);
        if (!pkt.IsValid()) return std::nullopt;

        std::optional<RPCTableRef<ResTable>> out{};
        const auto awaitKey = (static_cast<uint64>(static_cast<uint32>(e)) << 32) | static_cast<uint64>(requestId);

        RpcState::AwaitState st{};
        st.onPayload = [&out](const BYTE* data, size_t size, Packet owner)
            {
                if (auto table = RPCGetVerifiedRoot<ResTable>(data, size))
                {
                    out = RPCTableRef<ResTable>{ .owner = std::move(owner), .table = table };
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

        if (!rpcState->RegisterRequest(requestId, std::move(st)))
            return std::nullopt;

        if (!SendPacketToSession(e, pkt))
        {
            if (auto* rpcState2 = R.try_get<RpcState>(e))
            {
                rpcState2->PopRequest(requestId);
            }
            return std::nullopt;
        }

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
    void RPCSendResponseNative(entt::entity e, const Res& response, uint32 requestId, eChannel channel)
    {
        auto pkt = RPCBuildNativeResponsePacket(response, requestId, channel);
        if (!pkt.IsValid()) return;

        (void)SendPacketToSession(e, pkt);
    }

    template<typename Table>
    bool RPCSendResponse(entt::entity e, const void* flatBufferData, uint32 flatBufferSize, uint32 requestId, eChannel channel)
    {
        auto pkt = RPCBuildResponsePacket<Table>(flatBufferData, flatBufferSize, requestId, channel);
        if (!pkt.IsValid()) return false;

        return SendPacketToSession(e, pkt);
    }




} // namespace jam::net
