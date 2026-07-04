#include "pch.h"

#include "jambase/JsonFileIO.h"
#include "jamnet/runtime/world/data/WorldTemplatesLoader.h"

#include <Cpp/world_templates.generated.hpp>

#include <stdexcept>
#include <nlohmann/json.hpp>

namespace jam::net
{
	namespace
	{
		void ValidateWorldTemplateIdentity(const std::string& name, WorldTemplateKey key)
		{
			if (name.empty())
				throw std::runtime_error("world template name must not be empty");

			const WorldTemplateKey expectedKey = MakeWorldTemplateKey(name);
			if (!IsValidAssetKey(key))
				throw std::runtime_error("world template key must not be zero: " + name);
			if (expectedKey != key)
				throw std::runtime_error("world template key mismatch: " + name);
		}

		eWorldKind ToWorldKind(jam::shared::gen::eWorldTemplateDtoKind kind)
		{
			switch (kind)
			{
			case jam::shared::gen::eWorldTemplateDtoKind::Virtual:		return eWorldKind::Virtual;
			case jam::shared::gen::eWorldTemplateDtoKind::Physical:		return eWorldKind::Physical;
			}

			throw std::runtime_error("invalid world template dto kind");
		}

		eWorldTickMode ToWorldTickMode(jam::shared::gen::eWorldTemplateDtoTickMode tickMode)
		{
			switch (tickMode)
			{
			case jam::shared::gen::eWorldTemplateDtoTickMode::None:		return eWorldTickMode::None;
			case jam::shared::gen::eWorldTemplateDtoTickMode::Fixed:	return eWorldTickMode::Fixed;
			case jam::shared::gen::eWorldTemplateDtoTickMode::OnDemand: return eWorldTickMode::OnDemand;
			}

			throw std::runtime_error("invalid world template dto tick mode");
		}

		WorldTemplateData BuildData(const std::string& name, const jam::shared::gen::WorldTemplateDto& dto)
		{
			WorldTemplateData data{};
			data.name							= name;
			data.key							= WorldTemplateKey::FromU64(dto.templateKey);
			data.kind							= ToWorldKind(dto.kind);
			data.group							= static_cast<WorldGroup>(dto.group);
			data.allowMultipleInstancePerUser	= dto.allowMultipleInstancePerUser;
			data.persistent						= dto.persistent;
			data.destroyWhenEmpty				= dto.destroyWhenEmpty;
			data.isPrivate						= dto.isPrivate;
			data.maxAuxiliaryWorldMemberships	= static_cast<uint32>(dto.maxActiveGameplayMemberships);
			data.capacity						= static_cast<uint32>(dto.capacity);
			data.tickMode						= ToWorldTickMode(dto.tickMode);

			ValidateWorldTemplateIdentity(data.name, data.key);
			return data;
		}
	}

	WorldTemplatesLoader::json WorldTemplatesLoader::LoadJson(const std::string& path)
	{
		return JsonFileIO::Load(path, "failed to open world template file for read: ",
			[](const json&)
			{
			});
	}

	jam::shared::gen::WorldTemplatesRootDto WorldTemplatesLoader::LoadDto(const std::string& path)
	{
		return jam::shared::gen::LoadWorldTemplatesRootDto(path);
	}

	jam::shared::gen::WorldTemplatesRootDto WorldTemplatesLoader::LoadDtoFromJson(const json& json)
	{
		return jam::shared::gen::DeserializeWorldTemplatesRootDto(json);
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
