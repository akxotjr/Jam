#pragma once

#include "jamnet/runtime/world/IWorldAssignmentPolicy.h"
#include "jamnet/runtime/world/WorldDirectory.h"
#include "jamnet/runtime/world/WorldDescAsset.h"

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
		void							BindWorldTemplateAsset(const WorldDescAsset* asset) override { m_asset = asset; }

	private:
		uint32								ResolveDescId(const WorldActionRequest& req) const;
		const WorldDesc*					ResolveDesc(const WorldActionRequest& req) const;
		std::optional<WorldMeta>	SelectWorld(const WorldDesc& desc, std::span<const WorldMeta> candidates) const;

	private:
		const WorldDirectory*	m_directory = nullptr;
		const WorldDescAsset*	m_asset		= nullptr;
	};
}
