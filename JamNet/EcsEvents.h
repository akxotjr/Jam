#pragma once

namespace jam::net::ecs
{
    struct EvRecvAck
    {
        entt::entity   e{ entt::null };
        uint16         latestSeq{};
        uint32         bitfield{};
    };












    //struct EvRecvPing
    //{
    //    entt::entity e{ entt::null };
    //    uint64       clientSendTick{};
    //};

    //struct EvRecvPong
    //{
    //    entt::entity e{ entt::null };
    //    uint64       clientSendTick{};
    //    uint64       serverSendTick{};
    //};

    //struct EvRecvFragment
    //{
    //    entt::entity e{ entt::null };
    //    const uint8* data{ nullptr };
    //    uint32       size{ 0 };
    //    // �ʿ� �� fragment meta (seq, idx, total...) �߰�
    //};

    //struct EvAssembledPayload
    //{
    //    entt::entity   e{ entt::null };
    //    std::vector<uint8> payload; // ū ���۴� move ĸó ����
    //    uint8          pktId{};
    //    uint8          pktType{};
    //};

    //struct EvSendRequest
    //{
    //    entt::entity           e{ entt::null };
    //    std::shared_ptr<SendBuffer> buf;
    //};
}

