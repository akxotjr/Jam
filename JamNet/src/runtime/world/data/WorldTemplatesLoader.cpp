#include "pch.h"

#include "jamnet/runtime/world/data/WorldTemplatesLoader.h"

#include <Cpp/world_templates.generated.hpp>

#include <stdexcept>

namespace jam::net
{
	namespace
	{
		void ValidateWorldTemplateIdentity(const std::string& name)
		{
			if (name.empty())
				throw std::runtime_error("world template name must not be empty");

			const WorldTemplateKey expectedKey = MakeWorldTemplateKey(name);
			if (!IsValidAssetKey(expectedKey))
				throw std::runtime_error("derived world template key must not be zero: " + name);
		}

		WorldTemplateData BuildData(const std::string& name, const jam::shared::gen::WorldTemplateDto& dto)
		{
			WorldTemplateData data{};
			data.name							= name;
			data.key							= MakeWorldTemplateKey(name);
			data.group							= static_cast<WorldGroup>(dto.group);
			data.allowMultipleInstancePerUser	= dto.allowMultipleInstancePerUser;
			data.persistent						= dto.persistent;
			data.destroyWhenEmpty				= dto.destroyWhenEmpty;
			data.isPrivate						= dto.isPrivate;
			data.capacity						= static_cast<uint32>(dto.capacity);
			ValidateWorldTemplateIdentity(data.name);
			return data;
		}
	}

	jam::shared::gen::WorldTemplatesRootDto WorldTemplatesLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadWorldTemplatesRootDto(path);
	}

	WorldTemplateDatabase WorldTemplatesLoader::Load(const std::string& path)
	{
		return WorldTemplateDatabaseBuilder::Build(LoadDto(path));
	}

	WorldTemplateDatabase WorldTemplateDatabaseBuilder::Build(const jam::shared::gen::WorldTemplatesRootDto& dto)
	{
		WorldTemplateDatabase database{};
		database.version = dto.version;
		if (database.version != 1)
			throw std::runtime_error("unsupported world template asset version");

		database.templates.reserve(dto.worldTemplates.size());
		for (const auto& [name, templateDto] : dto.worldTemplates)
		{
			WorldTemplateData data = BuildData(name, templateDto);
			if (!database.templates.emplace(data.key, std::move(data)).second)
				throw std::runtime_error("duplicate world template key: " + name);
		}

		return database;
	}
}
