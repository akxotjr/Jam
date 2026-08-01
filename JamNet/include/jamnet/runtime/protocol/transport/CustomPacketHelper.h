#pragma once

#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/runtime/world/lifecycle/WorldIdentity.h"
#include "jamnet/runtime/protocol/schema/gen/world_assignment_generated.h"
#include "jamnet/runtime/protocol/schema/gen/input_generated.h"
#include "jamnet/runtime/protocol/schema/gen/lifecycle_generated.h"
#include "jamnet/runtime/protocol/schema/gen/snapshot_generated.h"
#include "jamnet/runtime/protocol/schema/gen/baseline_ack_generated.h"

namespace jam::net
{
	/// @brief Custom Packet ID 범위 및 프레임워크 예약 ID 정의
	namespace CustomPacketId
	{
		// ============================================================
		// ID 범위 정의
		// ============================================================

		/// @brief 프레임워크 예약 영역 (0-15)
		constexpr uint8 FRAMEWORK_MIN = 0;
		constexpr uint8 FRAMEWORK_MAX = 15;

		/// @brief 애플리케이션 사용 영역 (16-31)
		constexpr uint8 APPLICATION_MIN = 16;
		constexpr uint8 APPLICATION_MAX = 31;

		// ============================================================
		// 프레임워크 예약 ID (JamNetSync 전용)
		// ============================================================

		constexpr uint8 NONE					= 0;   // 미사용
		constexpr uint8 NOTIFICATION			= 1;   // 서버 푸시 알림 (애플리케이션에서 구현)
		constexpr uint8 SNAPSHOT				= 2;   // 게임 스냅샷 (JamNetSync 내부용)
		constexpr uint8 INPUT					= 3;   // 클라이언트 입력 (JamNetSync 내부용)
		constexpr uint8 LIFECYCLE				= 4;   // 생성/삭제/메타 복제
		constexpr uint8 USER_MAIN_WORLD_CHANGED = 5;
		constexpr uint8 CLIENT_WORLD_PREPARE	= 6;
		constexpr uint8 CLIENT_WORLD_COMMIT		= 7;
		constexpr uint8 CLIENT_BARRIER_RESULT	= 8;
		constexpr uint8 BASELINE_ACK			= 9;
		constexpr uint8 ENTER_WORLD_REQUEST		= 10;
		constexpr uint8 LEAVE_WORLD_REQUEST		= 11;
		constexpr uint8 WORLD_TRANSITION_RESULT = 12;

		constexpr uint8 SOCIAL_COMMAND			= 13;   // client -> server (implementation in application layer)
		constexpr uint8 SOCIAL_EVENT			= 14;   // server -> client (implementation in application layer)

		constexpr uint8 CONTROL         = LIFECYCLE;   // 하위 호환 별칭

		// 13-15: 향후 프레임워크 확장용 예약
	}

	// ============================================================
	// 유틸리티 함수
	// ============================================================

	/// @brief Custom Packet ID가 프레임워크 예약 영역인지 확인
	inline constexpr bool IsFrameworkReservedId(uint8 id)
	{
		return id >= CustomPacketId::FRAMEWORK_MIN && id <= CustomPacketId::FRAMEWORK_MAX;
	}

	/// @brief Custom Packet ID가 애플리케이션 사용 가능 영역인지 확인
	inline constexpr bool IsApplicationId(uint8 id)
	{
		return id >= CustomPacketId::APPLICATION_MIN && id <= CustomPacketId::APPLICATION_MAX;
	}

	/// @brief 애플리케이션 커스텀 패킷 ID 생성 (16 + offset)
	/// @param offset 0-15 범위의 오프셋 값
	/// @return 애플리케이션 패킷 ID (16-31)
	inline constexpr uint8 MakeApplicationId(uint8 offset)
	{
		return CustomPacketId::APPLICATION_MIN + (offset & 0x0F);
	}

#ifdef _DEBUG
	/// @brief 디버그 빌드에서 Custom Packet ID 유효성 검증
	inline void ValidateCustomPacketId(uint8 id, bool isFrameworkCall)
	{
		if (isFrameworkCall)
		{
			JAM_ASSERT(IsFrameworkReservedId(id) && "Framework must use reserved Custom Packet IDs (0-15)");
		}
		else
		{
			JAM_ASSERT(IsApplicationId(id) && "Application must use Custom Packet IDs (16-31)");
		}
	}
#else
	inline void ValidateCustomPacketId(uint8, bool) { /* No-op in release */ }
#endif

	inline WorldId ResolveScopedPacketWorldId(const PacketHeaderView& view)
	{
		if (!view.IsValid())
			return kInvalidWorldId;

		flatbuffers::Verifier verifier(view.Payload(), view.PayloadSize());
		switch (view.Id())
		{
		case CustomPacketId::INPUT:
		{
			if (!fb::VerifyfbCharacterControlCommandBuffer(verifier))
				return kInvalidWorldId;
			if (const auto* input = fb::GetfbCharacterControlCommand(view.Payload()))
				return input->world_id();
			return kInvalidWorldId;
		}
		case CustomPacketId::SNAPSHOT:
		{
			if (!fb::VerifyfbSnapshotBuffer(verifier))
				return kInvalidWorldId;
			if (const auto* snapshot = fb::GetfbSnapshot(view.Payload()))
				return snapshot->world_id();
			return kInvalidWorldId;
		}
		case CustomPacketId::LIFECYCLE:
		{
			if (!fb::VerifyfbLifecycleBatchBuffer(verifier))
				return kInvalidWorldId;
			if (const auto* lifecycle = fb::GetfbLifecycleBatch(view.Payload()))
				return lifecycle->world_id();
			return kInvalidWorldId;
		}
		case CustomPacketId::BASELINE_ACK:
		{
			if (!fb::VerifyfbBaselineAckBatchBuffer(verifier))
				return kInvalidWorldId;
			if (const auto* ack = fb::GetfbBaselineAckBatch(view.Payload()))
				return ack->world_id();
			return kInvalidWorldId;
		}
		default:
			return kInvalidWorldId;
		}
	}
}
