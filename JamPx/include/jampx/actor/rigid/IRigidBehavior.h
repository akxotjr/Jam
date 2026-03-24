#pragma once


namespace jam::px
{
	class RigidBody;

	class IRigidBehavior
	{
	public:
		virtual ~IRigidBehavior() = default;

		virtual void		TickOnMain(RigidBody& body, float dt) = 0;
		virtual void		TickOnReplay(RigidBody& body, float dt) = 0;

		virtual bool		SyncMainState(RigidBody& body) = 0;
		virtual void		SyncReplayState(RigidBody& body) = 0;

		virtual eActorType	GetActorType() const = 0;

		virtual bool		ApplyMainState(const RigidState& state) { return false; }
		virtual bool		ApplyReplayState(const RigidState& state) { return false; }
	};

} // namespace jam::px

