#pragma once
#include "pch.h"


namespace jam::net::ecs
{
	struct SessionRef
	{
		std::weak_ptr<Session> wp;
	};

	struct MailboxRef
	{
		std::shared_ptr<utils::exec::Mailbox> norm;
		std::shared_ptr<utils::exec::Mailbox> ctrl;
	};

	struct CompEndpoint
	{
		UdpSession* owner;
	};


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

}
