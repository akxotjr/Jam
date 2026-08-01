#pragma once

#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"

#include <string>

namespace jam::shared::gen
{
	struct ActorArchetypesRootDto;
}

namespace jam::net
{
	struct ActorArchetypeDatabaseBuilder
	{
		static ActorArchetypeDatabase Build(const jam::shared::gen::ActorArchetypesRootDto& dto);
	};

	struct ActorArchetypesLoader
	{
		static jam::shared::gen::ActorArchetypesRootDto LoadDto(const std::string& path);
		static ActorArchetypeDatabase Load(const std::string& path);
	};
}
