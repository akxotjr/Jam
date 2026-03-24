#pragma once
#include <jampx/PhysicsTypes.h>


namespace jam::net
{
	struct ReplayRelevanceConfig
	{
		float enterRadius = 8.0f;
		float leaveRadius = 10.f;
		uint32 minHoldTicks = 3;
	};

	class ClientReplaySystem
	{
	public:
		explicit ClientReplaySystem(entt::registry& world);

		void			Init(const ReplayRelevanceConfig& cfg = {});
		void			Tick();

	private:
		void			UpdateCharacterCandidates(entt::entity local, const px::Vec3& localPos);
		void			UpdateRigidCandidates(const px::Vec3& localPos);

	private:
		entt::registry&			m_world;
		ReplayRelevanceConfig	m_cfg	= {};
	};

} // namespace jam::net
