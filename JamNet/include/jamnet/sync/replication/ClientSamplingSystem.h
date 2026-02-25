#pragma once


namespace jam::net
{
	class ClientSamplingSystem
	{
	public:
		explicit ClientSamplingSystem(entt::registry& world) : m_world(world) {}

		void			Init();
		void			Tick();

	private:
		entt::registry& m_world;
	};

}
