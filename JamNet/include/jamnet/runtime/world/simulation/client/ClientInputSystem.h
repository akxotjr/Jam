#pragma once
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/world/simulation/common/ReplicationTypes.h"

namespace jam::net
{
	class ClientWorld;

	class ClientInputSystem
	{
	public:
		explicit ClientInputSystem(entt::registry& registry) : m_registry(registry) {}

		void						Init();
		void						Tick();

		/// @brief 최신 continuous 상태로 교체하고 아직 소비되지 않은 edge를 누적한다.
		void						SetIntent(const CharacterControlIntent& intent);
		/// @brief 서버 Ack 처리
		void						OnServerAck(uint32 ackSeq);
		/// @brief 현재 입력 샘플을 초기화
		void						ResetInput();

	private:
		void						SendInput(const CharacterControlCommand& cmd);


	private:
		entt::registry&									m_registry;

		ClientWorld*									m_world			 = nullptr;
		UserId											m_userId		 = kInvalidUserId;

		bool											m_bInitialized   = false;
		std::unique_ptr<flatbuffers::FlatBufferBuilder>	m_fbb			 = nullptr;

		CharacterControlIntent							m_intentSample   = {};
		static constexpr size_t							k_maxHistorySize = 512;
	};
}
