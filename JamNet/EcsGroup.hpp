#pragma once
#include "pch.h"
#include "EcsCommon.hpp"         
#include "EcsTransportAPI.hpp"   
#include "RoutingPolicy.h"
#include "PacketBuilder.h"       

namespace jam::net::ecs
{
    struct RouteCtx
    {
        jam::utils::exec::RoutingPolicy* routing = nullptr;
    };

    // 샤드 로컬 그룹 인덱스
    struct GroupIndex
    {
        std::unordered_map<uint64, xvector<entt::entity>> members;
    };

    struct CompGroupMembership
    {
        xuset<uint64> groups;
    };

    struct EvGpJoin
    {
        entt::entity    e{ entt::null };
        uint64          groupId{};
    };

    struct EvGpLeave
    {
        entt::entity    e{ entt::null };
        uint64          groupId{};
    };

    // 멀티캐스트 요청(호출자 샤드에서 실행)
    struct EvGpPostSend
    {
        entt::entity     e{ entt::null };
        uint64           groupId{};
        Sptr<SendBuffer> buf;
        eTxReason        reason{ eTxReason::NORMAL };
    };

    // 샤드 로컬 멀티캐스트 처리 이벤트(각 샤드에서 수신)
    struct EvGpPostSendLocal
    {
        uint64           groupId{};
        Sptr<SendBuffer> buf;     // 원본 템플릿(각 수신자마다 복제)
        eTxReason        reason{ eTxReason::NORMAL };
    };

    inline Sptr<SendBuffer> CloneSendBuffer(const Sptr<SendBuffer>& src)
    {
        if (!src || !src->Buffer()) return {};
        const uint32 sz = src->WriteSize();
        auto dup = jam::utils::memory::MakeShared<SendBuffer>(sz);
        ::memcpy(dup->Buffer(), src->Buffer(), sz);
        dup->Close(sz);
        return dup;
    }

    struct GroupHandlers
    {
        entt::registry* R{};

        jam::utils::exec::GroupHomeKey HomeKey(uint64 gid) const
        {
            if (auto* rc = R->ctx().find<RouteCtx>())
                if (rc && rc->routing) 
                    return rc->routing->KeyForGroup(gid);

            return jam::utils::exec::GroupHomeKey{ gid };
        }

        void OnJoin(const EvGpJoin& ev)
        {
            auto&& [ep, gm] = R->get<CompEndpoint, CompGroupMembership>(ev.e);
            const auto gk = HomeKey(ev.groupId);
            if (ep.owner) ep.owner->JoinGroup(ev.groupId, gk);
            gm.groups.insert(ev.groupId);

            if (auto* gi = R->ctx().find<GroupIndex>())
            {
                if (!gi) gi = &R->ctx().emplace<GroupIndex>();
                gi->members[ev.groupId].push_back(ev.e);
            }
        }

        void OnLeave(const EvGpLeave& ev)
        {
            auto&& [ep, gm] = R->get<CompEndpoint, CompGroupMembership>(ev.e);
            const auto gk = HomeKey(ev.groupId);
            if (ep.owner) ep.owner->LeaveGroup(ev.groupId, gk);
            gm.groups.erase(ev.groupId);

            if (auto* gi = R->ctx().find<GroupIndex>())
            {
                if (!gi) gi = &R->ctx().emplace<GroupIndex>();
                auto& vec = gi->members[ev.groupId];
                std::erase(vec, ev.e);
                if (vec.empty()) 
                    gi->members.erase(ev.groupId);
            }
        }

        // 원본 샤드에서 호출 → 그룹홈 경유하여 각 샤드로 전파
        void OnPostSend(const EvGpPostSend& ev)
        {
            auto& ep = R->get<CompEndpoint>(ev.e);
            if (!ep.owner || !ev.buf || !ev.buf->Buffer()) return;
            const auto gk = HomeKey(ev.groupId);

            // 각 샤드 Mailbox에서 실행될 Job: Local 이벤트 enqueue
            auto buf = ev.buf;
            auto reason = ev.reason;
            auto gid = ev.groupId;
            ep.owner->PostGroup(gid, gk, jam::utils::job::Job([&reg = *R, gid, buf, reason]() mutable {
                reg.ctx().get<entt::dispatcher>().enqueue<EvGpPostSendLocal>(EvGpPostSendLocal{ gid, buf, reason });
            }));
        }

        // 샤드 로컬: 실제 멤버 엔티티 순회 → EnqueueSend
        void OnPostSendLocal(const EvGpPostSendLocal& ev)
        {
            if (auto* gi = R->ctx().find<GroupIndex>())
            {
                if (!gi) return;

                auto it = gi->members.find(ev.groupId);
                if (it == gi->members.end()) return;

                for (auto target : it->second)
                {
                    auto dup = CloneSendBuffer(ev.buf); // 각 대상마다 복제
                    if (!dup) continue;
                    EnqueueSend(*R, target, dup, ev.reason);
                }
            }
        }
    };

    struct GroupSinks
    {
        bool                    wired = false;
        entt::scoped_connection onJoin;
        entt::scoped_connection onLeave;
        entt::scoped_connection onPostSend;
        entt::scoped_connection onPostSendLocal;
        GroupHandlers           handlers;
    };

    inline void GroupWiringSystem(jam::utils::exec::ShardLocal& L, uint64, uint64)
    {
        auto& R = L.world;
    	auto& D = L.events;
        auto& s = R.ctx().emplace<GroupSinks>();
        if (s.wired) return;
        s.handlers.R = &R;

    	R.ctx().emplace<GroupIndex>();  // todo: 여기서 R.ctx 에 주입하는게 맞는가?

        s.onJoin            = D.sink<EvGpJoin>().connect<&GroupHandlers::OnJoin>(&s.handlers);
        s.onLeave           = D.sink<EvGpLeave>().connect<&GroupHandlers::OnLeave>(&s.handlers);
        s.onPostSend        = D.sink<EvGpPostSend>().connect<&GroupHandlers::OnPostSend>(&s.handlers);
        s.onPostSendLocal   = D.sink<EvGpPostSendLocal>().connect<&GroupHandlers::OnPostSendLocal>(&s.handlers);

        s.wired = true;
    }
}
