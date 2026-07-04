#pragma once

#include "jamnet/runtime/world/action/IWorldAssignmentPolicy.h"
#include "jamnet/runtime/world/data/WorldArchetypeDatabase.h"
#include "jamnet/runtime/world/core/WorldDirectory.h"
#include "jamnet/runtime/world/data/WorldTemplateDatabase.h"

#include <optional>
#include <span>

namespace jam::net
{
	class DefaultWorldAssignmentPolicy : public IWorldAssignmentPolicy
	{
	public:
		DefaultWorldAssignmentPolicy() = default;
		~DefaultWorldAssignmentPolicy() override = default;

		WorldActionPlan					PlanAction(const WorldActionRequest& req) override;
		void							BindWorldDirectory(const WorldDirectory* directory) override { m_directory = directory; }
		void							BindWorldArchetypeDatabase(const WorldArchetypeDatabase* database) override { m_archetypes = database; }
		void							BindWorldTemplateDatabase(const WorldTemplateDatabase* database) override { m_templates = database; }

	private:
		WorldArchetypeKey				ResolveArchetypeKey(const WorldActionRequest& req) const;
		const WorldTemplateData*		ResolveTemplate(const WorldActionRequest& req) const;
		std::optional<WorldMeta>		SelectWorld(const WorldTemplateData& templateData, std::span<const WorldMeta> candidates) const;

	private:
		const WorldDirectory*			m_directory	 = nullptr;
		const WorldArchetypeDatabase*	m_archetypes = nullptr;
		const WorldTemplateDatabase*	m_templates	 = nullptr;
	};
}
