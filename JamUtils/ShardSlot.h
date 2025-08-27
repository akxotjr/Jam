#pragma once

namespace jam::utils::exec
{
    class Mailbox;

    enum class eMailboxChannel : uint8
    {
	    NORMAL      = 0,
    	CTRL        = 1,
    	COUNT
    };

    enum class eShardState : uint32
    {
	    CLOSED      = 0,
    	OPEN        = 1,
    	DRAINING    = 2,
    	DEAD        = 3
    };

    struct QueueSlot
	{
        std::atomic<Mailbox*>   q{ nullptr };                       // 현재 게시된 큐(없으면 nullptr)
        std::atomic<uint32>     gen{ 0 };                              // 세대 카운터(ABA/교체 감지)
        std::atomic<uint32>     state{ E2U(eShardState::CLOSED) };         // 운영 상태
    };

    struct ShardSlot
	{
        QueueSlot               ch[E2U(eMailboxChannel::COUNT)]; // 채널별 슬롯 (Normal/Ctrl)
        uint32                  shardId = 0;
        // 필요시 padding/metrics 추가
    };
}
