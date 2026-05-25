#pragma once

#include "jamnet/core/executor/GlobalEventBus.h"
#include "jamnet/runtime/ClientNetworkManager.h"
#include "jamnet/runtime/AppRuntimeEvents.h"
#include "jamnet/runtime/IClientNetworkView.h"
#include "jamnet/runtime/IClientNetworkController.h"

#include <array>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace jam::net
{
	class ClientRuntime final : public IClientNetworkView, public IClientNetworkController
	{
	public:
		explicit ClientRuntime(const ClientConfig& config = {});
		~ClientRuntime() override;

		bool									Connect();
		void									Disconnect();

		GlobalEventBus::Subscription			SubscribeNetworkState(std::function<void(const NetworkStateEvent&)> cb, SubscribeOptions opt = {});
		GlobalEventBus::Subscription			SubscribeWorldMembership(std::function<void(const WorldMembershipEvent&)> cb, SubscribeOptions opt = {});
		GlobalEventBus::Subscription			SubscribeWorldParticipant(std::function<void(const WorldParticipantEvent&)> cb, SubscribeOptions opt = {});
		GlobalEventBus::Subscription			SubscribeActorLifecycle(std::function<void(const ActorLifecycleEvent&)> cb, SubscribeOptions opt = {});
		GlobalEventBus::Subscription			SubscribeClickMoveResolved(std::function<void(const ClickMoveResolvedEvent&)> cb, SubscribeOptions opt = {});

		AccountId								GetAccountId() const override;
		UserId									GetUserId() const override;
		NetworkState							GetNetworkState() const override;
		std::vector<WorldMembershipView>		GetWorldMemberships() const override;
		std::optional<WorldMembershipView>		GetMainWorldMembership() const override;
		ActorPresentationFrameView				GetActorPresentationFrame(LocalWorldId localWorldId) const override;
		ClientUdpSession*						GetUdpSession() const;
	
		LocalWorldId							GetMainLocalWorldId() const;

		void									RequestWorldAction(eWorldAction action, const WorldKey& src = {}, const WorldKey& target = {}) override;
		void									RequestSpawnActor(const SpawnParams& params) override;
		void									RequestDespawnActor(NetId netId) override;
		void									RequestPossessActor(NetId netId) override;
		void									RequestUnpossessActor(NetId netId) override;
		void									PushInput(uint32 inputFlags, float pitch, float yaw, uint32 commandEpoch) override;
		void									PushInput(const px::CharacterInput& input) override;
		void									SetLatestClickMoveSeq(uint64 requestSeq) override;
		void									RequestClickMove(const px::Vec3& from, const px::Vec3& dir, float maxRange, uint64 requestSeq, uint32 commandEpoch, float facingYaw) override;

	private:
		LocalWorldId							ResolveLocalWorldId(NetWorldId worldId) const;
		void									HandleNetworkStateEvent(const NetworkStateEvent& evt);
		void									HandleWorldMembershipEvent(const WorldMembershipEvent& evt);
		void									HandlePresentationFramePushedEvent(const PresentationFramePushedEvent& evt);
		void									ApplyMainWorldInvariant(WorldMembershipView& membership);
		void									RefreshMainWorldId();
		bool									OwnsEvent(AccountId accountId) const;

	private:
		std::unique_ptr<ClientNetworkManager>		m_networkManager;
		GlobalEventBus::Subscription				m_networkStateSubscription;
		GlobalEventBus::Subscription				m_worldMembershipSubscription;
		GlobalEventBus::Subscription				m_presentationFrameSubscription;

		AccountId									m_accountId    = kInvalidAccountId;
		UserId										m_userId       = kInvalidUserId;
		NetworkState								m_networkState = {};
		std::vector<WorldMembershipView>			m_memberships;
		NetWorldId									m_mainWorldId  = kInvalidNetWorldId;

		mutable std::shared_mutex					m_snapshotMutex;
		std::array<ActorPresentationFrame, 2>		m_snapshotBuffers = {};
		uint32										m_frontSnapshotIndex = 0;
		uint64										m_snapshotSequence = 0;
		LocalWorldId								m_snapshotLocalWorldId = kInvalidLocalWorldId;
	};
}
