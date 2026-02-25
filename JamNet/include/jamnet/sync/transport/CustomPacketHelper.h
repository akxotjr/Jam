#pragma once

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

        constexpr uint8 NONE            = 0;   // 미사용
        constexpr uint8 NOTIFICATION    = 1;   // 서버 푸시 알림 (애플리케이션에서 구현)
        constexpr uint8 SNAPSHOT        = 2;   // 게임 스냅샷 (JamNetSync 내부용)
        constexpr uint8 INPUT           = 3;   // 클라이언트 입력 (JamNetSync 내부용)
        constexpr uint8 CONTROL         = 4;   // 예약/기타

        // 5-15: 향후 프레임워크 확장용 예약
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
            JAMNET_ASSERT(IsFrameworkReservedId(id) && "Framework must use reserved Custom Packet IDs (0-15)");
        }
        else
        {
            JAMNET_ASSERT(IsApplicationId(id) && "Application must use Custom Packet IDs (16-31)");
        }
    }
#else
    inline void ValidateCustomPacketId(uint8, bool) { /* No-op in release */ }
#endif
}