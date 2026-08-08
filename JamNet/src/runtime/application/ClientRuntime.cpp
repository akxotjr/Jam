#include "pch.h"
#include "jamnet/runtime/application/ClientRuntime.h"

#include "jamnet/core/executor/GlobalEventBus.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <deque>
#include <mutex>
#include <optional>
#include <type_traits>
#include <memory>

namespace jam::net
{

	struct ClientRuntime::Ingress
	{
		struct PresentationPair
		{
			std::optional<PresentationFramePushedEvent> previous;
			std::optional<PresentationFramePushedEvent> current;
		};

		explicit Ingress(AccountId ownerAccountId, uint64 ownerClientInstanceId)
			: accountId(ownerAccountId), clientInstanceId(ownerClientInstanceId)
		{
		}

		bool Begin()
		{
			std::scoped_lock lock(mutex);
			if (closed)
				return false;
			if (active)
				return true;

			active = true;
			controlEvents.clear();
			presentation = {};
			return true;
		}

		void Stop()
		{
			std::scoped_lock lock(mutex);
			active = false;
			controlEvents.clear();
			presentation = {};
		}

		void Close()
		{
			{
				std::scoped_lock lock(mutex);
				closed = true;
				active = false;
				controlEvents.clear();
				presentation = {};
			}

			networkStateSubscription.reset();
			worldParticipantSubscription.reset();
			actorLifecycleSubscription.reset();
			worldRayResolvedSubscription.reset();
			actorActionResultSubscription.reset();
			socialMessageSubscription.reset();
			contentResponseSubscription.reset();
			presentationFrameSubscription.reset();
		}

		template <typename T>
		void EnqueueControl(eClientEventType type, const T& event)
		{
			std::scoped_lock lock(mutex);
			if (!Accepts(event.accountId))
				return;

			controlEvents.push_back(ClientEvent{ .type = type, .payload = event });
		}

		void EnqueueNetworkState(const NetworkStateEvent& event)
		{
			std::scoped_lock lock(mutex);
			if (closed || !active)
				return;
			if (event.clientInstanceId != clientInstanceId)
				return;
			if (accountId != kInvalidAccountId
				&& event.accountId != kInvalidAccountId
				&& accountId != event.accountId)
				return;
			if (accountId == kInvalidAccountId && event.accountId != kInvalidAccountId)
				accountId = event.accountId;

			controlEvents.push_back(ClientEvent{
				.type = eClientEventType::NetworkStateChanged,
				.payload = event,
			});
		}

		void EnqueueActorActionResult(const ActorActionResultEvent& event)
		{
			std::scoped_lock lock(mutex);
			if (!Accepts(event.accountId))
				return;

			controlEvents.push_back(ClientEvent
				{
					.type	 = eClientEventType::ActorActionRequestCompleted,
					.payload = ActorActionRequestCompletedEvent
					{
						.receipt = ClientRequestReceipt
						{
							.requestId = event.requestId,
							.kind	   = eClientRequestKind::ActorAction,
						},
						.result = event.result,
					},
				});
		}

		void EnqueuePresentation(const PresentationFramePushedEvent& event)
		{
			std::scoped_lock lock(mutex);
			if (!Accepts(event.accountId))
				return;

			if (!presentation.current.has_value()
				|| presentation.current->worldId != event.worldId)
			{
				presentation.previous.reset();
			}
			else
			{
				presentation.previous = std::move(presentation.current);
			}

			presentation.current = event;
		}

		std::vector<ClientEvent> DrainControlEvents(size_t maxCount, OUT size_t& outPendingCount)
		{
			std::scoped_lock lock(mutex);
			const size_t count = std::min(maxCount, controlEvents.size());
			std::vector<ClientEvent> events;
			events.reserve(count);
			for (size_t i = 0; i < count; ++i)
			{
				events.push_back(std::move(controlEvents.front()));
				controlEvents.pop_front();
			}
			outPendingCount = controlEvents.size();
			return events;
		}

		std::optional<PresentationPair> TakePresentationPair()
		{
			std::scoped_lock lock(mutex);
			if (!presentation.current.has_value())
				return std::nullopt;

			PresentationPair result = std::move(presentation);
			presentation = {};
			return result;
		}

	private:
		bool Accepts(AccountId eventAccountId) const
		{
			return !closed
				&& active
				&& eventAccountId != kInvalidAccountId
				&& (accountId == kInvalidAccountId || accountId == eventAccountId);
		}

	public:
		AccountId			accountId = kInvalidAccountId;
		const uint64		clientInstanceId = 0;
		std::mutex				mutex;
		bool					active = false;
		bool					closed = false;
		std::deque<ClientEvent> controlEvents;
		PresentationPair		presentation = {};

		GlobalEventBus::Subscription networkStateSubscription;
		GlobalEventBus::Subscription worldParticipantSubscription;
		GlobalEventBus::Subscription actorLifecycleSubscription;
		GlobalEventBus::Subscription worldRayResolvedSubscription;
		GlobalEventBus::Subscription actorActionResultSubscription;
		GlobalEventBus::Subscription socialMessageSubscription;
		GlobalEventBus::Subscription contentResponseSubscription;
		GlobalEventBus::Subscription presentationFrameSubscription;
	};

	ClientRuntime::ClientRuntime(const ClientConfig& config)
		: m_networkManager(std::make_shared<ClientNetworkManager>(config))
		, m_ingress(std::make_shared<Ingress>(config.accountId, m_networkManager->GetClientInstanceId()))
		, m_frontendThreadId(std::this_thread::get_id())
	{
		m_frontendState.accountId = config.accountId;
		m_frontendState.networkState.phase = eNetworkPhase::Disconnected;

		const std::weak_ptr<Ingress> ingress = m_ingress;
		m_ingress->networkStateSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			NetworkStateEvent,
			[ingress](const NetworkStateEvent& evt)
			{
				if (const auto state = ingress.lock())
					state->EnqueueNetworkState(evt);
			}, {});
		m_ingress->worldParticipantSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			WorldParticipantEvent,
			[ingress](const WorldParticipantEvent& evt)
			{
				if (const auto state = ingress.lock())
					state->EnqueueControl(eClientEventType::WorldParticipantChanged, evt);
			}, {});
		m_ingress->actorLifecycleSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			ActorLifecycleEvent,
			[ingress](const ActorLifecycleEvent& evt)
			{
				if (const auto state = ingress.lock())
					state->EnqueueControl(eClientEventType::ActorLifecycleChanged, evt);
			}, {});
		m_ingress->worldRayResolvedSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			WorldRayResolvedEvent,
			[ingress](const WorldRayResolvedEvent& evt)
			{
				if (const auto state = ingress.lock())
					state->EnqueueControl(eClientEventType::WorldRayResolved, evt);
			}, {});
		m_ingress->actorActionResultSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			ActorActionResultEvent,
			[ingress](const ActorActionResultEvent& evt)
			{
				if (const auto state = ingress.lock())
					state->EnqueueActorActionResult(evt);
			}, {});
		m_ingress->socialMessageSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			SocialMessageEvent,
			[ingress](const SocialMessageEvent& evt)
			{
				if (const auto state = ingress.lock())
					state->EnqueueControl(eClientEventType::SocialMessageReceived, evt);
			}, {});
		m_ingress->contentResponseSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			GenericContentResponseEvent,
			[ingress](const GenericContentResponseEvent& evt)
			{
				if (const auto state = ingress.lock())
					state->EnqueueControl(eClientEventType::ContentRequestCompleted, evt);
			}, {});
		m_ingress->presentationFrameSubscription = GLOBAL_EVENTBUS_SUBSCRIBE(
			PresentationFramePushedEvent,
			[ingress](const PresentationFramePushedEvent& evt)
			{
				if (const auto state = ingress.lock())
					state->EnqueuePresentation(evt);
			}, {});
	}

	ClientRuntime::~ClientRuntime()
	{
		Shutdown();
	}

	bool ClientRuntime::Connect()
	{
		AssertFrontendThread();
		if (!m_networkManager)
			return false;
		if (m_frontendState.networkState.phase != eNetworkPhase::Disconnected)
			return true;
		if (!m_ingress || !m_ingress->Begin())
			return false;

		const bool admitted = m_networkManager->Connect();
		if (!admitted)
		{
			m_ingress->Stop();
			return false;
		}

		return true;
	}

	void ClientRuntime::Disconnect()
	{
		AssertFrontendThread();
		if (m_frontendState.networkState.phase == eNetworkPhase::Disconnected)
			return;
		if (m_ingress)
			m_ingress->Stop();

		if (m_networkManager)
			m_networkManager->Disconnect();

		ClearFrontendState();
		m_frontendEvents.push_back(ClientEvent{
			.type	 = eClientEventType::NetworkStateChanged,
			.payload = NetworkStateEvent
			{
				.accountId = m_frontendState.accountId,
				.userId	   = kInvalidUserId,
				.state	   = m_frontendState.networkState,
			},
		});
	}

	void ClientRuntime::Shutdown()
	{
		if (m_ingress)
			m_ingress->Close();

		if (m_networkManager)
		{
			m_networkManager->Shutdown();
			m_networkManager.reset();
		}

		ClearFrontendState();
		m_frontendEvents.clear();
	}

	ClientPumpResult ClientRuntime::Pump(const ClientPumpOptions& options)
	{
		AssertFrontendThread();

		ClientPumpResult result{};
		if (!m_ingress)
			return result;

		std::vector<ClientEvent> controlEvents = m_ingress->DrainControlEvents(options.maxControlEvents, result.pendingControlEvents);
		for (const ClientEvent& event : controlEvents)
		{
			switch (event.type)
			{
			case eClientEventType::NetworkStateChanged:
				HandleNetworkStateEvent(std::get<NetworkStateEvent>(event.payload));
				break;

			case eClientEventType::WorldParticipantChanged:
				HandleWorldParticipantEvent(std::get<WorldParticipantEvent>(event.payload));
				break;

			case eClientEventType::ActorLifecycleChanged:
				HandleActorLifecycleEvent(std::get<ActorLifecycleEvent>(event.payload));
				break;

			case eClientEventType::WorldRayResolved:
				m_frontendEvents.push_back(event);
				break;

			case eClientEventType::ActorActionRequestCompleted:
				m_pendingActorActions.erase(std::get<ActorActionRequestCompletedEvent>(event.payload).receipt.requestId);
				m_frontendEvents.push_back(event);
				break;

			case eClientEventType::SocialMessageReceived:
				m_frontendEvents.push_back(event);
				break;

			case eClientEventType::ContentRequestCompleted:
			{
				const auto& response = std::get<GenericContentResponseEvent>(event.payload).response;
				const auto pending = m_pendingContentRequests.find(response.requestId);
				if (pending != m_pendingContentRequests.end() && pending->second == response.opCode)
				{
					m_pendingContentRequests.erase(pending);
					m_frontendEvents.push_back(event);
				}
				break;
			}

			case eClientEventType::None:
			default:
				break;
			}
		}
		result.appliedControlEvents = controlEvents.size();

		if (auto presentation = m_ingress->TakePresentationPair())
		{
			ApplyPresentationFramePair(presentation->previous, *presentation->current);
			result.presentationUpdated = true;
		}

		return result;
	}

	AccountId ClientRuntime::GetAccountId() const
	{
		AssertFrontendThread();
		return m_frontendState.accountId;
	}

	UserId ClientRuntime::GetUserId() const
	{
		AssertFrontendThread();
		return m_frontendState.userId;
	}

	WorldRef ClientRuntime::GetMainWorldRef() const
	{
		AssertFrontendThread();
		return m_frontendState.mainWorld;
	}

	NetworkState ClientRuntime::GetNetworkState() const
	{
		AssertFrontendThread();
		return m_frontendState.networkState;
	}

	ActorPresentationFramePairView ClientRuntime::GetActorPresentationFramePair(WorldId worldId) const
	{
		AssertFrontendThread();
		if (worldId == kInvalidWorldId || worldId != m_snapshotWorldId)
			return {};

		const uint32 currentIndex  = m_frontSnapshotIndex;
		const uint32 previousIndex = 1u - currentIndex;

		ActorPresentationFramePairView result{};
		const ActorPresentationFrame& current = m_snapshotBuffers[currentIndex];
		result.current = ActorPresentationFrameView
		{
			.sequence	= current.sequence,
			.tick		= current.tick,
			.timestamp	= current.timestamp,
			.actors		= std::span<const ActorPresentationState>(current.actors.data(), current.actors.size()),
		};

		if (m_snapshotBufferWorldIds[previousIndex] == worldId)
		{
			const ActorPresentationFrame& previous = m_snapshotBuffers[previousIndex];
			result.previous = ActorPresentationFrameView
			{
				.sequence	= previous.sequence,
				.tick		= previous.tick,
				.timestamp	= previous.timestamp,
				.actors		= std::span<const ActorPresentationState>(previous.actors.data(), previous.actors.size()),
			};
		}

		return result;
	}

	bool ClientRuntime::PollEvent(ClientEvent& outEvent)
	{
		AssertFrontendThread();
		if (m_frontendEvents.empty())
			return false;

		outEvent = std::move(m_frontendEvents.front());
		m_frontendEvents.pop_front();
		return true;
	}

	ClientRequestSubmission ClientRuntime::RequestWorldAction(const WorldActionCommand& command)
	{
		AssertFrontendThread();
		if (!m_networkManager)
			return {};
		if (m_frontendState.networkState.phase != eNetworkPhase::Ready)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::NotConnected };

		WorldActionCommand resolved = command;
		const bool valid = std::visit([](const auto& request)
			{
				using Request = std::decay_t<decltype(request)>;
				if constexpr (std::is_same_v<Request, EnterWorldRequest>)
					return request.IsValid();
				return true;
			}, resolved.payload);
		if (!valid)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::InvalidArgument };

		ClientRequestId requestId = std::visit([this](auto& request)
			{
				if (request.requestId == 0)
					request.requestId = m_nextClientRequestId++;
				return static_cast<ClientRequestId>(request.requestId);
			}, resolved.payload);

		if (requestId == kInvalidClientRequestId)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::QueueFull };
		if (!m_networkManager->RequestWorldAction(resolved))
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::QueueFull };

		return ClientRequestSubmission
		{
			.admission = eClientRequestAdmission::Accepted,
			.receipt = { .requestId = requestId, .kind = eClientRequestKind::WorldAction },
		};
	}

	ClientRequestSubmission ClientRuntime::RequestActorAction(const ActorActionCommand& command)
	{
		AssertFrontendThread();
		if (!m_networkManager)
			return {};
		if (m_frontendState.networkState.phase != eNetworkPhase::Ready)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::NotConnected };

		ActorActionCommand resolved = command;

		const ClientRequestId requestId = std::visit([this](auto& request) -> ClientRequestId
			{
				if (request.requestId == kInvalidClientRequestId)
					request.requestId = m_nextClientRequestId++;
				return request.requestId;
			}, resolved.payload);
		if (requestId == kInvalidClientRequestId)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::QueueFull };

		if (!m_networkManager->RequestActorAction(resolved))
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::QueueFull };

		m_pendingActorActions.emplace(requestId, resolved);
		return ClientRequestSubmission
		{
			.admission = eClientRequestAdmission::Accepted,
			.receipt   = ClientRequestReceipt{ .requestId = requestId, .kind = eClientRequestKind::ActorAction },
		};
	}

	ClientRequestSubmission ClientRuntime::RequestSocialCommand(const SocialCommand& command)
	{
		AssertFrontendThread();
		if (!m_networkManager)
			return {};
		if (m_frontendState.networkState.phase != eNetworkPhase::Ready)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::NotConnected };

		SocialCommand resolved = command;
		if (resolved.requestId == kInvalidClientRequestId)
			resolved.requestId = m_nextClientRequestId++;
		if (resolved.requestId == kInvalidClientRequestId)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::QueueFull };
		if (resolved.destination.audience > eSocialAudience::Global)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::InvalidArgument };
		if (resolved.destination.recipient.kind > eSocialRecipientKind::CharacterName)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::InvalidArgument };
		if (!m_networkManager->RequestSocialCommand(resolved))
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::QueueFull };

		return ClientRequestSubmission{
			.admission = eClientRequestAdmission::Accepted,
			.receipt = {
				.requestId = resolved.requestId,
				.kind = eClientRequestKind::SocialCommand,
			},
		};
	}

	ClientRequestSubmission ClientRuntime::RequestContent(const GenericContentRequest& request)
	{
		constexpr size_t kMaxPendingContentRequests = 64;
		AssertFrontendThread();
		if (!m_networkManager)
			return {};
		if (m_frontendState.networkState.phase != eNetworkPhase::Ready)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::NotConnected };
		if (m_pendingContentRequests.size() >= kMaxPendingContentRequests)
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::QueueFull };

		GenericContentRequest resolved = request;
		if (resolved.requestId == kInvalidClientRequestId)
			resolved.requestId = m_nextClientRequestId++;
		if (!resolved.IsValid())
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::InvalidArgument };
		if (m_pendingContentRequests.contains(resolved.requestId))
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::InvalidArgument };
		if (!m_networkManager->RequestGenericContent(resolved))
			return ClientRequestSubmission{ .admission = eClientRequestAdmission::QueueFull };

		m_pendingContentRequests.emplace(resolved.requestId, resolved.opCode);
		return ClientRequestSubmission{
			.admission = eClientRequestAdmission::Accepted,
			.receipt = {
				.requestId = resolved.requestId,
				.kind = eClientRequestKind::ContentRequest,
			},
		};
	}

	void ClientRuntime::SubmitCharacterControl(const CharacterControlIntent& intent)
	{
		AssertFrontendThread();
		if (!m_networkManager || m_frontendState.networkState.phase != eNetworkPhase::Ready)
			return;

		auto isFiniteVec3 = [](const px::Vec3& value)
			{
				return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
			};
		if (!std::isfinite(intent.moveReferenceYaw) || !std::isfinite(intent.viewYaw) || !std::isfinite(intent.viewPitch))
			return;
		if (intent.viewPolicy != eCharacterViewPolicy::FollowMovement
			&& intent.viewPolicy != eCharacterViewPolicy::Explicit)
			return;

		const bool valid = std::visit([&isFiniteVec3](const auto& locomotion)
			{
				using Locomotion = std::decay_t<decltype(locomotion)>;
				if constexpr (std::is_same_v<Locomotion, DirectionalMoveIntent>)
					return std::isfinite(locomotion.localX)
						&& std::isfinite(locomotion.localY)
						&& std::abs(locomotion.localX) <= 1.0f
						&& std::abs(locomotion.localY) <= 1.0f;
				if constexpr (std::is_same_v<Locomotion, MoveByWorldRayIntent>)
					return isFiniteVec3(locomotion.rayOrigin)
						&& isFiniteVec3(locomotion.rayDirection)
						&& locomotion.rayDirection.MagnitudeSquared() > 0.0f
						&& std::isfinite(locomotion.maxRange)
						&& locomotion.maxRange > 0.0f;
				if constexpr (std::is_same_v<Locomotion, MoveToPositionIntent>)
					return isFiniteVec3(locomotion.target);
				if constexpr (std::is_same_v<Locomotion, FollowActorIntent>)
					return locomotion.target.IsValid();
				return true;
			}, intent.locomotion);
		if (!valid)
			return;

		m_networkManager->SubmitCharacterControl(intent);
	}

	void ClientRuntime::HandleNetworkStateEvent(const NetworkStateEvent& evt)
	{
		if (evt.accountId == kInvalidAccountId
			? m_frontendState.accountId != kInvalidAccountId
			: !OwnsEvent(evt.accountId))
			return;

		m_frontendState.accountId	 = evt.accountId;
		m_frontendState.userId		 = evt.userId;
		m_frontendState.networkState = evt.state;
		if (evt.state.phase == eNetworkPhase::Disconnected)
			ClearFrontendState();

		m_frontendEvents.push_back(ClientEvent{
			.type	 = eClientEventType::NetworkStateChanged,
			.payload = evt,
		});

	}

	void ClientRuntime::HandleWorldParticipantEvent(const WorldParticipantEvent& evt)
	{
		if (!OwnsEvent(evt.accountId))
			return;

		m_frontendState.accountId = evt.accountId;
		m_frontendState.userId	  = evt.userId;
		if (evt.participant.participantUserId == evt.userId)
		{
			if (evt.change == eWorldParticipantChange::Joined)
			{
				m_frontendState.mainWorld = evt.participant.world;
			}
			else if (m_frontendState.mainWorld.worldId == evt.participant.world.worldId)
			{
				m_frontendState.mainWorld = {};
			}
		}
		m_frontendEvents.push_back(ClientEvent{
			.type	 = eClientEventType::WorldParticipantChanged,
			.payload = evt,
		});
	}

	void ClientRuntime::HandleActorLifecycleEvent(const ActorLifecycleEvent& evt)
	{
		if (!OwnsEvent(evt.accountId))
			return;

		m_frontendState.accountId = evt.accountId;
		m_frontendState.userId	  = evt.userId;
		m_frontendEvents.push_back(ClientEvent{
			.type	 = eClientEventType::ActorLifecycleChanged,
			.payload = evt,
		});
	}

	void ClientRuntime::ApplyPresentationFramePair(
		const std::optional<PresentationFramePushedEvent>& previous,
		const PresentationFramePushedEvent& current)
	{
		if (!OwnsEvent(current.accountId))
			return;

		m_frontendState.accountId = current.accountId;
		m_frontendState.userId    = current.userId;
		if (current.worldId == kInvalidWorldId)
			return;

		constexpr uint32 previousIndex = 0;
		constexpr uint32 currentIndex = 1;

		m_snapshotBuffers[previousIndex] = {};
		m_snapshotBufferWorldIds.fill(kInvalidWorldId);

		if (previous.has_value()
			&& previous->worldId == current.worldId
			&& previous->frame.sequence < current.frame.sequence)
		{
			m_snapshotBuffers[previousIndex] = previous->frame;
			m_snapshotBufferWorldIds[previousIndex] = current.worldId;
		}

		m_snapshotBuffers[currentIndex] = current.frame;
		m_snapshotBufferWorldIds[currentIndex] = current.worldId;
		m_frontSnapshotIndex = currentIndex;
		m_snapshotSequence   = current.frame.sequence;
		m_snapshotWorldId = current.worldId;
	}

	void ClientRuntime::ClearFrontendState()
	{
		m_frontendState.userId = kInvalidUserId;
		m_frontendState.mainWorld = {};
		m_frontendState.networkState.phase = eNetworkPhase::Disconnected;
		for (ActorPresentationFrame& buffer : m_snapshotBuffers)
			buffer = {};
		m_snapshotBufferWorldIds.fill(kInvalidWorldId);
		m_frontSnapshotIndex = 0;
		m_snapshotSequence = 0;
		m_snapshotWorldId = kInvalidWorldId;
		m_frontendEvents.clear();
		m_pendingContentRequests.clear();
	}

	void ClientRuntime::AssertFrontendThread() const
	{
		assert(m_frontendThreadId == std::this_thread::get_id()
			&& "ClientRuntime public API must be called from its frontend thread");
	}

	bool ClientRuntime::OwnsEvent(AccountId accountId) const
	{
		return accountId != kInvalidAccountId
			&& (m_frontendState.accountId == kInvalidAccountId || m_frontendState.accountId == accountId);
	}

}
