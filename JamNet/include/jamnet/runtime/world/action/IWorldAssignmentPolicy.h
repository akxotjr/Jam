#pragma once

#include "jamnet/runtime/world/types/WorldActionTypes.h"

#include <memory>

namespace jam::net
{
	class WorldDirectory;
	struct WorldArchetypeDatabase;
	struct WorldTemplateDatabase;

	class IWorldAssignmentPolicy
	{
	public:
		virtual ~IWorldAssignmentPolicy() = default;

		virtual WorldActionPlan PlanAction(const WorldActionRequest& req) = 0;
		virtual void			BindWorldDirectory(const WorldDirectory* directory) { (void)directory; }
		virtual void			BindWorldArchetypeDatabase(const WorldArchetypeDatabase* database) { (void)database; }
		virtual void			BindWorldTemplateDatabase(const WorldTemplateDatabase* database) { (void)database; }

		void					SetResolveMode(eWorldResolveMode mode) { m_resolveMode = mode; }
		void					SetFallbackConfig(const WorldConfig& config) { m_fallbackConfig = config; }

	protected:
		eWorldResolveMode	m_resolveMode	  = eWorldResolveMode::CreateIfMissing;
		WorldConfig			m_fallbackConfig  = {};
	};
}
