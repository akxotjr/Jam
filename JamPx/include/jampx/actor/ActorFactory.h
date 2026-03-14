#pragma once


#include "jampx/actor/rigid/RigidBody.h"
#include "jampx/actor/character/CharacterBody.h"

namespace jam::px
{
	class  PhysicsWorld;
	struct ActorTemplateDef;
	struct SpawnDesc;
	struct ProjectileSpawnDesc;

	class ActorFactory
	{
	public:
		/// @brief Static / Dynamic / Kinematic RigidBody 생성 (prefab + runtime overrides 병합)
		static std::optional<RigidBody>     CreateRigidBody(
			PhysicsWorld&				world,
			TemplateHandle				tpl,
			const ActorTemplateDef&		tplDef,
			const SpawnDesc&			desc,
			ObjectId					id);

		/// @brief CCT / RemoteCCT CharacterBody 생성
		static std::optional<CharacterBody> CreateCharacterBody(
			PhysicsWorld&				world,
			TemplateHandle				tpl,
			const ActorTemplateDef&		tplDef,
			const SpawnDesc&			desc,
			ObjectId					id);


		/// @brief Actor의 모든 Shape에 PackedId 필터 적용
		static void ApplyPackedId(
			const PxRigidActor&	actor,
			uint16				teamId,
			uint8				partId,
			uint8				roleId);

		/// @brief RigidBody를 월드에서 제거 (actor 소유권은 PhysicsWorld)
		static void DestroyRigidBody(const PhysicsWorld& world, const RigidBody& body);

		/// @brief CharacterBody(CCT + hitbox)를 월드에서 제거
		static void DestroyCharacterBody(PhysicsWorld& world, const CharacterBody& body);
	};


} // namespace jam::px