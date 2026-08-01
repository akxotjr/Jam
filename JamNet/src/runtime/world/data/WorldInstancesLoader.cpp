#include "pch.h"

#include "jambase/JsonFileIO.h"
#include "jamnet/runtime/world/data/WorldInstancesLoader.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace jam::net
{
	namespace
	{
		eWorldInstanceStartup ParseStartup(const std::string& value)
		{
			if (value == "bootstrap")
				return eWorldInstanceStartup::Bootstrap;
			if (value == "on_demand")
				return eWorldInstanceStartup::OnDemand;
			throw std::runtime_error("unsupported world instance startup policy: " + value);
		}

		eWorldInstanceLifecycle ParseLifecycle(const std::string& value)
		{
			if (value == "persistent")
				return eWorldInstanceLifecycle::Persistent;
			if (value == "destroy_when_empty")
				return eWorldInstanceLifecycle::DestroyWhenEmpty;
			throw std::runtime_error("unsupported world instance lifecycle policy: " + value);
		}
	}

	WorldInstancesLoader::json WorldInstancesLoader::LoadJson(const std::string& path)
	{
		return JsonFileIO::Load(path, "failed to open world instance file for read: ", [](const json&) {});
	}

	WorldInstanceDatabase WorldInstancesLoader::Load(const std::string& path)
	{
		const json root = LoadJson(path);
		const int32 version = root.at("version").get<int32>();
		if (version != 1)
			throw std::runtime_error("unsupported world instance asset version");

		const json& instances = root.at("world_instances");
		if (!instances.is_object())
			throw std::runtime_error("world_instances must be an object");

		WorldInstanceDatabase database{};
		database.version = version;
		database.definitionsByName.reserve(instances.size());
		database.definitionsById.reserve(instances.size());
		database.definitionsByArchetype.reserve(instances.size());

		for (auto it = instances.begin(); it != instances.end(); ++it)
		{
			const std::string name = it.key();
			const json& value = it.value();
			if (name.empty())
				throw std::runtime_error("world instance name must not be empty");

			WorldInstanceDefinition definition{};
			definition.name = name;
			definition.instanceId = MakeStaticWorldInstanceId(name);
			definition.archetypeName = value.at("archetype").get<std::string>();
			definition.archetypeKey = MakeWorldArchetypeKey(definition.archetypeName);
			definition.startup = ParseStartup(value.at("startup").get<std::string>());
			definition.lifecycle = ParseLifecycle(value.at("lifecycle").get<std::string>());
			definition.defaultForArchetype = value.value("default", false);
			if (!definition.IsValid())
				throw std::runtime_error("invalid world instance definition: " + name);

			auto [stored, inserted] = database.definitionsByName.emplace(definition.name, std::move(definition));
			if (!inserted)
				throw std::runtime_error("duplicate world instance name: " + name);
			if (!database.definitionsById.emplace(stored->second.instanceId, &stored->second).second)
				throw std::runtime_error("duplicate world instance id: " + name);
			database.definitionsByArchetype.emplace(stored->second.archetypeKey, &stored->second);
		}

		for (const auto& [archetypeKey, definition] : database.definitionsByArchetype)
		{
			if (!definition->defaultForArchetype)
				continue;

			if (database.FindDefault(archetypeKey) != definition)
				throw std::runtime_error("multiple default world instances for one archetype");
		}

		return database;
	}
}
