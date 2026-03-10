#pragma once


namespace jam::px
{
	class IRigidBehavior
	{
	public:
		virtual ~IRigidBehavior() = default;

		virtual void Tick(RigidBody& body, float dt) = 0;
		virtual eActorType GetActorType() const = 0;
	};

} // namespace jam::px

