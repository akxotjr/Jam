#pragma once
#include "jamnet/runtime/UserContext.h"
#include "jamnet/sync/replication/ReplicationTypes.h"

namespace jam::net
{
	class ClientPhysicalWorld;

	class ClientInputSystem
	{
	public:
		explicit ClientInputSystem(entt::registry& registry) : m_registry(registry) {}

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
		void						SendInput(const InputCmd& cmd);


	private:
		entt::registry&									m_registry;

		ClientPhysicalWorld*							m_world			 = nullptr;
		UserId											m_userId		 = kInvalidUserId;

		bool											m_bInitialized   = false;
		std::unique_ptr<flatbuffers::FlatBufferBuilder>	m_fbb			 = nullptr;

		px::CharacterInput								m_inputSample    = {};

		static constexpr size_t							k_maxHistorySize = 512;
	};
}
