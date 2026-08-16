#include "pch.h"
#include "jamnet/runtime/session/ClientSession.h"


#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/runtime/application/ClientNetworkManager.h"
#include "jamnet/runtime/world/simulation/client/ClientWorld.h"

#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/protocol/codec/WorldCodec.h"
#include "jamnet/runtime/protocol/codec/SocialCodec.h"
#include "jamnet/runtime/protocol/codec/ContentCodec.h"

namespace jam::net
{
	bool ClientTcpSession::RequestEnterWorld(const EnterWorldRequest& request)
	{
		if (!m_manager || !request.IsValid())
			return false;
		Send(codec::MakeEnterWorldRequestPacket(request));
		return true;
	}

	bool ClientTcpSession::RequestLeaveWorld(const LeaveWorldRequest& request)
	{
		if (!m_manager)
			return false;
		Send(codec::MakeLeaveWorldRequestPacket(request));
		return true;
	}

	bool ClientTcpSession::SendSocialCommand(const SocialCommand& command)
	{
		if (!m_manager || command.requestId == kInvalidClientRequestId)
			return false;

		Send(codec::MakeSocialCommandPacket(command));
		return true;
	}

	bool ClientTcpSession::SendGenericContentRequest(const GenericContentRequest& request)
	{
		if (!m_manager || !request.IsValid())
			return false;
		Send(codec::MakeContentRequestPacket(request));
		return true;
	}

	void ClientTcpSession::OnLinkEstablished()
	{
		if (m_manager)
			m_manager->AssertPrincipalAffinity();
		JAM_LOG_INFO("[AccountId = {}, UserId = {}] ClientTcpSession established. ip: {} | port: {}",
			GetAccountId(),
			GetUserId(),
			GetRemoteNetAddress().GetIpAddress(),
			GetRemoteNetAddress().GetPort());

		if (m_manager)
			m_manager->NotifyTcpBound(GetAccountId(), GetUserId());
	}

	void ClientTcpSession::OnDisconnected()
	{
		JAM_LOG_INFO("[AccountId = {}, UserId = {}] ClientTcpSession disconnected", GetAccountId(), GetUserId());
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
			WorldTransitionResult result;
			if (!codec::DecodeWorldTransitionResult(view.Payload(), view.PayloadSize(), result))
				return;
			if (result.failure != eWorldTransitionFailure::None)
				JAM_LOG_WARN("World transition failed. kind={}, requestId={}, reason={}",
					static_cast<uint32>(result.kind), result.requestId, static_cast<uint32>(result.failure));
			return;
		}

		if (view.Id() == CustomPacketId::CLIENT_WORLD_PREPARE)
		{
			ClientWorldPrepare prepare;
			if (!codec::DecodeClientWorldPrepare(view.Payload(), view.PayloadSize(), prepare))
			{
				JAM_LOG_WARN("[ClientWorldPrepare] verification failed. payloadSize={}", view.PayloadSize());
				return;
			}

			const SessionId		 sessionId  = GetSessionId();
			const EndpointHandle endpoint   = GetEndpointHandle();
			const uint32		 generation = GetServiceGeneration();
			auto service = GetServiceRef();

			m_manager->PrepareMainWorld(prepare, [service, sessionId, endpoint, generation, token = prepare.token](bool succeeded)
				{
					auto* self = service ? static_cast<ClientTcpSession*>(service->FindOwnedSession(sessionId, endpoint, generation)) : nullptr;
					if (!self)
					{
						JAM_LOG_WARN("[ClientWorldPrepare] sync response dropped; TCP session lookup failed. token={}, sessionId={}, generation={}", token.value, sessionId, generation);
						return;
					}
					self->Send(codec::MakeClientWorldSyncResultPacket({ .token = token, .succeeded = succeeded, .failure = succeeded ? eWorldTransitionFailure::None : eWorldTransitionFailure::ClientPrepareFailed }));
				});
			return;
		}

		if (view.Id() == CustomPacketId::CLIENT_WORLD_COMMIT)
		{
			ClientWorldCommit commit;
			if (!codec::DecodeClientWorldCommit(view.Payload(), view.PayloadSize(), commit))
				return;
			WorldRef world{};
			if (const auto& prepared = m_manager->GetMainWorldState().Prepared(); prepared)
				world = prepared->world;
			const bool wireMatches = world.instance.instanceId == commit.correlation.world.instance.instanceId
				&& world.worldId == commit.correlation.world.worldId;
			commit.correlation.world = world;
			const bool succeeded = wireMatches && m_manager->CommitMainWorld(commit);

			Send(codec::MakeClientWorldSyncResultPacket({ .token = commit.token, .succeeded = succeeded,
				.failure = succeeded ? eWorldTransitionFailure::None : eWorldTransitionFailure::ClientPrepareFailed }));
			return;
		}

		if (view.Id() == CustomPacketId::USER_MAIN_WORLD_CHANGED)
		{
			UserWorldState state;
			if (!codec::DecodeUserMainWorldChanged(view.Payload(), view.PayloadSize(), state))
				return;
			m_manager->ApplyMainWorldChanged(state);
			return;
		}

		if (view.Id() == CustomPacketId::SOCIAL_EVENT)
		{
			SocialMessage message;
			if (!codec::DecodeSocialMessage(view.Payload(), view.PayloadSize(), message))
				return;

			m_manager->PublishSocialMessage(std::move(message));
			return;
		}

		if (view.Id() == CustomPacketId::CONTENT)
		{
			GenericContentResponse response;
			if (!codec::DecodeContentResponse(view.Payload(), view.PayloadSize(), response))
				return;
			m_manager->PublishGenericContentResponse(std::move(response));
			return;
		}

		const WorldId worldId = ResolveScopedPacketWorldId(view);
		if (worldId == kInvalidWorldId)
			return;

		m_manager->DispatchWorldPacket(GetUserId(), worldId, std::move(packet));
	}

	void ClientTcpSession::OnTcpBindBootstrap(eBootstrapKind kind)
	{
		if (m_manager)
			m_manager->NotifyBootstrap(GetUserId(), kind);
	}





	void ClientUdpSession::OnLinkEstablished()
	{
		if (m_manager)
			m_manager->AssertPrincipalAffinity();
		JAM_LOG_INFO("[AccountId = {}, UserId = {}] ClientUdpSession established. ip: {} | port: {}",
			GetAccountId(),
			GetUserId(),
			GetRemoteNetAddress().GetIpAddress(),
			GetRemoteNetAddress().GetPort());

		if (m_manager)
			m_manager->NotifyUdpBound(GetUserId());
	}

	void ClientUdpSession::OnDisconnected()
	{
		JAM_LOG_INFO("[AccountId = {}, UserId = {}] ClientUdpSession disconnected", GetAccountId(), GetUserId());
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
				JAM_LOG_WARN(
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
