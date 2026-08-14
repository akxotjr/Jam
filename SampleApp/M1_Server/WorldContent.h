#pragma once

#include "PortalSystem.h"
#include "CharacterSessionStore.h"
#include "WorldContentsDatabase.h"

#include <jamnet/runtime/content/world/IWorldContent.h>

#include <memory>

namespace m1
{
	class WorldContent final : public jam::net::IWorldContent
	{
	public:
		WorldContent(const WorldContentsData& contents, const WorldInstanceContentsData& instance, std::shared_ptr<CharacterSessionStore> characterSessions);

		bool Initialize(jam::net::ServerWorld& world) override;
		void OnPhysicsEvents(jam::net::ServerWorld& world, std::span<const jam::px::PhysicsEvent> events) override;
		void PrepareMemberContent(jam::net::ServerWorld& world, const jam::net::ServerWorldMemberContentContext& context, PrepareMemberCompletion completion) override;
		void RollbackMemberContent(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken transitionToken) override;
		bool CommitMemberLeave(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken transitionToken) override;
		bool RestoreMemberContent(jam::net::ServerWorld& world, const jam::net::WorldUserContext& user, jam::net::WorldTransitionToken transitionToken) override;

	private:
		WorldContentsData						m_contents;
		std::shared_ptr<CharacterSessionStore>	m_characterSessions;
		PortalSystem							m_portals;
	};
}
