#pragma once

#include "jamnet/runtime/world/WorldActionTypes.h"

#include <memory>

namespace jam::net
{
	class WorldDirectory;
	struct WorldDescAsset;

	class IWorldAssignmentPolicy
	{
	public:
		virtual ~IWorldAssignmentPolicy() = default;

		virtual WorldActionPlan PlanAction(const WorldActionRequest& req) = 0;
		virtual void			BindWorldDirectory(const WorldDirectory* directory) { (void)directory; }
		virtual void			BindWorldTemplateAsset(const WorldDescAsset* asset) { (void)asset; }

		void					SetResolveMode(eWorldResolveMode mode) { m_resolveMode = mode; }
		void					SetFallbackConfig(const WorldConfig& config) { m_fallbackConfig = config; }

	protected:
		eWorldResolveMode	m_resolveMode	  = eWorldResolveMode::CreateIfMissing;
		WorldConfig			m_fallbackConfig  = {};
	};
}
