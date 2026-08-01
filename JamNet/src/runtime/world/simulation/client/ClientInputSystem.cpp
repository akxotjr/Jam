#include "pch.h"
#include "jamnet/runtime/world/simulation/client/ClientInputSystem.h"

#include "jamnet/runtime/world/simulation/client/ClientWorld.h"
#include "jamnet/runtime/world/simulation/common/WorldContext.h"
#include "jamnet/runtime/protocol/schema/gen/input_generated.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"

namespace jam::net
{
	void ClientInputSystem::Init()
	{
		m_world  = *m_registry.ctx().find<ClientWorld*>();
		if (!m_world) return;

		m_userId = m_world->GetUserId();
		if (m_userId == kInvalidUserId) return;

		m_bInitialized = true;
		m_fbb.reset(new flatbuffers::FlatBufferBuilder(JAMNET_MTU));
		ResetInput();
	}


	void ClientInputSystem::SetIntent(const CharacterControlIntent& intent)
	{
		const CharacterActionFlags pendingEdges = m_intentSample.edgeActions;
		m_intentSample = intent;
		m_intentSample.edgeActions |= pendingEdges;
	}

	void ClientInputSystem::ResetInput()
	{
		m_intentSample = {};
	}

	void ClientInputSystem::SendInput(const CharacterControlCommand& cmd)
	{
		m_fbb->Clear();
		const auto& intent = cmd.intent;
		fb::fbLocomotionKind kind = fb::fbLocomotionKind_Stop;
		float localX = 0.0f, localY = 0.0f;
		px::Vec3 target = px::Vec3::Zero();
		uint32 targetActorId = 0;
		std::visit([&](const auto& locomotion)
			{
				using T = std::decay_t<decltype(locomotion)>;
				if constexpr (std::is_same_v<T, DirectionalMoveIntent>) { kind = fb::fbLocomotionKind_Directional; localX = locomotion.localX; localY = locomotion.localY; }
				else if constexpr (std::is_same_v<T, MoveToPositionIntent>) { kind = fb::fbLocomotionKind_MoveToPosition; target = locomotion.target; }
				else if constexpr (std::is_same_v<T, FollowActorIntent>) { kind = fb::fbLocomotionKind_FollowActor; targetActorId = locomotion.target.Value(); }
			}, intent.locomotion);
		auto command = fb::CreatefbCharacterControlCommand(*m_fbb, m_world->GetWorldId(), cmd.sequence, intent.controlRevision, intent.viewYaw, intent.viewPitch, intent.continuousActions, intent.edgeActions, kind, localX, localY, target.x, target.y, target.z, targetActorId, intent.moveReferenceYaw, static_cast<uint8>(intent.viewPolicy));
		m_fbb->Finish(command, fb::fbCharacterControlCommandIdentifier());

		auto pkt = PacketBuilder::CreateCustomPacket(CustomPacketId::INPUT, PacketFlags::NONE, eChannel::UNRELIABLE_SEQUENCED, m_fbb->GetBufferPointer(), m_fbb->GetSize());
		if (pkt.IsValid() && m_world)
			m_world->Send(pkt);
	}


	void ClientInputSystem::Tick()
	{
		if (!m_bInitialized) return;

		if (!m_registry.ctx().contains<InputHistoryBuffer>())
			m_registry.ctx().emplace<InputHistoryBuffer>();

		auto& inputHistory = m_registry.ctx().get<InputHistoryBuffer>();
		const uint32 currentTick = m_registry.ctx().get<TickCounter>().tick;

		CharacterControlCommand command{};
		command.sequence = currentTick;
		command.intent = m_intentSample;
		m_intentSample.edgeActions = 0;

		inputHistory.current = command;
		inputHistory.Push(command);
		SendInput(command);
	}


	void ClientInputSystem::OnServerAck(uint32 ackSeq)
	{
		if (auto* inputHistory = m_registry.ctx().find<InputHistoryBuffer>())
			inputHistory->PruneAck(ackSeq);
	}

}
