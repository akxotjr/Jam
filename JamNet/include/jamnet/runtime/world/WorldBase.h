#pragma once

#include "jamnet/core/executor/ShardOwnedObject.h"
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/executor/MailboxRef.h"
#include "jamnet/runtime/world/WorldActionTypes.h"
#include "jamnet/runtime/world/WorldShardState.h"
#include "jamnet/runtime/ServerSession.h"

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "jamnet/runtime/UserContext.h"


namespace jam::net
{
	struct PacketHeaderView;
	class ServerTcpSession;
	class ServerUdpSession;

	struct WorldUserContext
	{
		UserId					userId = 0;
		bool					joined = false;
		ServerSessionBundle		sessions = {};
	};


	class WorldBase : public IShardOwnedObject
	{
	public:
		WorldBase() = default;
		WorldBase(const WorldConfig& config);
		virtual ~WorldBase() override = default;

		virtual bool					Init();
		virtual void					Shutdown(eMailboxCloseMode mode, std::function<void()> onClosed = nullptr);

		RuntimeId						GetShardOwnedRuntimeId() const override;
		MailboxRef						GetShardOwnedMailboxRef() const override;
		bool							BeginClose(eMailboxCloseMode mode, std::function<void()> onClosed) override;

		void							Submit(Job j) const;
		bool 							Post(Job j)   const;
		MailboxRef						GetMailboxRef() const;
		bool							IsCurrentShardContext() const;

		void							SetWorldKey(const WorldKey& key) { m_config.key = key; }
		const WorldKey&					GetWorldKey() const { return m_config.key; }
		NetWorldId						GetWorldId() const { return m_config.key.worldId; }
		void							SetLocalWorldId(LocalWorldId id) { m_localWorldId = id; }
		LocalWorldId					GetLocalWorldId() const { return m_localWorldId; }

	protected:
		virtual void					FinalizeShutdown() {}

	protected:
		WorldConfig						m_config		= {};
		LocalWorldId					m_localWorldId	= kInvalidLocalWorldId;

		std::atomic<bool>				m_alive			= false;

		std::weak_ptr<ShardExecutor>	m_shard;
		MailboxRef						m_mailboxRef = {};
	};

	class WorldMembershipHost : public WorldBase
	{
	public:
		using WorldBase::WorldBase;

		bool							Init() override;
		virtual bool					AddMember(WorldUserContext user);
		virtual bool					RemoveMember(uint64 userId);

		virtual void					Send(Packet packet) { (void)packet; }
		virtual void					SendTo(Packet packet, uint64 userId) { (void)packet; (void)userId; }
		virtual void					Multicast(Packet packet) { (void)packet; }
		
		virtual void					HandleWorldPacket(uint64 callerUserId, Packet pkt) { (void)callerUserId; (void)pkt; }

		virtual void					UpdateMemberContext(WorldUserContext user);
		virtual void					RemoveMemberContext(uint64 userId);
		virtual Session*				GetMemberSession(uint64 userId, eProtocolType protocol);
		virtual void					GetMembers(OUT std::vector<uint64>& users) { users = m_users; }
		virtual uint32					GetMemberCount() const { return static_cast<uint32>(m_users.size()); }
		virtual std::shared_ptr<WorldTransferPayload>	PrepareTransferOut(const WorldTransferContext& ctx) { (void)ctx; return {}; }

		virtual bool					PrepareTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) { (void)ctx; (void)payload; return true; }
		virtual void					CommitTransferOut(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) { (void)ctx; (void)payload; }
		virtual void					RollbackTransferOut(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) { (void)ctx; (void)payload; }
		virtual void					CommitTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) { (void)ctx; (void)payload; }
		virtual void					RollbackTransferIn(const WorldTransferContext& ctx, const std::shared_ptr<WorldTransferPayload>& payload) { (void)ctx; (void)payload; }

	protected:
		virtual void					OnUserJoined(uint64 userId) { (void)userId; }
		virtual void					OnUserLeft(uint64 userId) { (void)userId; }

	protected:
		std::unordered_map<uint64, WorldUserContext>	m_userContexts;
		std::vector<uint64>								m_users;
	};
}
