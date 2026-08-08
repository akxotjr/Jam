#pragma once


#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/character/CharacterBody.h"
#include "jampx/PhysicsDatabase.h"

namespace jam::px
{
	class  PhysicsWorld;
	class PhysicsArchetypeRegistry;
	struct SpawnDesc;
	struct ProjectileSpawnDesc;


	class ActorFactory
	{
	public:
		/// @brief Static / Dynamic / Kinematic RigidBody 생성 (physics archetype + runtime overrides 병합)
		static std::optional<RigidBody>     CreateRigidBody(
			PhysicsWorld&				world,
			PhysicsArchetypeKey			key,
			const PhysicsArchetypeData&	data,
			const SpawnDesc&			desc,
			ActorId						id,
			const TargetPoseResolver&	resolver = nullptr);

		/// @brief CCT / RemoteCCT CharacterBody 생성
		static std::optional<CharacterBody> CreateCharacterBody(
			PhysicsWorld&				world,
			PhysicsArchetypeKey			key,
			const PhysicsArchetypeData&	data,
			const SpawnDesc&			desc,
			ActorId						id);


		/// @brief RigidBody를 월드에서 제거 (actor 소유권은 PhysicsWorld)
		static void DestroyRigidBody(PhysicsWorld& world, const RigidBody& body);

		/// @brief CharacterBody(CCT + hitbox)를 월드에서 제거
		static void DestroyCharacterBody(PhysicsWorld& world, const CharacterBody& body);
	};


} // namespace jam::px
