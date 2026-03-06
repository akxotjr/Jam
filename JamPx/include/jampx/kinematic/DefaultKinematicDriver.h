#pragma once
#include "IKinematicDriver.h"


namespace jam::px
{
	enum class eKinematicPathMode : uint8
	{
		Loop,			// A->B->C->A
		PingPong,		// A->B->C->B->A
		Once,			// A->B->C (stop)
	};


	struct KinematicWaypoint
	{
		Transform	pose{};
		float		pauseDuration = 0.f; // waiting time after arrival
	};

	struct DefaultKinematicDriverConfig
	{
		eKinematicPathMode			pathMode	= eKinematicPathMode::Once;
		float						speed		= 5.f; // m/s
		vector<KinematicWaypoint>	waypoints;
	};

	struct DefaultKinematicDriverState
	{
		int32		segmentIdx	= 0;		// start waypoint index of current segment
		int32		direction	= 1;		// +1 / -1 (only using PingPong)
		float		segProgress	= 0.f;		// progress in segment [0, 1]
		float		pauseTimer	= 0.f;
		bool		done		= false;
		Transform	pose{};
	};


	/// @brief Kinematic Driver based on waypoint 
	/// support Loop / PingPong / Once mode
	class DefaultKinematicDriver : public IKinematicDriver
	{
	public:
		explicit DefaultKinematicDriver(const DefaultKinematicDriverConfig& cfg);

		Transform	Tick(float dt) override;
		bool		IsDone() const override { return m_state.done; }

	private:
		int32		NextIndex() const;
		void		AdvanceSegment();

	private:
		DefaultKinematicDriverConfig	m_config{};
		DefaultKinematicDriverState		m_state{};
	};
}

