#pragma once

#include "jamnet/runtime/world/simulation/common/CharacterControlTypes.h"

namespace jam::net
{
	class ClientWorld;

	class ClientCharacterControlCoordinator
	{
	public:
		explicit ClientCharacterControlCoordinator(ClientWorld& world)
			: m_world(world)
		{
		}

		void Submit(CharacterControlIntent intent);

	private:
		void ResolveWorldRay(CharacterControlIntent& intent);
		void ApplyCurrentIntent();

	private:
		ClientWorld&			m_world;
		CharacterControlIntent	m_currentIntent = {};
		uint32					m_controlRevision = 0;
	};
}
