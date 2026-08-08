#pragma once

#include "jamnet/runtime/application/ClientNetworkManager.h"
#include "jamnet/runtime/application/AppRuntimeEvents.h"
#include "jamnet/runtime/world/actor/ActorActionTypes.h"

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace jam::net
{
	enum class eClientRequestKind : uint8
	{
		None,
		WorldAction,
		ActorAction,
		SocialCommand,
		ContentRequest,
	};

	enum class eClientRequestAdmission : uint8
	{
		Accepted,
		NotInitialized,
		NotConnected,
		InvalidArgument,
		QueueFull,
	};

	struct ClientRequestReceipt
	{
		ClientRequestId		requestId	= kInvalidClientRequestId;
		eClientRequestKind	kind		= eClientRequestKind::None;

		bool IsValid() const { return requestId != kInvalidClientRequestId; }
	};

	// Immediate result of submitting an asynchronous frontend request.
	// Accepted means ClientRuntime owns the request and will emit its completion later.
	struct ClientRequestSubmission
	{
		eClientRequestAdmission	admission	= eClientRequestAdmission::NotInitialized;
		ClientRequestReceipt	receipt		= {};

		bool Accepted() const { return admission == eClientRequestAdmission::Accepted; }
	};

	struct ActorActionRequestCompletedEvent
	{
		ClientRequestReceipt	receipt = {};
		ActorActionResult		result = {};
	};

	enum class eClientEventType : uint8
	{
		None = 0,
		NetworkStateChanged,
		WorldParticipantChanged,
		ActorLifecycleChanged,
		WorldRayResolved,
		ActorActionRequestCompleted,
		SocialMessageReceived,
		ContentRequestCompleted,
	};

	using ClientEventPayload = std::variant<
		NetworkStateEvent,
		WorldParticipantEvent,
		ActorLifecycleEvent,
		WorldRayResolvedEvent,
		ActorActionRequestCompletedEvent,
		SocialMessageEvent,
		GenericContentResponseEvent>;

	struct ClientEvent
	{
		eClientEventType	type	= eClientEventType::None;
		ClientEventPayload	payload	= NetworkStateEvent{};
	};

	// Pump is the only point where background client results become visible to the frontend.
	// All ClientRuntime public methods are main-thread-only, unless a later API documents otherwise.
	struct ClientPumpOptions
	{
		size_t maxControlEvents = 1024;
	};

	struct ClientPumpResult
	{
		size_t appliedControlEvents = 0;
		size_t pendingControlEvents = 0;
		bool   presentationUpdated  = false;
	};

	struct ClientFrontendState
	{
		AccountId			accountId			  = kInvalidAccountId;
		UserId				userId				  = kInvalidUserId;
		WorldRef			mainWorld			  = {};
		NetworkState		networkState		  = {};
	};


	class ClientRuntime final
	{
	public:
		explicit ClientRuntime(const ClientConfig& config = {});
		~ClientRuntime();

		bool									Connect();
		void									Disconnect();
		void									Shutdown();
		ClientPumpResult						Pump(const ClientPumpOptions& options = {});
		
		// Events are emitted after their corresponding frontend state has been applied.
		// An event is consumed only when this function returns true.
		bool									PollEvent(OUT ClientEvent& outEvent);
		
		// Frontend-thread snapshot. Values advance only when Pump() applies ingress events.
		AccountId								GetAccountId() const;
		UserId									GetUserId() const;
		WorldRef								GetMainWorldRef() const;
		NetworkState							GetNetworkState() const;
		// Returned spans remain valid until the next Pump(), Disconnect(), or destruction.
		ActorPresentationFramePairView			GetActorPresentationFramePair(WorldId worldId) const;
	

		ClientRequestSubmission					RequestWorldAction(const WorldActionCommand& command);

		// Returns local admission only. Completion is delivered through an
		// ActorActionRequestCompleted ClientEvent with the returned receipt.
		ClientRequestSubmission					RequestActorAction(const ActorActionCommand& command);
		ClientRequestSubmission					RequestSocialCommand(const SocialCommand& command);
		ClientRequestSubmission					RequestContent(const GenericContentRequest& request);
		
		
		void									SubmitCharacterControl(const CharacterControlIntent& intent);

	private:
		struct Ingress;

		void									AssertFrontendThread() const;
		
		void									HandleNetworkStateEvent(const NetworkStateEvent& evt);
		void									HandleWorldParticipantEvent(const WorldParticipantEvent& evt);
		void									HandleActorLifecycleEvent(const ActorLifecycleEvent& evt);
		void									ApplyPresentationFramePair(const std::optional<PresentationFramePushedEvent>& previous, const PresentationFramePushedEvent& current);
		void									ClearFrontendState();
		bool									OwnsEvent(AccountId accountId) const;

	private:
		std::shared_ptr<ClientNetworkManager>		m_networkManager;
		std::shared_ptr<Ingress>					m_ingress;
		std::thread::id								m_frontendThreadId;

		ClientFrontendState							m_frontendState = {};

		std::array<ActorPresentationFrame, 2>		m_snapshotBuffers = {};
		std::array<WorldId, 2>						m_snapshotBufferWorldIds = { kInvalidWorldId, kInvalidWorldId };
		uint32										m_frontSnapshotIndex = 0;
		uint64										m_snapshotSequence = 0;
		WorldId										m_snapshotWorldId = kInvalidWorldId;
		
		std::deque<ClientEvent>						m_frontendEvents;
		ClientRequestId								m_nextClientRequestId = 1;
		
		std::unordered_map<ClientRequestId, ActorActionCommand> m_pendingActorActions;
		std::unordered_map<ClientRequestId, GenericContentOperationCode> m_pendingContentRequests;
	};
}
