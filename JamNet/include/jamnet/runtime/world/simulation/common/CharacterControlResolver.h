#pragma once

#include "jamnet/runtime/world/simulation/common/CharacterControlTypes.h"

namespace jam::net
{
	struct CharacterControlResolveContext
	{
		const px::CharacterState*	selfState = nullptr;
		bool							hasFollowTargetPosition = false;
		px::Vec3						followTargetPosition = px::Vec3::Zero();
	};

	/// @brief Current control source를 simulation에 적용할 character input으로 해석한다.
	/// @note Phase 2에서는 existing px::CharacterInput contract를 직접 해석한다.
	class CharacterControlResolver
	{
	public:
		static px::CharacterMotorInput	Resolve(
			const CharacterControlIntent& intent,
			const CharacterControlResolveContext& context,
			const CharacterControlResolveConfig& config);

		static px::CharacterMotorInput	Resolve(
			const px::CharacterMotorInput& source,
			const CharacterControlResolveContext& context,
			const CharacterControlResolveConfig& config);
	};
}
