#include "pch.h"
#include "jamnet/sync/replication/ClientInputSystem.h"

#include "jamnet/sync/networld/ClientPhysicalWorld.h"
#include "jamnet/sync/replication/WorldContext.h"
#include "jamnet/sync/schema/gen/input_generated.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	void ClientInputSystem::Init()
	{
		m_world  = *m_registry.ctx().find<ClientPhysicalWorld*>();
		if (!m_world) return;

		m_userId = m_world->GetUserId();
		if (m_userId == kInvalidUserId) return;

		m_bInitialized = true;
		m_fbb.reset(new flatbuffers::FlatBufferBuilder(JAMNET_MTU));
		ResetInput();
	}


	void ClientInputSystem::SetInputFlags(uint32 flags)
	{
		px::CharacterInput input = m_inputSample;
		input.inputFlags	= flags;
		input.moveMode		= px::eMoveInputMode::Keyboard;
		input.mouseMoveKind = px::eMouseMoveKind::ToPosition;
		input.targetPos		= px::Vec3::Zero();
		input.targetNetId	= 0;
		m_inputSample = input;
	}

	void ClientInputSystem::SetInput(uint32 inputFlags, float facingYaw, float facingPitch, uint32 commandEpoch)
	{
		m_inputSample = px::CharacterInput{
			.inputFlags		= inputFlags,
			.commandEpoch	= commandEpoch,
			.facingYaw		= facingYaw,
			.facingPitch	= facingPitch,
			.moveMode		= px::eMoveInputMode::Keyboard,
			.mouseMoveKind	= px::eMouseMoveKind::ToPosition,
			.targetPos		= px::Vec3::Zero(),
			.targetNetId	= 0
		};
	}

	void ClientInputSystem::SetInput(const px::CharacterInput& input)
	{
		m_inputSample = input;
	}

	void ClientInputSystem::ResetInput()
	{
		m_inputSample = px::CharacterInput{};
	}

	void ClientInputSystem::SendInput(const InputCmd& cmd)
	{
		m_fbb->Clear();

		auto gameInput = fb::CreatefbGameInput(
			*m_fbb,
			m_world->GetWorldId(),
			m_userId,
			cmd.seq,
			cmd.input.inputFlags,
			cmd.input.commandEpoch,
			cmd.input.facingYaw,
			cmd.input.facingPitch,
			static_cast<uint8>(cmd.input.moveMode),
			static_cast<uint8>(cmd.input.mouseMoveKind),
			cmd.input.targetPos.x,
			cmd.input.targetPos.y,
			cmd.input.targetPos.z,
			cmd.input.targetNetId);
		m_fbb->Finish(gameInput, fb::fbGameInputIdentifier());

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

		inputHistory.current.input = m_inputSample;

		inputHistory.current.seq = currentTick;
		inputHistory.Push(inputHistory.current);

		SendInput(inputHistory.current);
	}


	void ClientInputSystem::OnServerAck(uint32 ackSeq)
	{
		if (auto* inputHistory = m_registry.ctx().find<InputHistoryBuffer>())
			inputHistory->PruneAck(ackSeq);
	}

}
