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
        std::atomic<Mailbox*>   q{ nullptr };                       // ���� �Խõ� ť(������ nullptr)
        std::atomic<uint32>     gen{ 0 };                              // ���� ī����(ABA/��ü ����)
        std::atomic<uint32>     state{ E2U(eShardState::CLOSED) };         // � ����
    };

    struct ShardSlot
	{
        QueueSlot               ch[E2U(eMailboxChannel::COUNT)]; // ä�κ� ���� (Normal/Ctrl)
        uint32                  shardId = 0;
        // �ʿ�� padding/metrics �߰�
    };
}
