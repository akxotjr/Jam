#pragma once


#include "IPhysicsJobBridge.h"
#include "PhysicsTypes.h"



namespace jam::px
{
	class IPhysicsFacade
	{
	public:
		virtual ~IPhysicsFacade() = default;

		virtual void			Init() = 0;
		virtual void			Shutdown() = 0;

		virtual void			SetJobBridge(IPhysicsJobBridge* bridge) = 0;

		virtual bool			LoadLevel(const std::string& path) = 0;

		virtual void			Step(float dt) = 0;
		virtual bool			BeginStep(float dt, uint64 awaitKey) = 0;
		virtual void			EndStep() = 0;

		virtual PhysicsHandle	Spawn(ObjectKey key, const SpawnDesc& desc) = 0;
		virtual void			Despawn(ObjectKey key) = 0;

		virtual eBodyKind		GetKind(ObjectKey key) const = 0;
		virtual eBodyKind		GetKind(PrefabKey prefab) const = 0;

		virtual bool			GetCharacterState(ObjectKey key, CharacterState& state) const = 0;
		virtual bool			SetCharacterState(ObjectKey key, const CharacterState& state) = 0;

		virtual bool			GetRigidState(ObjectKey key, RigidState& state) const = 0;
		virtual bool			SetRigidState(ObjectKey key, const RigidState& state) = 0;

		virtual void 			ApplyCharacterInput(ObjectKey key, const CharacterInput& input) = 0;
	};
}
