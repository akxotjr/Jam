#include "pch.h"
#include "jamnet/sync/replication/ClientInputSystem.h"
#include "jamnet/sync/networld/ClientNetWorld.h"
#include "jamnet/sync/replication/NetWorldContext.h"
#include "jamnet/sync/schema/gen/input_generated.h"
#include "jamnet/sync/transport/CustomPacketHelper.h"

namespace jam::net
{
	void ClientInputSystem::Init()
	{
		m_bInitialized = true;
		m_fbb.reset(new flatbuffers::FlatBufferBuilder(JAMNET_MTU));
		ResetInput();
	}


	void ClientInputSystem::SetInputFlags(uint32 flags)
	{
		px::CharacterInput input{};
		if (!m_inputSample.TryRead(input))
			input = {};

		input.inputFlags	= flags;
		input.moveMode		= px::eMoveInputMode::Keyboard;
		input.mouseMoveKind = px::eMouseMoveKind::ToPosition;
		input.targetPos		= px::Vec3::Zero();
		input.targetNetId	= 0;
		m_inputSample.Write(input);
	}

	void ClientInputSystem::SetInput(uint32 inputFlags, float facingYaw, float facingPitch, uint32 commandEpoch)
	{
		m_inputSample.Write(px::CharacterInput{
			.inputFlags		= inputFlags,
			.commandEpoch	= commandEpoch,
			.facingYaw		= facingYaw,
			.facingPitch	= facingPitch,
			.moveMode		= px::eMoveInputMode::Keyboard,
			.mouseMoveKind	= px::eMouseMoveKind::ToPosition,
			.targetPos		= px::Vec3::Zero(),
			.targetNetId	= 0
		});
	}

	void ClientInputSystem::SetInput(const px::CharacterInput& input)
	{
		m_inputSample.Write(input);
	}

	void ClientInputSystem::ResetInput()
	{
		m_inputSample.Write(px::CharacterInput{});
	}


	void ClientInputSystem::Tick()
	{
		if (!m_bInitialized) return;

		if (!m_world.ctx().contains<InputHistoryBuffer>())
			m_world.ctx().emplace<InputHistoryBuffer>();

		auto& inputHistory = m_world.ctx().get<InputHistoryBuffer>();
		const uint32 currentTick = m_world.ctx().get<TickCounter>().tick;

		px::CharacterInput sample{};
		if (m_inputSample.TryRead(sample))
			inputHistory.current.input = sample;

		inputHistory.current.seq = currentTick;
		inputHistory.Push(inputHistory.current);

		if (auto* netWorld = m_world.ctx().get<ClientNetWorld*>())
		{
			SendInput(netWorld, inputHistory.current);
		}
	}


	void ClientInputSystem::OnServerAck(uint32 ackSeq)
	{
		if (auto* inputHistory = m_world.ctx().find<InputHistoryBuffer>())
			inputHistory->PruneAck(ackSeq);
	}

	void ClientInputSystem::SendInput(ClientNetWorld* netWorld, const InputCmd& cmd)
	{
		m_fbb->Clear();

		uint64 userId = netWorld->GetUserId();
		auto gameInput = fb::CreatefbGameInput(
			*m_fbb,
			userId,
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

		if (auto buf = PacketBuilder::CreateCustomPacket(CustomPacketId::INPUT, PacketFlags::NONE, eChannelType::UNRELIABLE_SEQUENCED, m_fbb->GetBufferPointer(), m_fbb->GetSize())) 
			netWorld->Send(buf);
	}
}
