#pragma once

#include <jampx/PhysicsTypes.h>

#include "ReplicationTypes.h"

namespace jam::net
{


	/// @brief	Identifier of NetActor
	struct NetIdentity
	{
		uint32				netId = 0;
	};

	/// @brief	PhysicsFacade(ObjectKey) is NetWorld(registry) local Key. 
	///			rule : ObjectKey.value == entt::entity (casted uint32) 
	inline px::ObjectId MakeObjectId(entt::entity e) noexcept
	{
		return static_cast<px::ObjectId>(e);
	}

	/// @brief	PrefabKey(in PhyiscsFacade) of NetActor
	struct NetPrefabKey
	{
		px::PrefabKey		key;
	};


	struct NetActorBodyType
	{
		px::eBodyType		body = px::eBodyType::Rigid;
	};


	/// @brief	PhysicsHandle of PhysX PxRigidActor reference in PhysicsFacade
	struct RigidPhysicalBody
	{
		px::PhysicsHandle	handle{};
	};

	/// @brief	PhysicsHandle of PhysX PxController reference in PhysicsFacade
	struct CharacterPhysicalBody
	{
		px::PhysicsHandle	handle{};
	};

	struct CharacterHitboxPhysicalBody
	{
		px::PhysicsHandle	handle{};
	};


	/// @brief	Client predict spawn tag. not ensured by server
	struct NetPendingSpawnTag {};

	/// @brief	Client predict spwan identifier
	struct NetSpawnRequestId
	{
		uint32					requestId = 0;
	};

	/// @brief 새로 생성된 액터 표시
	struct NewlyCreatedTag {};



	/// @note Ownership vs Control
	///		소유권(Ownership) != 조종권(Control)
	///		- Ownership: 누가 이 액터를 생성하였는가? (권한, Despawn 권한)
	///		- Control  : 누가 이 액터를 조종하는가? (입력 권한)
	///		=> 소유권자는 조종권을 가질 수도 있고, 아닐 수도 있다.
	///		=> 즉, 조종권을 가지려면 소유해야된다. 

	/// @brief 액터 소유권
	/// @details 기본적으로 소유권 변경 불가. 
	struct OwnershipTag
	{
		uint64					userId = 0;		// = 0: 서버 소유. > 0: userId 소유자가 소유
	};

	/// @brief 액터 조종권
	/// @details 조종권 변경 가능
	struct ControlTag
	{
		uint64					userId = 0;		// = 0: 조종자 없음. > 0: userId 소유자가 조종권 소유 
	};

	/// @brief Tag of local character (usage only client-side). Local character is unique in NetWorld.
	struct LocalCharacterTag {};

	/// @brief Input state and history of Local character. 
	struct LocalInputState
	{
		InputCmd				currentInput{};
		deque<InputCmd>			unackedInputs;
	};



	// --- helpers ---

	static entt::entity GetLocalEntity(entt::registry& world)
	{
		auto view = world.view<LocalCharacterTag>();
		return view.empty() ? entt::null : view.front();
	}

}
