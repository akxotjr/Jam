#pragma once
#include "jamnet/core/executor/SeqLock.h"
#include "jamnet/sync/replication/ReplicationTypes.h"

namespace jam::net
{
	class ClientNetWorld;

	/**
	 * @name ClientInputSystem
	 *
	 * @brief 입력 수집, 히스토리, 서버 전송
	 * @details
	 * - 매 틱 InputCmd 생성 (seq + flags)
	 * - 입력 히스토리 저장 (Replay 용)
	 * - 서버로 입력 패킷 전송
	 * - 서버 Ack 수신 시 히스토리에서 ack 된 입력 제거
	 */
	class ClientInputSystem
	{
	public:
		explicit ClientInputSystem(entt::registry& world) : m_world(world) {}

		void						Init();
		void						Tick();

		/// @brief flags만 갱신 (yaw/pitch는 유지)
		void						SetInputFlags(uint32 flags);
		/// @brief flags/yaw/pitch 갱신
		void						SetInput(uint32 inputFlags, float facingYaw, float facingPitch, uint32 commandEpoch = 0);
		/// @brief 전체 입력 갱신
		void						SetInput(const px::CharacterInput& input);
		/// @brief 서버 Ack 처리
		void						OnServerAck(uint32 ackSeq);
		/// @brief 현재 입력 샘플을 초기화
		void						ResetInput();

	private:
		void						SendInput(ClientNetWorld* netWorld, const InputCmd& cmd);


	private:
		entt::registry&									m_world;
		bool											m_bInitialized   = false;
		std::unique_ptr<flatbuffers::FlatBufferBuilder>	m_fbb			 = nullptr;

		SeqLockBox<px::CharacterInput>					m_inputSample    = {};

		static constexpr size_t							k_maxHistorySize = 512;
	};
}
