#include "pch.h"
#include "UnityClientCore.h"

#include "jamnet/core/executor/MainExecutor.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace
{
	using namespace jam;

	JAM_eNetworkPhase ToUnity(net::eNetworkPhase phase)
	{
		return static_cast<JAM_eNetworkPhase>(phase);
	}

	JAM_Vec3 ToUnity(const px::Vec3& value)
	{
		return { .x = value.x, .y = value.y, .z = value.z };
	}

	JAM_Quat ToUnity(const px::Quat& value)
	{
		return { .x = value.x, .y = value.y, .z = value.z, .w = value.w };
	}

	px::Vec3 ToPx(const JAM_Vec3& value)
	{
		return { value.x, value.y, value.z };
	}

	px::Quat ToPx(const JAM_Quat& value)
	{
		return { value.x, value.y, value.z, value.w };
	}

	JAM_eResult ToUnity(net::eClientRequestAdmission admission)
	{
		switch (admission)
		{
		case net::eClientRequestAdmission::Accepted:		return JAM_eResult::Ok;
		case net::eClientRequestAdmission::NotInitialized:	return JAM_eResult::NotInitialized;
		case net::eClientRequestAdmission::NotConnected:	return JAM_eResult::NotConnected;
		case net::eClientRequestAdmission::InvalidArgument: return JAM_eResult::InvalidArgument;

		default: return JAM_eResult::InternalError;
		}
	}

	JAM_ClientRequestSubmission ToUnity(const net::ClientRequestSubmission& submission)
	{
		return {
			.admission = static_cast<JAM_eClientRequestAdmission>(submission.admission),
			.receipt = { 
				.requestId	= submission.receipt.requestId, 
				.kind		= static_cast<JAM_eClientRequestKind>(submission.receipt.kind) 
			},
		};
	}
}

bool UnityClientCore::Initialize(const JAM_ClientConfig& config)
{
	if (m_runtime)
		return true;

	net::RuntimeConfig runtimeConfig{};
	runtimeConfig.geConfig.autoTune  = true;
	runtimeConfig.geConfig.layoutCfg = { .mode = Balance, .reservedThreads = 1, .profile = CoreProfileClient };

	m_netRuntime = std::make_unique<net::NetRuntime>(runtimeConfig);

	net::ClientConfig clientConfig{};
	clientConfig.accountId		  = config.accountId;
	clientConfig.serverTcpAddress = net::NetAddress(config.serverIp ? config.serverIp : "127.0.0.1", config.tcpPort);
	clientConfig.serverUdpAddress = net::NetAddress(config.serverIp ? config.serverIp : "127.0.0.1", config.udpPort);

	const std::filesystem::path sharedDataManifestPath = config.sharedDataManifestPath ? config.sharedDataManifestPath : "";
	clientConfig.sharedDataManifestPath = sharedDataManifestPath.empty()
		? std::string{}
		: std::filesystem::absolute(sharedDataManifestPath).lexically_normal().string();
	clientConfig.headlessMode = config.headlessMode != 0;

	m_runtime = std::make_unique<net::ClientRuntime>(clientConfig);
	return true;
}

void UnityClientCore::Shutdown()
{
	if (m_runtime)
		m_runtime->Shutdown();
	m_runtime.reset();
	m_eventPayload.clear();
	m_netRuntime.reset();
}

bool UnityClientCore::Connect()
{
	return m_runtime && m_runtime->Connect();
}

void UnityClientCore::Disconnect()
{
	if (m_runtime)
		m_runtime->Disconnect();
}

JAM_eResult UnityClientCore::Pump(const JAM_ClientPumpOptions* options, JAM_ClientPumpResult& outResult)
{
	outResult = { .structSize = sizeof(JAM_ClientPumpResult) };
	if (!m_runtime)
		return JAM_eResult::NotInitialized;

	MAIN_EXEC.PumpOnce();
	net::ClientPumpOptions runtimeOptions{};
	if (options)
		runtimeOptions.maxControlEvents = static_cast<size_t>(options->maxControlEvents);

	const net::ClientPumpResult result = m_runtime->Pump(runtimeOptions);
	outResult.appliedControlEvents = result.appliedControlEvents;
	outResult.pendingControlEvents = result.pendingControlEvents;
	outResult.presentationUpdated  = result.presentationUpdated ? 1 : 0;

	return JAM_eResult::Ok;
}

JAM_eResult UnityClientCore::GetNetworkState(JAM_NetworkState& outState) const
{
	outState = {};
	if (!m_runtime)
		return JAM_eResult::NotInitialized;
	outState.phase = ToUnity(m_runtime->GetNetworkState().phase);
	return JAM_eResult::Ok;
}

JAM_eResult UnityClientCore::GetAccountId(uint64_t& outAccountId) const
{
	outAccountId = 0;
	if (!m_runtime)
		return JAM_eResult::NotInitialized;
	outAccountId = m_runtime->GetAccountId();

	return JAM_eResult::Ok;
}

JAM_eResult UnityClientCore::GetUserId(uint64_t& outUserId) const
{
	outUserId = 0;
	if (!m_runtime)
		return JAM_eResult::NotInitialized;
	outUserId = m_runtime->GetUserId();

	return JAM_eResult::Ok;
}

JAM_eResult UnityClientCore::GetMainWorldRef(JAM_WorldRuntimeRef& outWorldRef) const
{
	outWorldRef = {};
	if (!m_runtime)
		return JAM_eResult::NotInitialized;

	const net::WorldRuntimeRef world = m_runtime->GetMainWorldRef();
	outWorldRef = {
		.worldId		   = world.worldId, 
		.worldInstanceId   = world.instance.instanceId.value, 
		.worldArchetypeKey = world.instance.archetypeKey.v
	};
	
	return JAM_eResult::Ok;
}

JAM_eResult UnityClientCore::GetActorPresentationFramePair(uint64_t worldId, JAM_ActorState* outPreviousActors, int32_t previousCapacity, JAM_ActorFrame* outPreviousFrame, JAM_ActorState* outCurrentActors, int32_t currentCapacity, JAM_ActorFrame* outCurrentFrame, JAM_FrameCopyInfo& outInfo) const
{
	outInfo = { .structSize = sizeof(JAM_FrameCopyInfo) };
	if (!m_runtime)
		return JAM_eResult::NotInitialized;
	if (!outPreviousFrame || !outCurrentFrame || previousCapacity < 0 || currentCapacity < 0 || (previousCapacity > 0 && !outPreviousActors) || (currentCapacity > 0 && !outCurrentActors))
		return JAM_eResult::InvalidArgument;

	const net::ActorPresentationFramePairView pair = m_runtime->GetActorPresentationFramePair(worldId);
	outInfo.previousCopiedCount   = CopyFrameView(pair.previous, outPreviousActors, previousCapacity, outPreviousFrame);
	outInfo.currentCopiedCount    = CopyFrameView(pair.current, outCurrentActors, currentCapacity, outCurrentFrame);
	outInfo.previousRequiredCount = outPreviousFrame->actorCount;
	outInfo.currentRequiredCount  = outCurrentFrame->actorCount;

	return outInfo.previousCopiedCount < outInfo.previousRequiredCount || outInfo.currentCopiedCount < outInfo.currentRequiredCount ? JAM_eResult::BufferTooSmall : JAM_eResult::Ok;
}

JAM_eResult UnityClientCore::RequestWorldAction(const JAM_WorldActionCommand& command, JAM_ClientRequestSubmission& outSubmission)
{
	outSubmission = {};
	if (!m_runtime)
		return JAM_eResult::NotInitialized;
	if (command.structSize != sizeof(JAM_WorldActionCommand))
		return JAM_eResult::VersionMismatch;

	net::WorldActionCommand runtimeCommand{};
	if (command.kind == JAM_eWorldActionKind::Enter)
	{
		runtimeCommand.payload = net::EnterWorldRequest{
			.archetypeKey			= { command.enter.worldArchetypeKey },
			.selector				= static_cast<net::eWorldDestinationSelector>(command.enter.selector),
			.explicitInstanceId		= { command.enter.explicitWorldInstanceId },
			.destinationName		= command.enter.destinationName ? command.enter.destinationName : "",
			.expectedMainRevision	= command.enter.expectedMainRevision,
		};
	}
	else if (command.kind == JAM_eWorldActionKind::Leave)
	{
		runtimeCommand.payload = net::LeaveWorldRequest{ .expectedMainRevision = command.leave.expectedMainRevision };
	}
	else
	{
		return JAM_eResult::InvalidArgument;
	}

	const net::ClientRequestSubmission submission = m_runtime->RequestWorldAction(runtimeCommand);
	outSubmission = ToUnity(submission);

	return ToUnity(submission.admission);
}

JAM_eResult UnityClientCore::RequestActorAction(const JAM_ActorActionCommand& command, JAM_ClientRequestSubmission& outSubmission)
{
	outSubmission = {};
	if (!m_runtime)
		return JAM_eResult::NotInitialized;
	if (command.structSize != sizeof(JAM_ActorActionCommand))
		return JAM_eResult::VersionMismatch;

	net::ActorActionCommand runtimeCommand{};
	runtimeCommand.worldId = command.worldId;
	if (command.action == JAM_eActorAction::Spawn)
	{
		net::FrontendSpawnActorSpec spec{};
		spec.actorArchetypeKey = { command.spawn.actorArchetypeKey };
		spec.pose = { .p = ToPx(command.spawn.position), .q = ToPx(command.spawn.rotation) };
		spec.team = command.spawn.team;
		spec.part = command.spawn.part;
		spec.role = command.spawn.role;
		spec.requestOwnership = command.spawn.requestOwnership != 0;
		spec.requestControl = command.spawn.requestControl != 0;
		spec.targetActorId = net::ActorId(command.spawn.targetActorId);
		if (command.spawn.overrideMask & ACTOR_SPAWN_OVERRIDE_LINEAR_VELOCITY) spec.linearVelocity = ToPx(command.spawn.linearVelocity);
		if (command.spawn.overrideMask & ACTOR_SPAWN_OVERRIDE_ANGULAR_VELOCITY) spec.angularVelocity = ToPx(command.spawn.angularVelocity);
		if (command.spawn.overrideMask & ACTOR_SPAWN_OVERRIDE_LINEAR_DAMPING) spec.linearDamping = command.spawn.linearDamping;
		if (command.spawn.overrideMask & ACTOR_SPAWN_OVERRIDE_ANGULAR_DAMPING) spec.angularDamping = command.spawn.angularDamping;
		if (command.spawn.overrideMask & ACTOR_SPAWN_OVERRIDE_VIEW_YAW) spec.viewYaw = command.spawn.viewYaw;
		if (command.spawn.overrideMask & ACTOR_SPAWN_OVERRIDE_VIEW_PITCH) spec.viewPitch = command.spawn.viewPitch;
		runtimeCommand.payload = net::SpawnActorRequest{ .spec = std::move(spec) };
	}
	else if (command.action == JAM_eActorAction::Despawn)
	{
		runtimeCommand.payload = net::DespawnActorRequest{ .actorId = net::ActorId(command.targetActorId) };
	}
	else
	{
		return JAM_eResult::InvalidArgument;
	}

	const net::ClientRequestSubmission submission = m_runtime->RequestActorAction(runtimeCommand);
	outSubmission = ToUnity(submission);
	return ToUnity(submission.admission);
}

JAM_eResult UnityClientCore::RequestSocialCommand(const JAM_SocialCommand& command, JAM_ClientRequestSubmission& outSubmission)
{
	outSubmission = {};
	if (!m_runtime)
		return JAM_eResult::NotInitialized;
	if (command.structSize != sizeof(JAM_SocialCommand))
		return JAM_eResult::VersionMismatch;
	if (command.destination.audience > JAM_eSocialAudience::Global
		|| (command.payloadSize > 0 && !command.payload))
		return JAM_eResult::InvalidArgument;

	net::SocialCommand runtimeCommand{
		.destination = {
			.audience = static_cast<net::eSocialAudience>(command.destination.audience),
			.scopeId = command.destination.scopeId,
		},
		.contentType = command.contentType,
	};
	runtimeCommand.payload.resize(command.payloadSize);
	if (!runtimeCommand.payload.empty())
		std::memcpy(runtimeCommand.payload.data(), command.payload, command.payloadSize);

	const net::ClientRequestSubmission submission = m_runtime->RequestSocialCommand(runtimeCommand);
	outSubmission = ToUnity(submission);
	return ToUnity(submission.admission);
}

JAM_eResult UnityClientCore::SubmitCharacterControl(const JAM_CharacterControlIntent& intent)
{
	if (!m_runtime)
		return JAM_eResult::NotInitialized;
	if (intent.structSize != sizeof(JAM_CharacterControlIntent))
		return JAM_eResult::VersionMismatch;

	net::CharacterControlIntent runtimeIntent{
		.moveReferenceYaw	= intent.moveReferenceYaw,
		.viewYaw			= intent.viewYaw,
		.viewPitch			= intent.viewPitch,
		.viewPolicy		= static_cast<net::eCharacterViewPolicy>(intent.viewPolicy),
		.continuousActions	= intent.continuousActions,
		.edgeActions		= intent.edgeActions,
	};
	switch (intent.locomotion)
	{
	case JAM_eCharacterLocomotionKind::Stop: runtimeIntent.locomotion = net::StopMovementIntent{}; break;
	case JAM_eCharacterLocomotionKind::Directional: runtimeIntent.locomotion = net::DirectionalMoveIntent{ .localX = intent.vector.x, .localY = intent.vector.y }; break;
	case JAM_eCharacterLocomotionKind::WorldRay: runtimeIntent.locomotion = net::MoveByWorldRayIntent{ .rayOrigin = ToPx(intent.rayOrigin), .rayDirection = ToPx(intent.rayDirection), .maxRange = intent.maxRange }; break;
	case JAM_eCharacterLocomotionKind::Position: runtimeIntent.locomotion = net::MoveToPositionIntent{ .target = ToPx(intent.vector) }; break;
	case JAM_eCharacterLocomotionKind::FollowActor: runtimeIntent.locomotion = net::FollowActorIntent{ .target = net::ActorId(intent.targetActorId) }; break;
	default: return JAM_eResult::InvalidArgument;
	}
	m_runtime->SubmitCharacterControl(runtimeIntent);
	return JAM_eResult::Ok;
}

JAM_eResult UnityClientCore::PollEvent(JAM_ClientEvent& outEvent)
{
	outEvent = { .structSize = sizeof(JAM_ClientEvent), .type = JAM_eClientEventType::None };
	m_eventPayload.clear();
	if (!m_runtime)
		return JAM_eResult::NotInitialized;

	net::ClientEvent event{};
	if (!m_runtime->PollEvent(event))
		return JAM_eResult::NoEvent;

	switch (event.type)
	{
	case net::eClientEventType::NetworkStateChanged:
	{
		const auto& value = std::get<net::NetworkStateEvent>(event.payload);
		outEvent.type = JAM_eClientEventType::NetworkStateChanged;
		outEvent.payload.networkStateChanged = { .accountId = value.accountId, .userId = value.userId, .state = { .phase = ToUnity(value.state.phase) } };
		break;
	}
	case net::eClientEventType::WorldParticipantChanged:
	{
		const auto& value = std::get<net::WorldParticipantEvent>(event.payload);
		outEvent.type = JAM_eClientEventType::WorldParticipantChanged;
		outEvent.payload.worldParticipantChanged = { .accountId = value.accountId, .userId = value.userId, .change = static_cast<JAM_eWorldParticipantChange>(value.change), .world = { .worldId = value.participant.runtime.worldId, .worldInstanceId = value.participant.runtime.instance.instanceId.value, .worldArchetypeKey = value.participant.runtime.instance.archetypeKey.v }, .participantUserId = value.participant.participantUserId };
		break;
	}
	case net::eClientEventType::ActorLifecycleChanged:
	{
		const auto& value = std::get<net::ActorLifecycleEvent>(event.payload);
		outEvent.type = JAM_eClientEventType::ActorLifecycleChanged;
		outEvent.payload.actorLifecycleChanged = { .accountId = value.accountId, .userId = value.userId, .worldId = value.worldId, .clientRequestId = value.clientRequestId, .actorId = value.actorId.Value(), .isLocal = value.isLocal ? 1 : 0, .reason = static_cast<JAM_eActorLifecycleReason>(value.reason), .actorArchetypeKey = value.actorArchetypeKey.v };
		break;
	}
	case net::eClientEventType::WorldRayResolved:
	{
		const auto& value = std::get<net::WorldRayResolvedEvent>(event.payload);
		outEvent.type = JAM_eClientEventType::WorldRayResolved;
		outEvent.payload.worldRayResolved = { .accountId = value.accountId, .userId = value.userId, .worldId = value.worldId, .hit = value.hit ? 1 : 0, .position = ToUnity(value.position), .normal = ToUnity(value.normal), .hitActorId = value.hitActorId.Value() };
		break;
	}
	case net::eClientEventType::ActorActionRequestCompleted:
	{
		const auto& value = std::get<net::ActorActionRequestCompletedEvent>(event.payload);
		outEvent.type = JAM_eClientEventType::ActorActionRequestCompleted;
		outEvent.payload.actorActionRequestCompleted = { .receipt = { .requestId = value.receipt.requestId, .kind = static_cast<JAM_eClientRequestKind>(value.receipt.kind) }, .action = static_cast<JAM_eActorAction>(value.result.action), .status = static_cast<JAM_eActorActionStatus>(value.result.status), .reason = static_cast<JAM_eActorActionReason>(value.result.reason), .actorId = value.result.actorId.Value() };
		break;
	}
	case net::eClientEventType::SocialMessageReceived:
	{
		const auto& value = std::get<net::SocialMessageEvent>(event.payload);
		m_eventPayload.resize(value.message.payload.size());
		if (!m_eventPayload.empty())
			std::memcpy(m_eventPayload.data(), value.message.payload.data(), m_eventPayload.size());

		outEvent.type = JAM_eClientEventType::SocialMessageReceived;
		outEvent.payload.socialMessageReceived = {
			.accountId = value.accountId,
			.userId = value.userId,
			.messageId = value.message.messageId,
			.senderUserId = value.message.sender,
			.destination = {
				.audience = static_cast<JAM_eSocialAudience>(value.message.destination.audience),
				.scopeId = value.message.destination.scopeId,
			},
			.contentType = value.message.contentType,
			.payload = m_eventPayload.empty() ? nullptr : m_eventPayload.data(),
			.payloadSize = static_cast<uint32_t>(m_eventPayload.size()),
		};
		break;
	}
	case net::eClientEventType::None:
	default:
		return JAM_eResult::NoEvent;
	}

	return JAM_eResult::Ok;
}

int32_t UnityClientCore::CopyFrameView(const net::ActorPresentationFrameView& frame, JAM_ActorState* outActors, int32_t actorCapacity, JAM_ActorFrame* outFrame)
{
	if (!outFrame)
		return 0;
	outFrame->sequence = frame.sequence;
	outFrame->tick = frame.tick;
	outFrame->timestamp = frame.timestamp;
	outFrame->actorCount = static_cast<int32_t>(frame.actors.size());
	if (!outActors || actorCapacity <= 0)
		return outFrame->actorCount;

	const int32_t copyCount = std::min<int32_t>(actorCapacity, outFrame->actorCount);
	for (int32_t i = 0; i < copyCount; ++i)
	{
		const net::ActorPresentationState& actor = frame.actors[static_cast<size_t>(i)];
		JAM_ActorState& output = outActors[i];
		output = { .actorId = actor.actorId.Value(), .isLocal = actor.isLocal ? 1 : 0 };
		if (actor.cs)
		{
			output.hasTransform = 1;
			output.position = ToUnity(actor.cs->pos);
			output.rotation = ToUnity(px::Quat::FromYawPitch(actor.cs->bodyYaw, 0.0f));
		}
		else if (actor.rs)
		{
			output.hasTransform = 1;
			output.position = ToUnity(actor.rs->pose.p);
			output.rotation = ToUnity(actor.rs->pose.q);
		}
	}
	return copyCount;
}
