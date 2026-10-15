#pragma once

#include "PortalSystem.h"
#include "CharacterSessionStore.h"
#include "WorldContentsDatabase.h"

#include <jamnet/runtime/content/world/IWorldContent.h>

#include <memory>
#include <unordered_map>

namespace m1
{
	class WorldContent final : public jam::net::IWorldContent
	{
	public:
		WorldContent(const WorldContentsData& contents, const WorldInstanceContentsData& instance, std::shared_ptr<CharacterSessionStore> characterSessions);

		bool OnInitialize(jam::net::ServerWorld& world) override;
		void OnClose(jam::net::ServerWorld& world) override;
		bool OnJoiningMembership(jam::net::ServerWorld& world, const jam::net::WorldMembershipContext& context) override;
		bool OnPreparedMembership(jam::net::ServerWorld& world, const jam::net::WorldMembershipContext& context) override;
		bool OnActivatedMembership(jam::net::ServerWorld& world, const jam::net::WorldMembershipContext& context) override;
		bool OnDetachingMembership(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken token) override;
		bool OnRollbackMembership(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken token) override;
		bool OnRemovedMembership(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken token) override;
		bool OnRestoredMembership(jam::net::ServerWorld& world, const jam::net::WorldUserContext& user, jam::net::WorldTransitionToken token) override;
		void OnCommittedMembership(jam::net::ServerWorld& world, jam::net::UserId userId, jam::net::WorldTransitionToken token) override;
		void OnPhysicsEvents(jam::net::ServerWorld& world, std::span<const jam::px::PhysicsEvent> events) override;

	private:
		struct DetachedMember
		{
			jam::net::UserId userId;
			PlayerActorBinding player;
			PersistentCharacterState character;
		};
		std::unordered_map<jam::net::WorldTransitionToken, DetachedMember, jam::net::WorldTransitionTokenHash> m_detachedMembers;
		std::unordered_map<jam::net::WorldTransitionToken, PersistentCharacterState, jam::net::WorldTransitionTokenHash> m_entryCharacters;
		std::unordered_map<jam::net::WorldTransitionToken, PlayerActorBinding, jam::net::WorldTransitionTokenHash> m_entryPlayers;
		WorldContentsData						m_contents;
		std::shared_ptr<CharacterSessionStore>	m_characterSessions;
		PortalSystem							m_portals;
	};
}
