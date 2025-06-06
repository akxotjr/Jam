#pragma once


class Room
{
public:
	Room();
	~Room();




private:
	uint32 m_roomId = 0;

	entt::registry m_registry;
};

