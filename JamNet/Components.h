#pragma once
#include <bitset>

#include "EcsHandle.h"
#include "ReliableTransportManager.h"

namespace jam::net::ecs
{
	struct SessionRef
	{
		std::weak_ptr<Session> wp;
	};

	//struct MailboxRef
	//{
	//	std::shared_ptr<utils::exec::Mailbox> norm;
	//	std::shared_ptr<utils::exec::Mailbox> ctrl;
	//};


	//struct GroupMember
	//{
	//	uint64 gid = 0;
	//	utils::exec::GroupHomeKey gk{ 0 };
	//};

	//struct SendQueue
	//{
	//	std::deque<Sptr<SendBuffer>> q;
	//};

	//struct BackpressureConfig
	//{
	//	uint64 maxQueue = 1024;
	//	bool dropWhenFull = true;
	//};

	//struct BackpressureStats
	//{
	//	uint64 dropped = 0;
	//	uint64 delayed = 0;
	//};

	//struct GroupMulticastEvent
	//{
	//	uint64 group_id;
	//	utils::job::Job j;
	//};



	struct ReliabilityState
	{
		uint16		sendSeq = 1;
		uint16		latestSeq = 0;
		uint16		expectedNextSeq = 1;
		uint32		inFlightSize = 0;

		bool		hasPendingAck = false;
		uint16		pendingAckSeq = 0;
		uint32		pendingAckBitfield = 0;
		uint64		firstPendingAckTick = 0;

		bitset<WINDOW_SIZE> receiveHistory;

		// NACK 타이밍(매 프레임 체크)
		uint64		lastNackTime = 0;

		EcsHandle	hStore = EcsHandle::invalid();
	};

	struct ReliabilityStore
	{
		std::unordered_map<uint16, PendingPacketInfo> pending; // seq -> info
		std::unordered_map<uint16, uint32> dupAckCount;
		std::unordered_set<uint16> sentNackSeqs;
		uint16 lastAckedSeq = 0;
	};
}
