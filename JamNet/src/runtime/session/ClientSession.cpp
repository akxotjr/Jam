#include "pch.h"
#include "jamnet/runtime/session/ClientSession.h"


#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/runtime/application/ClientNetworkManager.h"
#include "jamnet/runtime/world/simulation/client/ClientWorld.h"

#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/schema/gen/world_assignment_generated.h"
#include "jamnet/runtime/protocol/schema/gen/social_command_generated.h"
#include "jamnet/runtime/protocol/schema/gen/social_message_generated.h"

namespace jam::net
{
	namespace
	{
		Packet MakeBarrierResultPacket(WireBarrierToken token, bool succeeded, eWorldTransitionFailure failure)
		{
			flatbuffers::FlatBufferBuilder fbb(64);
			const auto root = fb::CreatefbClientBarrierResult(fbb, token.value, succeeded, static_cast<fb::fbWorldTransitionFailure>(failure));
			fbb.Finish(root);

			return PacketBuilder::CreateCustomPacket(CustomPacketId::CLIENT_BARRIER_RESULT, PacketFlags::NONE, eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize());
		}


	} // anonymous namespace

	bool ClientTcpSession::RequestEnterWorld(const EnterWorldRequest& request)
	{
		if (!m_manager || !request.IsValid())
			return false;
		flatbuffers::FlatBufferBuilder fbb(160);
		flatbuffers::Offset<flatbuffers::String> destination;
		if (!request.destinationName.empty())
			destination = fbb.CreateString(request.destinationName);
		const auto root = fb::CreatefbEnterWorldRequest(fbb, request.requestId, request.archetypeKey.v, static_cast<fb::fbWorldDestinationSelector>(request.selector), request.explicitInstanceId.value, destination, request.expectedMainRevision);
		fbb.Finish(root);
		Send(PacketBuilder::CreateCustomPacket(CustomPacketId::ENTER_WORLD_REQUEST, PacketFlags::NONE, eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize()));
		return true;
	}

	bool ClientTcpSession::RequestLeaveWorld(const LeaveWorldRequest& request)
	{
		if (!m_manager)
			return false;
		flatbuffers::FlatBufferBuilder fbb(64);
		const auto root = fb::CreatefbLeaveWorldRequest(fbb, request.requestId, request.expectedMainRevision);
		fbb.Finish(root);
		Send(PacketBuilder::CreateCustomPacket(CustomPacketId::LEAVE_WORLD_REQUEST, PacketFlags::NONE, eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize()));
		return true;
	}

	bool ClientTcpSession::SendSocialCommand(const SocialCommand& command)
	{
		if (!m_manager || command.requestId == kInvalidClientRequestId)
			return false;

		flatbuffers::FlatBufferBuilder fbb(128 + command.payload.size());
		const fb::fbSocialAddress destination(
			static_cast<fb::fbSocialAudience>(command.destination.audience),
			command.destination.scopeId);
		const auto payload = fbb.CreateVector(
			reinterpret_cast<const uint8*>(command.payload.data()), command.payload.size());
		const auto root = fb::CreatefbSocialCommand(
			fbb, command.requestId, &destination, command.contentType, payload);
		fb::FinishfbSocialCommandBuffer(fbb, root);
		Send(PacketBuilder::CreateCustomPacket(CustomPacketId::SOCIAL_COMMAND, PacketFlags::NONE,
			eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize()));
		return true;
	}

	void ClientTcpSession::OnLinkEstablished()
	{
		if (m_manager)
			m_manager->AssertPrincipalAffinity();
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ClientTcpSession established. ip: {} | port: {}",
			GetAccountId(),
			GetUserId(),
			GetRemoteNetAddress().GetIpAddress(),
			GetRemoteNetAddress().GetPort());

		if (m_manager)
			m_manager->NotifyTcpBound(GetUserId());
	}

	void ClientTcpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ClientTcpSession disconnected", GetAccountId(), GetUserId());
		if (m_manager)
			m_manager->NotifyTcpDisconnected(this);
	}

	void ClientTcpSession::HandleCustomPacket(Packet packet)
	{
		if (m_manager)
			m_manager->AssertPrincipalAffinity();
		auto view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid() || !m_manager)
			return;

		if (view.Id() == CustomPacketId::WORLD_TRANSITION_RESULT)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			const auto* result = flatbuffers::GetRoot<fb::fbWorldTransitionResult>(view.Payload());
			if (!result || !result->Verify(verifier))
				return;
			if (result->failure() != fb::fbWorldTransitionFailure_None)
				JAMNET_LOG_WARN("World transition failed. kind={}, requestId={}, reason={}",
					static_cast<uint32>(result->kind()), result->request_id(), static_cast<uint32>(result->failure()));
			return;
		}

		if (view.Id() == CustomPacketId::CLIENT_WORLD_PREPARE)
		{
			JAMNET_LOG_INFO("[ClientWorldPrepare] received. payloadSize={}", view.PayloadSize());

			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			const auto* wire = flatbuffers::GetRoot<fb::fbClientWorldPrepare>(view.Payload());
			if (!wire || !wire->Verify(verifier))
			{
				JAMNET_LOG_WARN("[ClientWorldPrepare] verification failed. payloadSize={}", view.PayloadSize());
				return;
			}
			JAMNET_LOG_INFO("[ClientWorldPrepare] verified. token={}, instance={}, runtime={}",
				wire->barrier_token(), wire->instance_id(), wire->runtime_id());

			ClientWorldPrepare prepare
			{
				.token = { wire->barrier_token() },
				.kind = static_cast<eWireBarrierKind>(wire->kind()),
				.correlation =
				{
					.world =
					{
						.instance = { .instanceId = WorldInstanceId{ wire->instance_id() }, .archetypeKey = WorldArchetypeKey{ wire->archetype_key() } },
						.worldId = wire->runtime_id(),
					},
					.mainRevision = wire->main_revision(),
				},
				.archetypeKey = WorldArchetypeKey{ wire->archetype_key() },
				.contentRevision = wire->content_revision(),
			};

			const SessionId		 sessionId  = GetSessionId();
			const EndpointHandle endpoint   = GetEndpointHandle();
			const uint32		 generation = GetServiceGeneration();
			auto service = GetServiceRef();

			m_manager->PrepareMainWorld(prepare, [service, sessionId, endpoint, generation, token = prepare.token](bool succeeded)
				{
					JAMNET_LOG_INFO("[ClientWorldPrepare] completed. token={}, succeeded={}", token.value, succeeded);

					auto* self = service ? static_cast<ClientTcpSession*>(service->FindOwnedSession(sessionId, endpoint, generation)) : nullptr;
					if (!self)
					{
						JAMNET_LOG_WARN("[ClientWorldPrepare] barrier response dropped; TCP session lookup failed. token={}, sessionId={}, generation={}",
							token.value, sessionId, generation);
						return;
					}
					self->Send(MakeBarrierResultPacket(token, succeeded,
						succeeded ? eWorldTransitionFailure::None : eWorldTransitionFailure::ClientPrepareFailed));
				});
			return;
		}

		if (view.Id() == CustomPacketId::CLIENT_WORLD_COMMIT)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			const auto* wire = flatbuffers::GetRoot<fb::fbClientWorldCommit>(view.Payload());
			if (!wire || !wire->Verify(verifier))
				return;
			WorldRuntimeRef world{};
			if (const auto& prepared = m_manager->GetMainWorldState().Prepared(); prepared)
				world = prepared->world;
			ClientWorldCommit commit
			{
				.token		 = { wire->barrier_token() },
				.correlation = { .world = world, .mainRevision = wire->main_revision() },
			};
			const bool wireMatches = world.instance.instanceId == WorldInstanceId{ wire->instance_id() }
				&& world.worldId == wire->runtime_id();
			const bool succeeded = wireMatches && m_manager->CommitMainWorld(commit);

			Send(MakeBarrierResultPacket(commit.token, succeeded, succeeded ? eWorldTransitionFailure::None : eWorldTransitionFailure::ClientPrepareFailed));
			return;
		}

		if (view.Id() == CustomPacketId::USER_MAIN_WORLD_CHANGED)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			const auto* changed = flatbuffers::GetRoot<fb::fbUserMainPhysicalWorldChanged>(view.Payload());
			if (!changed || !changed->Verify(verifier) || !changed->state())
				return;
			UserPhysicalWorldState state{ .revision = changed->state()->revision() };
			if (const auto* main = changed->state()->main())
				state.main = WorldRuntimeRef
				{
					.instance = { .instanceId = WorldInstanceId{ main->instance_id() }, .archetypeKey = WorldArchetypeKey{ main->archetype_key() } },
					.worldId = main->runtime_id(),
				};
			m_manager->ApplyMainWorldChanged(state);
			return;
		}

		if (view.Id() == CustomPacketId::SOCIAL_EVENT)
		{
			flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
			if (!fb::VerifyfbSocialMessageBuffer(verifier))
				return;

			const auto* wire = fb::GetfbSocialMessage(view.Payload());
			const auto* destination = wire ? wire->destination() : nullptr;
			const auto* payload = wire ? wire->payload() : nullptr;
			if (!wire || !destination || !payload
				|| destination->audience() < fb::fbSocialAudience_MIN
				|| destination->audience() > fb::fbSocialAudience_MAX)
				return;

			SocialMessage message{
				.messageId = wire->message_id(),
				.sender = wire->sender(),
				.destination = {
					.audience = static_cast<eSocialAudience>(destination->audience()),
					.scopeId = destination->scope_id(),
				},
				.contentType = wire->content_type(),
			};
			message.payload.resize(payload->size());
			if (!message.payload.empty())
				std::memcpy(message.payload.data(), payload->data(), payload->size());
			m_manager->PublishSocialMessage(std::move(message));
			return;
		}

		const WorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidWorldId)
			return;

		m_manager->DispatchWorldPacket(GetUserId(), worldId, std::move(packet));
	}





	void ClientUdpSession::OnLinkEstablished()
	{
		if (m_manager)
			m_manager->AssertPrincipalAffinity();
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ClientUdpSession established. ip: {} | port: {}",
			GetAccountId(),
			GetUserId(),
			GetRemoteNetAddress().GetIpAddress(),
			GetRemoteNetAddress().GetPort());

		if (m_manager)
			m_manager->NotifyUdpBound(GetUserId());
	}

	void ClientUdpSession::OnDisconnected()
	{
		JAMNET_LOG_INFO("[AccountId = {}, UserId = {}] ClientUdpSession disconnected", GetAccountId(), GetUserId());
		if (m_manager)
			m_manager->NotifyUdpDisconnected(this);
	}

	void ClientUdpSession::HandleCustomPacket(Packet packet)
	{
		if (!m_manager)
			return;
		m_manager->AssertPrincipalAffinity();

		auto view = PacketHeaderView::Parse(packet->Head(), packet->Size());
		if (!view.IsValid())
			return;

		const WorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidWorldId)
		{
			if (view.Id() == CustomPacketId::SNAPSHOT || view.Id() == CustomPacketId::LIFECYCLE)
			{
				JAMNET_LOG_WARN(
					"[ClientUdpSession] Invalid scoped world id. account={} user={} packetId={} payloadSize={}",
					GetAccountId(),
					GetUserId(),
					static_cast<uint32>(view.Id()),
					view.PayloadSize());
			}
			return;
		}

		m_manager->DispatchWorldPacket(GetUserId(), worldId, std::move(packet));
	}


}
