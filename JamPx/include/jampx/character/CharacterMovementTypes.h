#pragma once



namespace jam::px
{
	struct CharacterUserData
	{
		ObjectId id = INVALID_OBJ_ID;
		
		bool isGhost = false;
		bool isRemote = false;
	};

	static CharacterUserData* GetCharacterUserData(const PxController& cct)
	{
		return static_cast<CharacterUserData*>(cct.getUserData());
	}


	struct MoveCollision
	{
		enum Enum : uint8_t
		{
			NONE	= 0,
			SIDES	= 1 << 0,
			UP		= 1 << 1,
			DOWN	= 1 << 2,
		};

		using Flags = jam::FlagsT<Enum, uint32_t>;
	};


	enum class eStance : uint8
	{
		Standing, 
		Crouching, 
		Prone
	};

	enum class eGait : uint8
	{
		Walk, 
		Run, 
		Sprint
	};

	enum class eAirState : uint8
	{
		Grounded,
		Rising,
		Falling,
	};

	struct StanceConfig
	{
		// standing
		float				standingHeight			= 1.0f;			// absolute

		// crouch
		float				crouchHeight			= 0.5f;			// absolute
		float				crouchSpeedMultiplier	= 0.6f;
		bool				holdToCrouch			= true;			// hold or toggle

		// prone
		float				proneHeight				= 0.2f;			// absolute
		float				proneSpeedMultiplier	= 0.0f;
		bool				holdToProne				= false;		// hold or toggle
	};

	struct GaitConfig
	{
		// walk
		float				walkSpeedMultiplier		= 1.0f;

		// run
		float				runSpeedMultiplier		= 1.2f;

		// sprint
		float				sprintSpeedMultiplier	= 1.4f;
		float				sprintAccelMultiplier	= 1.2f;
		float				sprintMinSpeedToStart;
		bool				sprintAllowInAir		= false;
	};

	struct JumpConfig
	{
		float				speed				= 10.0f;
		float				coyoteTime			= 0.1f;
		float				jumpBuffer			= 0.1f;
		bool				edgeTrigger			= true;
	};


	struct JumpState
	{
		float				coyoteRemain		= 0.f;
		float				bufferRemain		= 0.f;
	};


	struct DashConfig 
	{
		float				speed				= 30.f;
		float				duration			= 0.2f;
		bool				overrideLocomotion	= true;
		bool				allowInAir			= true;
		bool				endOnCollision		= true; 
		float				steerFactor			= 0.0f;   // 대쉬 중 조향 허용 비율(선택)
	};

	struct DashState
	{
		bool				active				= false;
		float				remain				= 0.f;
		float				cooldownRemain		= 0.f;
		int					charges				= 0;
		Vec3				dir					= Vec3::Zero();
	};



	struct CharacterMoveConfig
	{
		// -- Gravity --
		
		float				gravity				= 25.0f;

		// -- Ground --

		float				groundAccel			= 60.0f;
		float				groundFriction		= 8.0f;
		float				groundMaxSpeed		= 10.0f;

		// -- Air --

		float				airAccel			= 10.0f;
		float				airMaxSpeed			= 10.0f;

		// -- policy of Cap --

		bool				capHorizontalOnly	= true;
		float				hardSpeedCapAir		= 7.5f;
		float				softCapStartAir		= 7.0f;
		float				softCapStrengthAir	= 20.0f;

		StanceConfig		stance;
		GaitConfig			gait;
		JumpConfig			jump;
		DashConfig			dash;
	};


	struct MoveIntent
	{
		float				moveX			= 0.0f;			// local x. [-1, 1]
		float				moveY			= 0.0f;			// local y. [-1, 1]
		float				moveYaw			= 0.0f;			// world yaw. rad
		float				moveMag			= 1.0f;			// input intensity. keyboard is always '1' and gamepad is [0, 1] 

		// --- AI path: world-space 직접 지정 (nullopt 이 아니라면 local 변환 스킵) ---
		std::optional<Vec3>	wishDir			= std::nullopt;	// world-space normalized dir

		eStance				stanceRequest	= eStance::Standing;
		eGait				gaitRequest		= eGait::Walk;

		bool				jumpPressed		= false;		// edge-trigger
		bool				dashPressed		= false;		// edge-trigger
		
		bool				jumpHeld		= false;		// optional holds (for variable jump / special)
	};


	struct CharacterMoveState
	{
		// pose
		Vec3		position		= Vec3::Zero();
		Vec3		velocity		= Vec3::Zero();
		float		bodyYaw			= 0.f;

		// modes
		eStance		stance			= eStance::Standing;
		eGait		gait			= eGait::Run;		
		eAirState	air				= eAirState::Grounded;

		// grounding
		Vec3		groundNormal{ 0,1,0 };
		bool		grounded		= true;

		//todo optional: debug/snap/step-down support
		//float		groundDist = 0.f;

		// ceiling
		bool		ceiling			= false;

		// events (1 tick)
		bool		justLanded		= false;
		bool		justJumped		= false;

		// jump & dash
		JumpState	jump{};
		DashState	dash{};

		//todo optional: platform support
		//uint32	 groundBodyId = 0;					// 움직이는 플랫폼 추적용
		//Vec3     platformDelta{ 0,0,0 };		// 이번 틱 플랫폼이 밀어준 delta (또는 별도 시스템)
	};




	// -- helpers -- 


	static float GetStanceHeight(const StanceConfig& cfg, eStance stance)
	{
		switch (stance)
		{
		case eStance::Standing:		return cfg.standingHeight;
		case eStance::Crouching:	return cfg.crouchHeight;
		case eStance::Prone:		return cfg.proneHeight;
		}
		return cfg.standingHeight;
	}

	static float GetStanceSpeedMultiplier(const StanceConfig& cfg, eStance stance)
	{
		switch (stance)
		{
		case eStance::Standing:		return 1.0f;
		case eStance::Crouching:	return cfg.crouchSpeedMultiplier;
		case eStance::Prone:		return cfg.proneSpeedMultiplier;
		}
		return 1.0f;
	}

	static float GetGaitSpeedMultiplier(const GaitConfig& cfg, eGait gait)
	{
		switch (gait)
		{
		case eGait::Walk:			return cfg.walkSpeedMultiplier;
		case eGait::Run:			return cfg.runSpeedMultiplier;
		case eGait::Sprint:			return cfg.sprintSpeedMultiplier;
		}
		return cfg.walkSpeedMultiplier;
	}

	static float GetGaitAccelMultiplier(const GaitConfig& cfg, eGait g)
	{
		return (g == eGait::Sprint) ? cfg.sprintAccelMultiplier : 1.0f;
	}



	static float HorizontalSpeed(Vec3 vel)
	{
		vel.y = 0.0f;
		return vel.Magnitude();
	}

	static Vec2 HorizontalDir(Vec3 vel)
	{
		return Vec2(vel.x, vel.z).GetNormalized();
	}

}
