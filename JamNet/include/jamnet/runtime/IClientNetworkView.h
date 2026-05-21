#pragma once

#include "jamnet/runtime/UserContext.h"
#include "jamnet/runtime/AppRuntimeEvents.h"

#include <optional>
#include <span>


namespace jam::net
{
	class IClientNetworkView
	{
	public:
		virtual ~IClientNetworkView() = default;

		virtual AccountId								GetAccountId() const = 0;
		virtual UserId									GetUserId() const = 0;
		virtual NetworkState							GetNetworkState() const = 0;
		virtual std::vector<WorldMembershipView>		GetWorldMemberships() const = 0;
		virtual std::optional<WorldMembershipView>		GetMainWorldMembership() const = 0;
		virtual ActorPresentationFrameView				GetActorPresentationFrame(LocalWorldId localWorldId) const = 0;
	};
}
