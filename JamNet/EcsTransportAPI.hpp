#pragma once
#include "pch.h"

namespace jam::net::ecs
{
    enum class eTxReason : uint8
    {
        NORMAL,
        RETRANSMIT,
        CONTROL,
        ACK_ONLY,
    };

    void EnqueueSend(/*entt::registry& R,*/ entt::entity e, const Sptr<SendBuffer>& buf, eTxReason reason);
}
