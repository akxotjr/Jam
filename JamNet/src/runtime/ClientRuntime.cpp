#include "pch.h"
#include "jamnet/runtime/ClientRuntime.h"

#include <algorithm>

namespace jam::net
{
	//namespace
	//{
	//	struct PerClientPresentationRuntimeCounter
	//	{
	//		struct Entry
	//		{
	//			uint64 windowStartNs = 0;
	//			uint32 pushedFrameCount = 0;
	//			uint32 fetchedFrameCount = 0;
	//			uint32 lastPushedActorCount = 0;
	//			uint32 lastFetchedActorCount = 0;
	//			uint64 lastSequence = 0;
	//			LocalWorldId lastResolvedLocalWorldId = kInvalidLocalWorldId;
	//			LocalWorldId lastRequestedLocalWorldId = kInvalidLocalWorldId;
	//			NetWorldId lastWorldId = kInvalidNetWorldId;
	//		};

	//		void RecordPush(AccountId accountId, UserId userId, NetWorldId worldId, LocalWorldId resolvedLocalWorldId, uint64 sequence, uint32 actorCount)
	//		{
	//			if (accountId == kInvalidAccountId || userId == kInvalidUserId)
	//				return;

	//			const uint64 nowNs = NOW_NS();
	//			const uint64 key = (static_cast<uint64>(accountId) << 32) ^ static_cast<uint64>(GetRuntimeLocalIndex(userId)) ^ userId;

	//			std::lock_guard<std::mutex> guard(mutex);
	//			Entry& entry = entries[key];
	//			if (entry.windowStartNs == 0)
	//				entry.windowStartNs = nowNs;

	//			++entry.pushedFrameCount;
	//			entry.lastPushedActorCount = actorCount;
	//			entry.lastSequence = sequence;
	//			entry.lastResolvedLocalWorldId = resolvedLocalWorldId;
	//			entry.lastWorldId = worldId;

	//			FlushIfNeeded(accountId, userId, nowNs, entry);
	//		}

	//		void RecordFetch(AccountId accountId, UserId userId, LocalWorldId requestedLocalWorldId, uint64 sequence, uint32 actorCount)
	//		{
	//			if (accountId == kInvalidAccountId || userId == kInvalidUserId)
	//				return;

	//			const uint64 nowNs = NOW_NS();
	//			const uint64 key = (static_cast<uint64>(accountId) << 32) ^ static_cast<uint64>(GetRuntimeLocalIndex(userId)) ^ userId;

	//			std::lock_guard<std::mutex> guard(mutex);
	//			Entry& entry = entries[key];
	//			if (entry.windowStartNs == 0)
	//				entry.windowStartNs = nowNs;

	//			++entry.fetchedFrameCount;
	//			entry.lastFetchedActorCount = actorCount;
	//			entry.lastRequestedLocalWorldId = requestedLocalWorldId;
	//			entry.lastSequence = sequence;

	//			FlushIfNeeded(accountId, userId, nowNs, entry);
	//		}

	//		void FlushIfNeeded(AccountId accountId, UserId userId, uint64 nowNs, Entry& entry)
	//		{
	//			if ((nowNs - entry.windowStartNs) < 1_s)
	//				return;

	//			JAMNET_LOG_INFO(
	//				"[ClientRuntime] Presentation push rate. account={} user={} pushed={} fetched={} worldId={} resolvedLocalWorldId={} requestedLocalWorldId={} lastSequence={} pushedActors={} fetchedActors={} windowMs={}",
	//				accountId,
	//				userId,
	//				entry.pushedFrameCount,
	//				entry.fetchedFrameCount,
	//				entry.lastWorldId,
	//				entry.lastResolvedLocalWorldId,
	//				entry.lastRequestedLocalWorldId,
	//				entry.lastSequence,
	//				entry.lastPushedActorCount,
	//				entry.lastFetchedActorCount,
	//				(nowNs - entry.windowStartNs) / 1'000'000ull);

	//			entry.windowStartNs = nowNs;
	//			entry.pushedFrameCount = 0;
	//			entry.fetchedFrameCount = 0;
	//		}

	//		std::mutex mutex;
	//		std::unordered_map<uint64, Entry> entries;
	//	};

	//	PerClientPresentationRuntimeCounter& GetPresentationRuntimeCounter()
	//	{
	//		static PerClientPresentationRuntimeCounter counter;
	//		return counter;
	//	}
	//}

	ClientRuntime::ClientRuntime(const ClientConfig& config)
		: m_networkManager(std::make_unique<ClientNetworkManager>(config, config.accountId))
		, m_accountId(config.accountId)
	{
		m_networkState.phase = eNetworkPhase::Disconnected;

		m_networkStateSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			NetworkStateEvent,
			[this](const NetworkStateEvent& evt) { HandleNetworkStateEvent(evt); }, {});
		m_worldMembershipSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			WorldMembershipEvent,
			[this](const WorldMembershipEvent& evt) { HandleWorldMembershipEvent(evt); }, {});
		m_presentationFrameSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			PresentationFramePushedEvent,
			[this](const PresentationFramePushedEvent& evt) { HandlePresentationFramePushedEvent(evt); }, {});
	}

	ClientRuntime::~ClientRuntime() = default;

	bool ClientRuntime::Connect()
	{
		if (!m_networkManager)
			return false;

		const bool connected = m_networkManager->Connect();
		if (connected && m_networkState.phase == eNetworkPhase::Disconnected)
			m_networkState.phase = eNetworkPhase::Connecting;
		return connected;
	}

	void ClientRuntime::Disconnect()
	{
		if (m_networkManager)
			m_networkManager->Disconnect();

		m_userId = kInvalidUserId;
		m_networkState.phase = eNetworkPhase::Disconnected;
		m_memberships.clear();
		m_mainWorldId = kInvalidNetWorldId;
		{
			std::unique_lock lock(m_snapshotMutex);
			for (ActorPresentationFrame& buffer : m_snapshotBuffers)
				buffer = {};
			m_frontSnapshotIndex = 0;
			m_snapshotSequence = 0;
			m_snapshotLocalWorldId = kInvalidLocalWorldId;
		}
	}


	GlobalEventBus::Subscription ClientRuntime::SubscribeNetworkState(std::function<void(const NetworkStateEvent&)> cb, SubscribeOptions opt)
	{
		return GLOBAL_EVENTBUS_SUBSCRIBE(NetworkStateEvent, [this, cb = std::move(cb)](const NetworkStateEvent& evt)
			{
				if (cb && OwnsEvent(evt.accountId))
					cb(evt);
			}, opt);
	}

	GlobalEventBus::Subscription ClientRuntime::SubscribeWorldMembership(std::function<void(const WorldMembershipEvent&)> cb, SubscribeOptions opt)
	{
		return GLOBAL_EVENTBUS_SUBSCRIBE(WorldMembershipEvent, [this, cb = std::move(cb)](const WorldMembershipEvent& evt)
			{
				if (cb && OwnsEvent(evt.accountId))
					cb(evt);
			}, opt);
	}

	GlobalEventBus::Subscription ClientRuntime::SubscribeWorldParticipant(std::function<void(const WorldParticipantEvent&)> cb, SubscribeOptions opt)
	{
		return GLOBAL_EVENTBUS_SUBSCRIBE(WorldParticipantEvent, [this, cb = std::move(cb)](const WorldParticipantEvent& evt)
			{
				if (cb && OwnsEvent(evt.accountId))
					cb(evt);
			}, opt);
	}

	GlobalEventBus::Subscription ClientRuntime::SubscribeActorLifecycle(std::function<void(const ActorLifecycleEvent&)> cb, SubscribeOptions opt)
	{
		return GLOBAL_EVENTBUS_SUBSCRIBE(ActorLifecycleEvent, [this, cb = std::move(cb)](const ActorLifecycleEvent& evt)
			{
				if (cb && OwnsEvent(evt.accountId))
					cb(evt);
			}, opt);
	}

	GlobalEventBus::Subscription ClientRuntime::SubscribeClickMoveResolved(std::function<void(const ClickMoveResolvedEvent&)> cb, SubscribeOptions opt)
	{
		return GLOBAL_EVENTBUS_SUBSCRIBE(ClickMoveResolvedEvent, [this, cb = std::move(cb)](const ClickMoveResolvedEvent& evt)
			{
				if (cb && OwnsEvent(evt.accountId))
					cb(evt);
			}, opt);
	}

	AccountId ClientRuntime::GetAccountId() const
	{
		return m_accountId;
	}

	UserId ClientRuntime::GetUserId() const
	{
		return m_userId;
	}

	NetworkState ClientRuntime::GetNetworkState() const
	{
		return m_networkState;
	}

	std::vector<WorldMembershipView> ClientRuntime::GetWorldMemberships() const
	{
		return m_memberships;
	}

	std::optional<WorldMembershipView> ClientRuntime::GetMainWorldMembership() const
	{
		for (const WorldMembershipView& membership : m_memberships)
		{
			if (membership.key.worldId == m_mainWorldId)
				return membership;
		}
		return std::nullopt;
	}

	ActorPresentationFrameView ClientRuntime::GetActorPresentationFrame(LocalWorldId localWorldId) const
	{
		std::shared_lock lock(m_snapshotMutex);
		if (localWorldId == kInvalidLocalWorldId || localWorldId != m_snapshotLocalWorldId)
			return {};

		const ActorPresentationFrame& front = m_snapshotBuffers[m_frontSnapshotIndex];
		//GetPresentationRuntimeCounter().RecordFetch(
		//	m_accountId,
		//	m_userId,
		//	localWorldId,
		//	front.sequence,
		//	static_cast<uint32>(front.actors.size()));
		return ActorPresentationFrameView
		{
			.sequence	= front.sequence,
			.tick		= front.tick,
			.timestamp	= front.timestamp,
			.actors		= std::span<const ActorPresentationState>(front.actors.data(), front.actors.size()),
		};
	}

	ClientUdpSession* ClientRuntime::GetUdpSession() const
	{
		return m_networkManager ? m_networkManager->GetUdpSession() : nullptr;
	}

	LocalWorldId ClientRuntime::GetMainLocalWorldId() const
	{
		auto membership = GetMainWorldMembership();
		return membership.has_value() ? membership->localWorldId : kInvalidLocalWorldId;
	}

	LocalWorldId ClientRuntime::ResolveLocalWorldId(NetWorldId worldId) const
	{
		if (worldId == kInvalidNetWorldId)
			return kInvalidLocalWorldId;

		for (const WorldMembershipView& membership : m_memberships)
		{
			if (membership.key.worldId == worldId)
				return membership.localWorldId;
		}
		return kInvalidLocalWorldId;
	}

	void ClientRuntime::RequestWorldAction(eWorldAction action, const WorldKey& src, const WorldKey& target)
	{
		if (m_networkManager)
			m_networkManager->RequestWorldAction(action, src, target);
	}

	void ClientRuntime::RequestSpawnActor(const SpawnParams& params)
	{
		if (m_networkManager)
			m_networkManager->RequestSpawnActor(GetMainLocalWorldId(), params);
	}

	void ClientRuntime::RequestDespawnActor(NetId netId)
	{
		if (m_networkManager)
			m_networkManager->RequestDespawnActor(GetMainLocalWorldId(), netId);
	}

	void ClientRuntime::RequestPossessActor(NetId netId)
	{
		if (m_networkManager)
			m_networkManager->RequestPossessActor(GetMainLocalWorldId(), netId);
	}

	void ClientRuntime::RequestUnpossessActor(NetId netId)
	{
		if (m_networkManager)
			m_networkManager->RequestUnpossessActor(GetMainLocalWorldId(), netId);
	}

	void ClientRuntime::PushInput(uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch)
	{
		if (m_networkManager)
			m_networkManager->PushInput(GetMainLocalWorldId(), inputFlags, pitch, yaw, commandEpoch);
	}

	void ClientRuntime::PushInput(const px::CharacterInput& input)
	{
		if (m_networkManager)
			m_networkManager->PushInput(GetMainLocalWorldId(), input);
	}

	void ClientRuntime::SetLatestClickMoveSeq(uint64 requestSeq)
	{
		if (m_networkManager)
			m_networkManager->SetLatestClickMoveSeq(GetMainLocalWorldId(), requestSeq);
	}

	void ClientRuntime::RequestClickMove(const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw)
	{
		if (m_networkManager)
			m_networkManager->RequestClickMove(GetMainLocalWorldId(), from, dir, maxRange, requestSeq, commandEpoch, facingYaw);
	}

	void ClientRuntime::HandleNetworkStateEvent(const NetworkStateEvent& evt)
	{
		if (!OwnsEvent(evt.accountId))
			return;

		m_accountId		= evt.accountId;
		m_userId		= evt.userId;
		m_networkState	= evt.state;
		if (evt.state.phase == eNetworkPhase::Disconnected)
		{
			m_memberships.clear();
			m_mainWorldId = kInvalidNetWorldId;
			std::unique_lock lock(m_snapshotMutex);
			for (ActorPresentationFrame& buffer : m_snapshotBuffers)
				buffer = {};
			m_frontSnapshotIndex = 0;
			m_snapshotSequence = 0;
			m_snapshotLocalWorldId = kInvalidLocalWorldId;
		}
	}

	void ClientRuntime::HandleWorldMembershipEvent(const WorldMembershipEvent& evt)
	{
		if (!OwnsEvent(evt.accountId))
			return;

		m_accountId = evt.accountId;
		m_userId    = evt.userId;

		switch (evt.change)
		{
		case eWorldMembershipChange::Joined:
		case eWorldMembershipChange::Updated:
		case eWorldMembershipChange::Promoted:
		case eWorldMembershipChange::Transferred:
		{
			if (!evt.membership.IsValid())
				break;

			WorldMembershipView membership = evt.membership;
			ApplyMainWorldInvariant(membership);

			auto it = std::ranges::find_if(m_memberships, [&membership](const WorldMembershipView& existing)
			{
				return existing.key == membership.key;
			});
			if (it != m_memberships.end())
				*it = membership;
			else
				m_memberships.push_back(membership);
			break;
		}

		case eWorldMembershipChange::Left:
			if (evt.membership.key.IsValid())
			{
				std::erase_if(m_memberships, [&evt](const WorldMembershipView& membership)
					{
						return membership.key == evt.membership.key;
					});
			}
			break;
		}

		RefreshMainWorldId();
	}

	void ClientRuntime::HandlePresentationFramePushedEvent(const PresentationFramePushedEvent& evt)
	{
		if (!OwnsEvent(evt.accountId))
			return;

		m_accountId = evt.accountId;
		m_userId = evt.userId;
		const LocalWorldId resolvedLocalWorldId = ResolveLocalWorldId(evt.worldId);

		std::unique_lock lock(m_snapshotMutex);
		const uint32 backIndex = 1u - m_frontSnapshotIndex;
		ActorPresentationFrame& back = m_snapshotBuffers[backIndex];
		back = evt.frame;
		m_frontSnapshotIndex = backIndex;
		m_snapshotSequence   = evt.frame.sequence;
		m_snapshotLocalWorldId = resolvedLocalWorldId;

		//GetPresentationRuntimeCounter().RecordPush(
		//	evt.accountId,
		//	evt.userId,
		//	evt.worldId,
		//	resolvedLocalWorldId,
		//	evt.frame.sequence,
		//	static_cast<uint32>(evt.frame.actors.size()));
	}

	void ClientRuntime::ApplyMainWorldInvariant(WorldMembershipView& membership)
	{
		if (membership.role != eWorldRole::Main)
			return;

		for (WorldMembershipView& existing : m_memberships)
		{
			if (existing.key != membership.key && existing.role == eWorldRole::Main)
				existing.role = eWorldRole::Auxiliary;
		}
	}

	void ClientRuntime::RefreshMainWorldId()
	{
		m_mainWorldId = kInvalidNetWorldId;
		for (const WorldMembershipView& membership : m_memberships)
		{
			if (membership.role == eWorldRole::Main)
			{
				m_mainWorldId = membership.key.worldId;
				return;
			}
		}
	}

	bool ClientRuntime::OwnsEvent(AccountId accountId) const
	{
		return accountId != kInvalidAccountId && (m_accountId == kInvalidAccountId || m_accountId == accountId);
	}

}
