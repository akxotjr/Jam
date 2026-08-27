#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/executor/MailboxRef.h"
#include "jamnet/runtime/world/lifecycle/WorldActionTypes.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"
#include "jamnet/runtime/session/ServerSession.h"
#include "jamnet/runtime/session/UserContext.h"

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>



namespace jam::net
{
	struct PacketHeaderView;
	class ServerTcpSession;
	class ServerUdpSession;

	struct WorldUserContext
	{
		AccountId				accountId	 = kInvalidAccountId;
		UserId					userId		 = 0;
		WorldStateRevision		mainRevision = 0;
		ServerSessionBundle		sessions	 = {};
	};


	class WorldBase : public IShardOwnedObject, public std::enable_shared_from_this<WorldBase>
	{
	public:
		WorldBase() = default;
		WorldBase(const WorldConfig& config);
		virtual ~WorldBase() override = default;

		bool							Initialize();

		RuntimeId						GetShardOwnedRuntimeId() const override;
		MailboxRef						GetShardOwnedMailboxRef() const override;
		bool							BeginClose(eMailboxCloseMode mode, std::function<void()> onClosed) override;

		void							Submit(Job j) const;
		bool 							Post(Job j)   const;
		MailboxRef						GetMailboxRef() const;
		bool							IsCurrentShardContext() const;
		const WorldInstanceRef&			GetWorldInstance() const { return m_config.world.instance; }
		WorldId							GetWorldId()	   const { return m_config.world.worldId; }
		WorldRef					GetWorldRef()  const { return m_config.GetWorldRef(); }

	protected:
		virtual bool					OnInitialize() { return true; }
		virtual void					OnCloseStarted() {}
		virtual void					BeginCloseBarrier(std::function<void()> completion) { completion(); }
		virtual void					OnCloseCompleted() {}

	protected:
		WorldConfig						m_config		= {};

		std::atomic<bool>				m_alive			= false;

		std::weak_ptr<ShardExecutor>	m_shard;
		MailboxRef						m_mailboxRef = {};
	};

	class WorldMembershipHost : public WorldBase
	{
	public:
		using WorldBase::WorldBase;

		virtual bool					AddMember(WorldUserContext user);
		virtual bool					RemoveMember(UserId userId);
		virtual void					UpdateMemberContext(WorldUserContext user);

		virtual void					Send(Packet packet) { (void)packet; }
		virtual void					SendTo(Packet packet, UserId userId) { (void)packet; (void)userId; }
		virtual void					Multicast(Packet packet) { (void)packet; }
		
		virtual void					HandleWorldPacket(UserId callerUserId, Packet pkt) { (void)callerUserId; (void)pkt; }

		virtual Session*				GetMemberSession(UserId userId, eProtocolType protocol);

	protected:
		bool							OnInitialize() override;
		virtual void					OnUserEntered(UserId userId) { (void)userId; }
		virtual void					OnUserLeft(UserId userId) { (void)userId; }

	protected:
		std::unordered_map<UserId, WorldUserContext>	m_userContexts;
	};
}
