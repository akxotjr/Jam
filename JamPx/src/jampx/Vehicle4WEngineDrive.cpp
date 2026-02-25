#include "pch.h"
#include "Physics/Vehicle4WEngineDrive.h"
#include <future>
#include "Physics/PhysicsFilters.h"

namespace jam::px
{
    namespace
    {
        inline float Clamp01(float v)
        {
	        return PxClamp(v, 0.0f, 1.0f);
        }

        inline float Clamp11(float v)
        {
	        return PxClamp(v, -1.0f, 1.0f);
        }
        
        inline float ApplyDeadzone(float v, float dz)
        {
            float a = fabsf(v);
            if (a <= dz) return 0.0f;
            float s = (a - dz) / (1.0f - dz);
            return copysignf(s, v);
        }

        inline float ExpoShape(float v, float exponent)
        {
            // exponent>1 → 중심부 민감도↓(더 안정적)
            return copysignf(powf(fabsf(v), exponent), v);
        }

        inline float ClampRate(float prev, float target, float risePerSec, float fallPerSec, float dt)
        {
            float delta = target - prev;
            float limit = (delta >= 0.0f ? risePerSec : fallPerSec) * dt;
            if (fabsf(delta) > fabsf(limit)) return prev + copysignf(limit, delta);
            return target;
        }


        inline float Lerp(float a, float b, float t)
        {
	        return a + (b - a) * PxClamp(t, 0.0f, 1.0f);
        }

        inline float Remap01(float x, float x0, float x1)
        {
	        return PxClamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
        }


        // 속도에 따른 스티어 게인(고속에서 조향 감도 하향)
        inline float SteerGainForSpeedKmh(float v)
        {
            // 0km/h→1.0, 120km/h→0.35 선형
            return lerp(1.0f, 0.35f, Remap01(v, 0.0f, 120.0f));
        }

        // 속도에 따른 최종 스티어 캡(정규화 입력 기준)
        inline float SteerCapForSpeedKmh(float v)
        {
            // 0km/h→1.0, 80km/h→0.6, 150km/h→0.35
            float t1 = Remap01(v, 0.0f, 80.0f);
            float t2 = Remap01(v, 80.0f, 150.0f);
            float mid = Lerp(1.0f, 0.6f, t1);
            return Lerp(mid, 0.35f, t2);
        }
    }
    




    void Vehicle4WEngineDrive::Init(const Vehicle4WEngineDriveDesc& desc)
    {
        m_desc = desc;
        if (!desc.scene) return;

        m_frame.setToDefault();
        m_frame.lngAxis = PxVehicleAxes::ePosZ;  
        m_frame.latAxis = PxVehicleAxes::ePosX;  
        m_frame.vrtAxis = PxVehicleAxes::ePosY;  

        // Simulation context
        m_simCtx.setToDefault();
        m_simCtx.frame = m_frame;
        m_simCtx.gravity = PxVec3(0, -9.81f, 0);
        m_simCtx.physxScene = desc.scene;

        // Commands default
        m_commands.setToDefault();
        m_commands.nbBrakes = 2;

        m_transmissionCommands.setToDefault();
        m_transmissionCommands.clutch       = 0.0f;
        m_transmissionCommands.targetGear   = PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR;

        BuildWheelsSuspension(desc);
        BuildRigidBody(desc);
        BuildTiresAndFricition(desc);
        BuildDrivetrain(desc);
        BuildSteerAndAckermann(desc);
        BuildBrakes(desc);
        BuildAntiRollBars(desc);
        BuildRoadAndFriction(desc);

        // Build sequence
        CreateSequence();
    }

    void Vehicle4WEngineDrive::UpdateInput(const Vehicle4WEngineDriveControl& input)
    {
        m_control = input;

        const float dt = net::SIMULATION_TICK_NS;
        const float speedKmh = GetSpeedKmh();
        
    	// --- Steering ---
    	const float steerShaped = ShapeSteer(input.steer, speedKmh, dt);
        m_filteredInput.steer = steerShaped;
        m_commands.steer = steerShaped; // [-1,1]
        
    	// --- Throttle ---
    	//const float throttleShaped = ShapeThrottle(input.throttle, steerShaped, speedKmh, dt);
     //   m_filteredInput.throttle = throttleShaped;
     //   m_commands.throttle = throttleShaped; // [0,1]
        m_commands.throttle = input.throttle;
        
    	// --- Brake / Handbrake ---
    	const float brakeShaped = ShapeBrake(input.brake, dt);
        m_filteredInput.brake = brakeShaped;
        m_filteredInput.handbrake = Clamp01(input.handbrake);
        
    	m_commands.brakes[0] = brakeShaped;                 // foot brake
        m_commands.brakes[1] = m_filteredInput.handbrake;   // hand brake
        m_commands.nbBrakes = 2;

        if (input.autoMode)
        {
            m_transmissionCommands.targetGear = PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR;
            m_transmissionCommands.clutch = 0.0f;  // 클러치 완전 결합
        }
        else  // 수동 모드
        {
            if (input.shiftUp || input.shiftDown)
            {
                // 변속 중: 클러치 분리
                m_transmissionCommands.targetGear = input.shiftUp ? m_gearboxState.currentGear + 1 : PxMax(0u, m_gearboxState.currentGear - 1);
                m_transmissionCommands.clutch = 1.0f;  // 변속 시 분리
            }
            else
            {
                m_transmissionCommands.clutch = 0.0f;  // 일반 주행 시 결합
            }
        }
    }

    void Vehicle4WEngineDrive::UpdateSequence()
    {
        m_sequence.update(net::SIMULATION_TICK_SEC, m_simCtx);


        DebugDumpVehicleState();
		//DebugCheckGroundContactRaycast(m_desc.scene);
  //      

  //      std::cout << "[Dbg] contraints group nbConstraintGroups=" << m_constraintGroupState.getNbConstraintGroups() << endl;
    }


    void Vehicle4WEngineDrive::BuildRigidBody(const Vehicle4WEngineDriveDesc& desc)
    {
        const PxVec3 he(0.5f * desc.chassisSize.x, 0.5f * desc.chassisSize.y, 0.5f * desc.chassisSize.z);

        PxTransform pose = desc.pose;
        if (!pose.q.isFinite() || pose.q.magnitudeSquared() < 1e-6f) pose.q = PxQuat(PxIdentity); else pose.q.normalize();

        const float m = desc.chassisMass;

        m_rigidBodyParams.mass = m;
        const float sx = desc.chassisSize.x;
        const float sy = desc.chassisSize.y;
        const float sz = desc.chassisSize.z;
        m_rigidBodyParams.moi = PxVec3(
            (m * (sy * sy + sz * sz)) / 12.0f,
            (m * (sx * sx + sz * sz)) / 12.0f,
            (m * (sx * sx + sy * sy)) / 12.0f
        );

        PX_ASSERT(m_rigidBodyParams.isValid());

        const PxTransform comLocalPose(PxVec3(0, -desc.comLowering, 0));


        // --- Vehicle 헬퍼 파라미터 구성 ---
        auto& sdk = PhysicsCore::Instance();
        auto& physics = *sdk.Physics();

        // 차체(박스) 지오메트리 & 로컬 포즈
        PxBoxGeometry       chassisGeom(he);
        const PxTransform   chassisLocalPose(PxIdentity);


        const PxFilterData  chassisSimFD = MakeFilterData(LAYER_VEHICLE, LAYER_STATIC | LAYER_DYNAMIC | LAYER_CHARACTER);
        const PxFilterData  chassisQryFD{};
        const PxShapeFlags  chassisFlags(PxShapeFlag::eSIMULATION_SHAPE);

        const PxFilterData  wheelSimFD = MakeFilterData(LAYER_VEHICLE, 0);
        const PxFilterData  wheelQryFD{};
		const PxShapeFlags  wheelFlags(0);

        PxVehiclePhysXRigidActorParams      rigidActorParams(m_rigidBodyParams, "JamVehicle");
        PxVehiclePhysXRigidActorShapeParams rigidActorShapeParams(chassisGeom, chassisLocalPose, *sdk.ChassisMaterial(), chassisFlags, chassisSimFD, chassisQryFD);
        PxVehiclePhysXWheelParams           wheelParams(m_axleDesc, m_wheelParams);
        PxVehiclePhysXWheelShapeParams      wheelShapeParams(*sdk.WheelMaterial(), wheelFlags, wheelSimFD, wheelQryFD);
        PxCookingParams                     cookingParams(physics.getTolerancesScale());

        PxVehiclePhysXActorCreate(
            m_frame,              
            rigidActorParams, 
            comLocalPose,
            rigidActorShapeParams,
            wheelParams, 
            wheelShapeParams,
            physics,
            cookingParams,
            m_physxActor
        );


        PX_ASSERT(m_physxActor.rigidBody);

        m_physxActor.rigidBody->setGlobalPose(pose);
        m_physxActor.rigidBody->setLinearDamping(0.02f);  
        m_physxActor.rigidBody->setAngularDamping(0.2f); 

        m_rigidBodyState.setToDefault();
        desc.scene->addActor(*m_physxActor.rigidBody);


        PxVehicleConstraintsCreate(m_axleDesc, physics, *m_physxActor.rigidBody, m_constraints);
        m_constraintGroupState.setToDefault();
    }

    void Vehicle4WEngineDrive::BuildWheelsSuspension(const Vehicle4WEngineDriveDesc& desc)
    {
        // Axles: [0, 1] = front, [2, 3] = rear
        m_axleDesc.setToDefault();
        constexpr PxU32 front[2] = { kFL, kFR };
        constexpr PxU32 rear[2] = { kRL, kRR };

        m_axleDesc.addAxle(2, front);
        m_axleDesc.addAxle(2, rear);

        PX_ASSERT(m_axleDesc.nbWheels == kWheelCount);


        const float xFR = +0.5f * desc.frontTrackWidth;
		const float xFL = -0.5f * desc.frontTrackWidth;
        const float xRR = +0.5f * desc.rearTrackWidth;
        const float xRL = -0.5f * desc.rearTrackWidth;
        const float zF  = +desc.frontWheelBase;
        const float zR  = -desc.rearWheelBase;

        const float wheelAttachY = -0.5f * desc.chassisSize.y;

        const PxVec3 travelDir(0.f, -1.f, 0.f);
        const float  travelDist = desc.compressionMax + desc.droopMax;

        const PxVec3 restPos[kWheelCount] = {
            PxVec3(xFL, wheelAttachY, zF), // FL
            PxVec3(xFR, wheelAttachY, zF), // FR
            PxVec3(xRL, wheelAttachY, zR), // RL
            PxVec3(xRR, wheelAttachY, zR)  // RR
        };


        for (PxU32 i = 0; i < kWheelCount; ++i)
        {
            // Wheel

            // Wheel params
            auto& wp = m_wheelParams[i];
            wp.mass         = desc.wheelMass;
            wp.radius       = desc.wheelRadius;
            wp.halfWidth    = 0.5f * desc.wheelWidth;
            wp.moi          = 0.5f * wp.mass * wp.radius * wp.radius;      
            wp.dampingRate  = 1.2f;                                        
            PX_ASSERT(wp.isValid());

            // Wheel Local Position
            m_wheelLocalPositions[i] = restPos[i] - desc.comOffset;

            // Wheel Local Pose
            m_wheelLocalPoses[i].setToDefault();
            m_wheelLocalPoses[i].localPose = PxTransform(m_wheelLocalPositions[i], PxQuat(PxIdentity));
            m_wheelLocalPoses[i].localPose.q.normalize();

            // Wheel Shape Local Pose
            m_wheelShapeLocalPoses[i] = PxTransform(PxQuat(PxIdentity));

            m_wheelActuationState[i].setToDefault();    // Wheel Actuation State
            m_wheelRB1dStates[i].setToDefault();        // Wheel RigidBody 1D State
        }


        PxReal sprungMasses[kWheelCount];
        if (!vehicle2::PxVehicleComputeSprungMasses(kWheelCount, m_wheelLocalPositions, desc.chassisMass, PxVehicleAxes::eNegY, sprungMasses))
        {
            return;
        }

        constexpr PxReal naturalHz      = 5.0f;
        constexpr PxReal dampingRatio   = 1.2f;

        for (PxU32 i = 0; i < kWheelCount; ++i)
        {
			// Suspension

            // Suspension params
            auto& sp = m_suspParams[i];
            sp.suspensionAttachment = PxTransform(m_wheelLocalPositions[i] - travelDir * desc.compressionMax);
            sp.suspensionTravelDir  = travelDir;
            sp.suspensionTravelDist = travelDist;
            sp.wheelAttachment      = PxTransform(PxIdentity);
            PX_ASSERT(sp.isValid());

            auto& sc = m_suspCalcParams[i];
            sc.limitSuspensionExpansionVelocity = true;
            sc.suspensionJounceCalculationType  = PxVehicleSuspensionJounceCalculationType::eRAYCAST;
            PX_ASSERT(sc.isValid());

            auto& sf = m_suspForceParams[i];
            sf.sprungMass   = sprungMasses[i];
            sf.stiffness    = naturalHz * naturalHz * sprungMasses[i];
            sf.damping      = dampingRatio * 2 * sqrt(sf.stiffness * sprungMasses[i]);
            PX_ASSERT(sf.isValid());

            auto& cp = m_suspComplianceParams[i];
            cp.suspForceAppPoint.nbDataPairs    = 0;
            cp.tireForceAppPoint.nbDataPairs    = 0;
            cp.wheelCamberAngle.nbDataPairs     = 0;
            cp.wheelToeAngle.nbDataPairs        = 0;
            PX_ASSERT(cp.isValid());

            auto& lp = m_suspLimitParams[i];
            lp.restitution                              = 0.0f;
            lp.directionForSuspensionLimitConstraint    = PxVehiclePhysXSuspensionLimitConstraintParams::eROAD_GEOMETRY_NORMAL;
            PX_ASSERT(lp.isValid());

            m_suspStates[i].setToDefault();
            m_suspComplianceStates[i].setToDefault();
        }
    }

    void Vehicle4WEngineDrive::BuildTiresAndFricition(const Vehicle4WEngineDriveDesc& desc)
    {
        for (PxU32 i = 0; i < kWheelCount; ++i)
        {
            PxVehicleTireForceParams& tf = m_tireForceParams[i];

            // restLoad 
            const float sprungMass = m_suspForceParams[i].sprungMass;       
            const float wheelMass  = m_wheelParams[i].mass;
            tf.restLoad = (sprungMass + wheelMass) * 9.81f;

            // lateral stiffness
            tf.latStiffX = 2.0f;          
            tf.latStiffY = 100000.0f;      

            // longitudinal stiffness
            tf.longStiff = 9000.0f;      

            // camber stiffness
            tf.camberStiff = 10000.0f;    

            // friction vs longitudinal slip
            tf.frictionVsSlip[0][0] = 0.0f;     tf.frictionVsSlip[0][1] = 1.0f; 
            tf.frictionVsSlip[1][0] = 0.5f;     tf.frictionVsSlip[1][1] = 1.0f; 
            tf.frictionVsSlip[2][0] = 1.0f;     tf.frictionVsSlip[2][1] = 1.0f; 

            // load filter
            tf.loadFilter[0][0] = 0.0f;         tf.loadFilter[0][1] = 0.0f;
            tf.loadFilter[1][0] = 1000.0f;      tf.loadFilter[1][1] = 1000.0f;

            PX_ASSERT(tf.isValid());

            m_tireForces[i].setToDefault();
            m_tireDirectionStates[i].setToDefault();
            m_tireSpeedStates[i].setToDefault();
            m_tireSlipStates[i].setToDefault();
            m_tireStickyStates[i].setToDefault();
            m_tireGripStates[i].setToDefault();
            m_tireCamberAngleStates[i].setToDefault();
        }
    }

    void Vehicle4WEngineDrive::BuildDrivetrain(const Vehicle4WEngineDriveDesc& desc)
    {
        // -----------------------
        //      Engine
        // -----------------------
        auto rpm2rad = [](float rpm) -> float { return rpm * PxTwoPi / 60.0f; };

        m_engineParams.torqueCurve.clear();
        m_engineParams.torqueCurve.addPair(0.00f, 0.00f); 
        m_engineParams.torqueCurve.addPair(0.20f, 0.55f);
        m_engineParams.torqueCurve.addPair(0.40f, 0.95f);
        m_engineParams.torqueCurve.addPair(0.60f, 1.00f); // 피크
        m_engineParams.torqueCurve.addPair(0.80f, 0.92f);
        m_engineParams.torqueCurve.addPair(1.00f, 0.70f); // 레드존 부근에서 토크 하강

        m_engineParams.moi          = 1.0f;
        m_engineParams.peakTorque   = 600.0f;
        m_engineParams.idleOmega    = rpm2rad(900.0f);      // 공회전
        m_engineParams.maxOmega     = rpm2rad(7500.0f);     // 최대 회전

        m_engineParams.dampingRateFullThrottle                  = 0.25f;
        m_engineParams.dampingRateZeroThrottleClutchEngaged     = 2.0f;
        m_engineParams.dampingRateZeroThrottleClutchDisengaged  = 0.5f;

        PX_ASSERT(m_engineParams.isValid());

        m_engineState.setToDefault();
        m_engineThrottleResponseState.setToDefault();

        // Clutch

        m_clutchParams.accuracyMode         = physx::vehicle2::PxVehicleClutchAccuracyMode::eBEST_POSSIBLE;
        m_clutchParams.estimateIterations   = 1; // BEST_POSSIBLE이면 의미 없음
        PX_ASSERT(m_clutchParams.isValid());

        m_clutchCommandResponseParams.maxResponse = 10.0f;
        PX_ASSERT(m_clutchCommandResponseParams.isValid());

        m_clutchSlipState.setToDefault();
        m_clutchCommandResponseState.setToDefault();

        // Gearbox

        // 0: Reverse, 1: Neutral, 2..N: Forward gears

        m_gearboxParams.nbRatios    = 8;                // R + N + 6 = 8
        m_gearboxParams.neutralGear = 1;

        for (float& ratio : m_gearboxParams.ratios)
	        ratio = 0.0f;

        m_gearboxParams.ratios[0] = -3.6f;    // Reverse
        m_gearboxParams.ratios[1] = 0.0f;     // Neutral
        m_gearboxParams.ratios[2] = 3.2f;     // 1단
        m_gearboxParams.ratios[3] = 2.1f;     // 2단
        m_gearboxParams.ratios[4] = 1.5f;     // 3단
        m_gearboxParams.ratios[5] = 1.2f;     // 4단
        m_gearboxParams.ratios[6] = 1.0f;     // 5단
        m_gearboxParams.ratios[7] = 0.8f;     // 6단

        m_gearboxParams.finalRatio = 3.42f;    // 전체 감속비 (디퍼렌셜)
        m_gearboxParams.switchTime = 0.25f;    // 기어 전환 시간

        PX_ASSERT(m_gearboxParams.isValid());

        m_gearboxState.setToDefault();

        // Autobox

        for (PxU32 i = 0; i < PxVehicleGearboxParams::eMAX_NB_GEARS; ++i) 
        {
            m_autoboxParams.upRatios[i]     = 999.0f;
            m_autoboxParams.downRatios[i]   = 0.0f;
        }

        // Forward gears
        m_autoboxParams.upRatios[2]   = 0.85f; // 1단 → 2단
        m_autoboxParams.downRatios[2] = 0.40f;

        m_autoboxParams.upRatios[3]   = 0.84f; // 2단 → 3단
        m_autoboxParams.downRatios[3] = 0.44f;

        m_autoboxParams.upRatios[4]   = 0.83f; // 3단 → 4단
        m_autoboxParams.downRatios[4] = 0.46f;

        m_autoboxParams.upRatios[5]   = 0.82f; // 4단 → 5단
        m_autoboxParams.downRatios[5] = 0.50f;

        m_autoboxParams.upRatios[6]   = 0.82f; // 5단 → 6단
        m_autoboxParams.downRatios[6] = 0.55f;

        m_autoboxParams.latency = 0.5f; // 다음 변속까지 최소 1초 대기

        PX_ASSERT(m_autoboxParams.isValid(m_gearboxParams));

        m_autoboxState.setToDefault();

        ConfigureDifferentialAndDriveWheels(desc.isRwd);
    }

    void Vehicle4WEngineDrive::BuildSteerAndAckermann(const Vehicle4WEngineDriveDesc& desc)
    {
        // Steer Command Response

        m_steerResponseParams.maxResponse = 0.38f; // 21.8° 
        for (float& wheelResponseMultiplier : m_steerResponseParams.wheelResponseMultipliers)
	        wheelResponseMultiplier = 0.0f;
        m_steerResponseParams.wheelResponseMultipliers[kFL] = 0.5f;
        m_steerResponseParams.wheelResponseMultipliers[kFR] = 0.5f;
        m_steerResponseParams.wheelResponseMultipliers[kRL] = 0.0f;
        m_steerResponseParams.wheelResponseMultipliers[kRR] = 0.0f;

        PX_ASSERT(m_steerResponseParams.isValid(m_axleDesc));

        m_steerState.setToDefault();
        memset(m_steerResponseStates, 0.f, kWheelCount * sizeof(PxReal));

        // Ackermann

        m_ackermannParams.wheelIds[0]   = kFL;  
        m_ackermannParams.wheelIds[1]   = kFR;  
        m_ackermannParams.trackWidth    = desc.frontTrackWidth;
        m_ackermannParams.wheelBase     = desc.frontWheelBase + desc.rearWheelBase;
        m_ackermannParams.strength      = 0.8f; 

        PX_ASSERT(m_ackermannParams.isValid(m_axleDesc));
    }

    void Vehicle4WEngineDrive::BuildBrakes(const Vehicle4WEngineDriveDesc& desc)
    {
        float avgRadius     = 0.0f;
        PxU32 wheelCount    = m_axleDesc.nbWheels;
        for (PxU32 i = 0; i < wheelCount; ++i)
            avgRadius += m_wheelParams[m_axleDesc.wheelIdsInAxleOrder[i]].radius;
        avgRadius = (wheelCount > 0) ? (avgRadius / wheelCount) : 0.33f;

        const float mass    = m_rigidBodyParams.mass;        // 차량 질량(kg)
        const float g       = 9.81f;
        const float a_max   = 1.05f * g;              // 목표 최대 제동 감속(≈1.05 g) — 트랙/타이어에 맞춰 조절
        const float T_total = mass * a_max * avgRadius; // 차량 전체 허브 기준 브레이크 토크(N·m)

        // Foot Brake
        // 전륜 60% / 후륜 40% 배분

        for (float& wheelResponseMultiplier : m_brakeCommandResponseParams[0].wheelResponseMultipliers)
	        wheelResponseMultiplier = 0.0f;

        m_brakeCommandResponseParams[kFootBrake].maxResponse                     = T_total;  // brakeCommand=1일 때 차량 전체 최대 토크
        m_brakeCommandResponseParams[kFootBrake].wheelResponseMultipliers[kFL]   = 0.30f;    // 앞왼 30%
        m_brakeCommandResponseParams[kFootBrake].wheelResponseMultipliers[kFR]   = 0.30f;    // 앞오 30%
        m_brakeCommandResponseParams[kFootBrake].wheelResponseMultipliers[kRL]   = 0.20f;    // 뒤왼 20%
        m_brakeCommandResponseParams[kFootBrake].wheelResponseMultipliers[kRR]   = 0.20f;    // 뒤오 20%

        PX_ASSERT(m_brakeCommandResponseParams[0].isValid(m_axleDesc));

        // Handbrake
        // 보통 후륜만 강하게 잡음. 필요에 따라 드리프트/락업 느낌 조절.
        for (float& wheelResponseMultiplier : m_brakeCommandResponseParams[1].wheelResponseMultipliers)
	        wheelResponseMultiplier = 0.0f;

        // 핸드브레이크는 별도 기준 토크(풋브레이크보다 좀 약하게 시작하거나, 반대로 더 강하게도 가능)
        // 여기서는 풋브레이크 기준의 0.8배로 설정.
        m_brakeCommandResponseParams[kHandBrake].maxResponse = 0.8f * T_total;

        // 후륜만 배분(합 1.0)    
        m_brakeCommandResponseParams[kHandBrake].wheelResponseMultipliers[kRL] = 0.50f;
        m_brakeCommandResponseParams[kHandBrake].wheelResponseMultipliers[kRR] = 0.50f;

        PX_ASSERT(m_brakeCommandResponseParams[1].isValid(m_axleDesc));


        memset(m_brakeResponseStates, 0.f, kWheelCount * sizeof(PxReal));
    }

    void Vehicle4WEngineDrive::BuildAntiRollBars(const Vehicle4WEngineDriveDesc& desc)
    {
        // front antiroll bar
        m_arbParams[0].wheel0       = kFL;  
        m_arbParams[0].wheel1       = kFR;  
        m_arbParams[0].stiffness    = 16000.f;  

        PX_ASSERT(m_arbParams[0].isValid());
			
        m_antiRollTorque.setToDefault();

        // rear antiroll bar
        m_arbParams[1].wheel0       = kRL;
        m_arbParams[1].wheel1       = kRR;
        m_arbParams[1].stiffness    = 14000.f;  

        PX_ASSERT(m_arbParams[1].isValid());
    }


    void Vehicle4WEngineDrive::BuildRoadAndFriction(const Vehicle4WEngineDriveDesc& desc)
    {
        // -----------------------------
        // 1) Road geometry (Raycast)
        // -----------------------------

       // m_roadQueryParams.defaultFilterData     = MakeVehicleRoadQueryFilterData(true, true);
        m_roadQueryParams.filterDataEntries     = nullptr; // per-wheel 오버라이드 없으면 nullptr
       // m_roadQueryParams.filterCallback        = &gVehicleDrivablePreFilter;
        m_roadQueryParams.roadGeometryQueryType = PxVehiclePhysXRoadGeometryQueryType::eRAYCAST;

        PX_ASSERT(m_roadQueryParams.isValid());

        for (PxU32 i = 0; i < kWheelCount; ++i)
        {
            m_roadGeometryStates[i].setToDefault();
            m_roadGeometryQueryStates[i].setToDefault();
        }

        // -----------------------------
        // 2) Material <-> Friction Mapping
        // -----------------------------

        for (PxU32 i = 0; i < kWheelCount; ++i)
        {
            m_frictionParams[i].materialFrictions = /*gMaterialFrictionTable.entries.empty() ? nullptr : gMaterialFrictionTable.entries.data()*/nullptr;
            m_frictionParams[i].nbMaterialFrictions = /*static_cast<PxU32>(gMaterialFrictionTable.entries.size())*/0;
            m_frictionParams[i].defaultFriction = 1.1f;

            PX_ASSERT(m_frictionParams[i].isValid());
        }
    }



    void Vehicle4WEngineDrive::ConfigureDifferentialAndDriveWheels(bool isRwd)
    {
        // Differential

        m_diffParams.setToDefault();

        for (float& r : m_diffParams.torqueRatios)         r = 0.0f;
        for (float& a : m_diffParams.aveWheelSpeedRatios)  a = 0.0f;

        m_diffParams.frontWheelIds[0]   = kFL;
    	m_diffParams.frontWheelIds[1]   = kFR;
        m_diffParams.rearWheelIds[0]    = kRL;
    	m_diffParams.rearWheelIds[1]    = kRR;

        m_diffParams.frontBias      = 1.3f;
    	m_diffParams.frontTarget    = 1.29f;
        m_diffParams.rearBias       = 1.3f;
    	m_diffParams.rearTarget     = 1.29f;
        m_diffParams.centerBias     = 1.3f;
    	m_diffParams.centerTarget   = 1.29f;
        m_diffParams.rate           = 10.0f;

        if (isRwd) 
        {
            // --- RWD: 뒤 50/50, 앞 0 ---
            m_diffParams.torqueRatios[kFL] = 0.0f;
            m_diffParams.torqueRatios[kFR] = 0.0f;
            m_diffParams.torqueRatios[kRL] = 0.5f;
            m_diffParams.torqueRatios[kRR] = 0.5f;

            m_diffParams.aveWheelSpeedRatios[kFL] = 0.0f;
            m_diffParams.aveWheelSpeedRatios[kFR] = 0.0f;
            m_diffParams.aveWheelSpeedRatios[kRL] = 0.5f;
            m_diffParams.aveWheelSpeedRatios[kRR] = 0.5f;

            m_wheelActuationState[kRL].isDriveApplied = true;
            m_wheelActuationState[kRR].isDriveApplied = true;
        }
        else 
        {
            // --- AWD(기본): 앞 40%(각 0.20), 뒤 60%(각 0.30) ---
            m_diffParams.torqueRatios[kFL] = 0.20f;
            m_diffParams.torqueRatios[kFR] = 0.20f;
            m_diffParams.torqueRatios[kRL] = 0.30f;
            m_diffParams.torqueRatios[kRR] = 0.30f;

            m_diffParams.aveWheelSpeedRatios[kFL] = 0.25f;
            m_diffParams.aveWheelSpeedRatios[kFR] = 0.25f;
            m_diffParams.aveWheelSpeedRatios[kRL] = 0.25f;
            m_diffParams.aveWheelSpeedRatios[kRR] = 0.25f;
        }

        m_diffState.setToDefault();
    }

    void Vehicle4WEngineDrive::CreateSequence()
    {
        m_sequence.add(static_cast<PxVehiclePhysXActorBeginComponent*>(this));
        m_sequence.add(static_cast<PxVehicleEngineDriveCommandResponseComponent*>(this));
        m_sequence.add(static_cast<PxVehicleFourWheelDriveDifferentialStateComponent*>(this));
        m_sequence.add(static_cast<PxVehicleEngineDriveActuationStateComponent*>(this));
        m_sequence.add(static_cast<PxVehiclePhysXRoadGeometrySceneQueryComponent*>(this));
        m_sequence.add(static_cast<PxVehicleSuspensionComponent*>(this));
        m_sequence.add(static_cast<PxVehicleTireComponent*>(this));
        m_sequence.add(static_cast<PxVehiclePhysXConstraintComponent*>(this));
        m_sequence.add(static_cast<PxVehicleEngineDrivetrainComponent*>(this));
        m_sequence.add(static_cast<PxVehicleRigidBodyComponent*>(this));
        m_sequence.add(static_cast<PxVehicleWheelComponent*>(this));
        m_sequence.add(static_cast<PxVehiclePhysXActorEndComponent*>(this));
    }

    float Vehicle4WEngineDrive::GetSpeedKmh() const
    {
        if (!m_physxActor.rigidBody) return 0.0f;
        return m_physxActor.rigidBody->getLinearVelocity().magnitude() * 3.6f;
    }

    float Vehicle4WEngineDrive::ShapeSteer(float raw, float speedKmh, float dt) const
    {
        // 1) 입력 범위 제한
        float x = Clamp11(raw);
        // 2) 데드존
    	x = ApplyDeadzone(x, 0.03f);
    	// 3) 속도 연동 게인
    	x *= SteerGainForSpeedKmh(speedKmh);
        // 4) 비선형 (expo>1이면 중심부 민감도↓)
    	x = ExpoShape(x, 1.6f);
        // 5) 변화율 제한 (정규화 입력 기준 초당 변화 한계)
        // 상승 2.5/s, 하강 3.5/s 정도로 부드럽게
        x = ClampRate(m_filteredInput.steer, x, 2.5f, 3.5f, dt);
        // 6) 속도 기반 최종 캡
        float cap = SteerCapForSpeedKmh(speedKmh);
        x = PxClamp(x, -cap, cap);
        return x;
    }

    float Vehicle4WEngineDrive::ShapeThrottle(float raw, float steerShaped, float speedKmh, float dt) const
    {
        float t = Clamp01(raw);
    	// 가속 곡선(미세 입력 둔감화)
    	t = ExpoShape(t, 1.25f);
        // 램프: 가속 상승 1.2/s, 감속 해제 3.0/s
        t = ClampRate(m_filteredInput.throttle, t, 1.2f, 3.0f, dt);
        // 고속·대조향 시 트랙션 보조(간단 스케일)
    	float steerAbs = fabsf(steerShaped);
        float steerFactor = Lerp(1.0f, 0.35f, Remap01(steerAbs, 0.2f, 1.0f));
        float speedFactor = Lerp(1.0f, 0.7f, Remap01(speedKmh, 40.0f, 160.0f));
        t *= (steerFactor * speedFactor);
        return PxClamp(t, 0.0f, 1.0f);
    }

    float Vehicle4WEngineDrive::ShapeBrake(float raw, float dt) const
    {
        float b = Clamp01(raw);
        // 브레이크는 빠르게 걸리고(상승 5/s), 서서히 풀림(하강 3/s)
    	b = ClampRate(m_filteredInput.brake, b, 5.0f, 3.0f, dt);
        return b;
    }


    // ----------------------- Component data wiring -----------------------

    void Vehicle4WEngineDrive::getDataForPhysXActorBeginComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleCommandState*& commands,
        const PxVehicleEngineDriveTransmissionCommandState*& transmissionCommands,
        const PxVehicleGearboxParams*& gearParams, 
        const PxVehicleGearboxState*& gearState,
        const PxVehicleEngineParams*& engineParams, 
        PxVehiclePhysXActor*& physxActor,
        PxVehiclePhysXSteerState*& physxSteerState, 
        PxVehiclePhysXConstraints*& physxConstraints,
        PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates, 
        PxVehicleEngineState*& engineState)
    {
        axleDescription         = &m_axleDesc;
        commands                = &m_commands;
        transmissionCommands    = &m_transmissionCommands;
        gearParams              = &m_gearboxParams;
        gearState               = &m_gearboxState;
        engineParams            = &m_engineParams;
        physxActor              = &m_physxActor;
        physxSteerState         = &m_steerState;
        physxConstraints        = &m_constraints;
        rigidBodyState          = &m_rigidBodyState;
        wheelRigidBody1dStates.setData(m_wheelRB1dStates);
        engineState             = &m_engineState;
    }

    void Vehicle4WEngineDrive::getDataForEngineDriveCommandResponseComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleSizedArrayData<const PxVehicleBrakeCommandResponseParams>& brakeResponseParams,
        const PxVehicleSteerCommandResponseParams*& steerResponseParams,
        PxVehicleSizedArrayData<const PxVehicleAckermannParams>& ackermannParams,
        const PxVehicleGearboxParams*& gearboxParams, 
        const PxVehicleClutchCommandResponseParams*& clutchResponseParams,
        const PxVehicleEngineParams*& engineParams, 
        const PxVehicleRigidBodyState*& rigidBodyState,
        const PxVehicleEngineState*& engineState, 
        const PxVehicleAutoboxParams*& autoboxParams,
        const PxVehicleCommandState*& commands,
        const PxVehicleEngineDriveTransmissionCommandState*& transmissionCommands,
        PxVehicleArrayData<PxReal>& brakeResponseStates,
        PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        PxVehicleArrayData<PxReal>& steerResponseStates, 
        PxVehicleGearboxState*& gearboxResponseState,
        PxVehicleClutchCommandResponseState*& clutchResponseState, 
        PxVehicleAutoboxState*& autoboxState)
    {
        axleDescription      = &m_axleDesc;
        brakeResponseParams.setDataAndCount(m_brakeCommandResponseParams, 2);
        steerResponseParams  = &m_steerResponseParams;
        ackermannParams.setDataAndCount(&m_ackermannParams, 1);

        gearboxParams        = &m_gearboxParams;
        clutchResponseParams = &m_clutchCommandResponseParams;
        engineParams         = &m_engineParams;
        rigidBodyState       = &m_rigidBodyState;
        engineState          = &m_engineState;
        autoboxParams        = &m_autoboxParams;

        commands             = &m_commands;
        transmissionCommands = &m_transmissionCommands;

        brakeResponseStates.setData(m_brakeResponseStates);
        throttleResponseState = &m_engineThrottleResponseState;
        steerResponseStates.setData(m_steerResponseStates);
        gearboxResponseState  = &m_gearboxState;
        clutchResponseState   = &m_clutchCommandResponseState;
        autoboxState          = &m_autoboxState;
    }

    void Vehicle4WEngineDrive::getDataForFourWheelDriveDifferentialStateComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleFourWheelDriveDifferentialParams*& differentialParams,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidbody1dStates,
        PxVehicleDifferentialState*& differentialState, 
        PxVehicleWheelConstraintGroupState*& wheelConstraintGroupState)
    {
        axleDescription             = &m_axleDesc;
        differentialParams          = &m_diffParams;
        wheelRigidbody1dStates.setData(m_wheelRB1dStates);
        differentialState           = &m_diffState;
        wheelConstraintGroupState   = &m_constraintGroupState;
    }

    void Vehicle4WEngineDrive::getDataForEngineDriveActuationStateComponent(
        const PxVehicleAxleDescription*& axleDescription, 
        const PxVehicleGearboxParams*& gearboxParams,
        PxVehicleArrayData<const PxReal>& brakeResponseStates,
        const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        const PxVehicleGearboxState*& gearboxState, 
        const PxVehicleDifferentialState*& differentialState,
        const PxVehicleClutchCommandResponseState*& clutchResponseState,
        PxVehicleArrayData<PxVehicleWheelActuationState>& actuationStates)
    {
        axleDescription         = &m_axleDesc;
        gearboxParams           = &m_gearboxParams;
        brakeResponseStates.setData(m_brakeResponseStates);
        throttleResponseState   = &m_engineThrottleResponseState;
        gearboxState            = &m_gearboxState;
        differentialState       = &m_diffState;
        clutchResponseState     = &m_clutchCommandResponseState;
        actuationStates.setData(m_wheelActuationState);
    }

    void Vehicle4WEngineDrive::getDataForPhysXRoadGeometrySceneQueryComponent(
        const PxVehicleAxleDescription*& axleDescription, 
        const PxVehiclePhysXRoadGeometryQueryParams*& roadGeomParams,
        PxVehicleArrayData<const PxReal>& steerResponseStates, 
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehiclePhysXMaterialFrictionParams>& materialFrictionParams,
        PxVehicleArrayData<PxVehicleRoadGeometryState>& roadGeometryStates,
        PxVehicleArrayData<PxVehiclePhysXRoadGeometryQueryState>& physxRoadGeometryStates)
    {
        axleDescription     = &m_axleDesc;
        roadGeomParams      = &m_roadQueryParams;
        steerResponseStates.setData(m_steerResponseStates);
        rigidBodyState      = &m_rigidBodyState;

        wheelParams.setData(m_wheelParams);
        suspensionParams.setData(m_suspParams);

        materialFrictionParams.setData(m_frictionParams);

        roadGeometryStates.setData(m_roadGeometryStates);
        physxRoadGeometryStates.setData(m_roadGeometryQueryStates);
    }

    void Vehicle4WEngineDrive::getDataForSuspensionComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyParams*& rigidBodyParams,
        const PxVehicleSuspensionStateCalculationParams*& suspensionStateCalculationParams,
        PxVehicleArrayData<const PxReal>& steerResponseStates, 
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceParams>& suspensionComplianceParams,
        PxVehicleArrayData<const PxVehicleSuspensionForceParams>& suspensionForceParams,
        PxVehicleSizedArrayData<const PxVehicleAntiRollForceParams>& antiRollForceParams,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates,
        PxVehicleArrayData<PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<PxVehicleSuspensionForce>& suspensionForces, 
        PxVehicleAntiRollTorque*& antiRollTorque)
    {
        axleDescription                     = &m_axleDesc;
        rigidBodyParams                     = &m_rigidBodyParams;
        suspensionStateCalculationParams    = m_suspCalcParams;
        steerResponseStates.setData(m_steerResponseStates);
        rigidBodyState                      = &m_rigidBodyState;

        wheelParams.setData(m_wheelParams);
        suspensionParams.setData(m_suspParams);

        suspensionComplianceParams.setData(m_suspComplianceParams);
        suspensionForceParams.setData(m_suspForceParams);

        antiRollForceParams.setDataAndCount(m_arbParams, 2);

        wheelRoadGeomStates.setData(m_roadGeometryStates);

        suspensionStates.setData(m_suspStates);
        suspensionComplianceStates.setData(m_suspComplianceStates);
        suspensionForces.setData(m_suspForces);

        antiRollTorque                      = &m_antiRollTorque;
    }

    void Vehicle4WEngineDrive::getDataForTireComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxReal>& steerResponseStates, 
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleTireForceParams>& tireForceParams,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& roadGeomStates,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1DStates,
        PxVehicleArrayData<PxVehicleTireGripState>& tireGripStates,
        PxVehicleArrayData<PxVehicleTireDirectionState>& tireDirectionStates,
        PxVehicleArrayData<PxVehicleTireSpeedState>& tireSpeedStates,
        PxVehicleArrayData<PxVehicleTireSlipState>& tireSlipStates,
        PxVehicleArrayData<PxVehicleTireCamberAngleState>& tireCamberAngleStates,
        PxVehicleArrayData<PxVehicleTireStickyState>& tireStickyStates,
        PxVehicleArrayData<PxVehicleTireForce>& tireForces)
    {
        axleDescription = &m_axleDesc;
        steerResponseStates.setData(m_steerResponseStates);
        rigidBodyState = &m_rigidBodyState;
        actuationStates.setData(m_wheelActuationState);
        wheelParams.setData(m_wheelParams);
        suspensionParams.setData(m_suspParams);
        tireForceParams.setData(m_tireForceParams);
        roadGeomStates.setData(m_roadGeometryStates);
        suspensionStates.setData(m_suspStates);
        suspensionComplianceStates.setData(m_suspComplianceStates);
        suspensionForces.setData(m_suspForces);
        wheelRigidBody1DStates.setData(m_wheelRB1dStates);
        tireGripStates.setData(m_tireGripStates);
        tireDirectionStates.setData(m_tireDirectionStates);
        tireSpeedStates.setData(m_tireSpeedStates);
        tireSlipStates.setData(m_tireSlipStates);
        tireCamberAngleStates.setData(m_tireCamberAngleStates);
        tireStickyStates.setData(m_tireStickyStates);
        tireForces.setData(m_tireForces);
    }

    void Vehicle4WEngineDrive::getDataForPhysXConstraintComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehiclePhysXSuspensionLimitConstraintParams>& suspensionLimitParams,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates,
        PxVehicleArrayData<const PxVehicleTireDirectionState>& tireDirectionStates,
        PxVehicleArrayData<const PxVehicleTireStickyState>& tireStickyStates, 
        PxVehiclePhysXConstraints*& constraints)
    {
        axleDescription = &m_axleDesc;
        rigidBodyState  = &m_rigidBodyState;
        suspensionParams.setData(m_suspParams);
        suspensionLimitParams.setData(m_suspLimitParams);
        suspensionStates.setData(m_suspStates);
        suspensionComplianceStates.setData(m_suspComplianceStates);
        wheelRoadGeomStates.setData(m_roadGeometryStates);
        tireDirectionStates.setData(m_tireDirectionStates);
        tireStickyStates.setData(m_tireStickyStates);
        constraints     = &m_constraints;
    }

    void Vehicle4WEngineDrive::getDataForEngineDrivetrainComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams, 
        const PxVehicleEngineParams*& engineParams,
        const PxVehicleClutchParams*& clutchParams, 
        const PxVehicleGearboxParams*& gearboxParams,
        PxVehicleArrayData<const PxReal>& brakeResponseStates,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleTireForce>& tireForces,
        const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        const PxVehicleClutchCommandResponseState*& clutchResponseState,
        const PxVehicleDifferentialState*& differentialState,
        const PxVehicleWheelConstraintGroupState*& constraintGroupState,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates, 
        PxVehicleEngineState*& engineState,
        PxVehicleGearboxState*& gearboxState, 
        PxVehicleClutchSlipState*& clutchState)
    {
        axleDescription         = &m_axleDesc;
        wheelParams.setData(m_wheelParams);
        engineParams            = &m_engineParams;
        clutchParams            = &m_clutchParams;
        gearboxParams           = &m_gearboxParams;
        brakeResponseStates.setData(m_brakeResponseStates);
        actuationStates.setData(m_wheelActuationState);
        tireForces.setData(m_tireForces);
        throttleResponseState   = &m_engineThrottleResponseState;
        clutchResponseState     = &m_clutchCommandResponseState;
        differentialState       = &m_diffState;
        constraintGroupState    = &m_constraintGroupState;
        wheelRigidBody1dStates.setData(m_wheelRB1dStates);
        engineState             = &m_engineState;
        gearboxState            = &m_gearboxState;
        clutchState             = &m_clutchSlipState;
    }

    void Vehicle4WEngineDrive::getDataForRigidBodyComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyParams*& rigidBodyParams,
        PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces,
        PxVehicleArrayData<const PxVehicleTireForce>& tireForces, 
        const PxVehicleAntiRollTorque*& antiRollTorque,
        PxVehicleRigidBodyState*& rigidBodyState)
    {
        axleDescription     = &m_axleDesc;
        rigidBodyParams     = &m_rigidBodyParams;
        suspensionForces.setData(m_suspForces);
        tireForces.setData(m_tireForces);
        antiRollTorque      = &m_antiRollTorque;
        rigidBodyState      = &m_rigidBodyState;
    }

    void Vehicle4WEngineDrive::getDataForWheelComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleTireSpeedState>& tireSpeedStates,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleArrayData<PxVehicleWheelLocalPose>& wheelLocalPoses)
    {
        axleDescription     = &m_axleDesc;
        steerResponseStates.setData(m_steerResponseStates);
        wheelParams.setData(m_wheelParams);
        suspensionParams.setData(m_suspParams);
        actuationStates.setData(m_wheelActuationState);
        suspensionStates.setData(m_suspStates);
        suspensionComplianceStates.setData(m_suspComplianceStates);
        tireSpeedStates.setData(m_tireSpeedStates);
        wheelRigidBody1dStates.setData(m_wheelRB1dStates);
        wheelLocalPoses.setData(m_wheelLocalPoses);
    }

    void Vehicle4WEngineDrive::getDataForPhysXActorEndComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyState*& rigidBodyState, 
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxTransform>& wheelShapeLocalPoses,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleArrayData<const PxVehicleWheelLocalPose>& wheelLocalPoses, 
        const PxVehicleGearboxState*& gearState,
        const PxReal*& throttle, 
        PxVehiclePhysXActor*& physxActor)
    {
        axleDescription     = &m_axleDesc;
        rigidBodyState      = &m_rigidBodyState;
        wheelParams.setData(m_wheelParams);
        wheelShapeLocalPoses.setData(m_wheelShapeLocalPoses);
        wheelRigidBody1dStates.setData(m_wheelRB1dStates);
        wheelLocalPoses.setData(m_wheelLocalPoses);
        gearState           = &m_gearboxState;
        throttle            = &m_commands.throttle;
        physxActor          = &m_physxActor;
    }

    void Vehicle4WEngineDrive::DebugDumpVehicleState() const
    {
        if (!m_physxActor.rigidBody) return;
        const PxVec3 v = m_physxActor.rigidBody->getLinearVelocity();
        const float speedKmh = v.magnitude() * 3.6f;
        const float rpm = (m_engineState.rotationSpeed * 60.0f / PxTwoPi);
        const PxTransform pose = m_physxActor.rigidBody->getGlobalPose();
        const PxVec3 vel = m_physxActor.rigidBody->getLinearVelocity();

        std::cout
            << "[Dbg] pose : "
            << "pos (" << pose.p.x << ", " << pose.p.y << ", " << pose.p.z << ")\n"
    		<< "[Dbg] vel : "
            << "vel (" << vel.x << ", " << vel.y << ", " << vel.z << ")\n";

        std::cout << "[Dbg] speed=" << speedKmh << " km/h"
            << " gear=" << m_gearboxState.currentGear
            << " rpm=" << rpm
            << " thr=" << m_commands.throttle
            << " brk=" << (m_commands.nbBrakes > 0 ? m_commands.brakes[0] : 0.0f)
            << " hbrk=" << (m_commands.nbBrakes > 1 ? m_commands.brakes[1] : 0.0f)
            << " steer=" << m_commands.steer
            << " clutch=" << m_transmissionCommands.clutch
            << " auto=" << (m_transmissionCommands.targetGear ==
                PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR ? "Yes" : "No")
    		<< "\n";



    //    const PxVec3 lng = m_frame.getLngAxis();

    //    for (int i = 0; i < kWheelCount; ++i)
    //    {
    //    	PxVec3 tLng = m_tireDirectionStates[i].directions[PxVehicleTireDirectionModes::eLONGITUDINAL];
    //        const float a = tLng.dot(lng);
    //        const float w = m_wheelRB1dStates[i].rotationSpeed;
    //        const float v_est = -w * m_wheelParams[i].radius;

    //        std::cout
    //            << "[Dbg] wheel rotation speed [" << i << "] =" << w << "\n"
    //            << "[Dbg] wheel rotation angle [" << i << "] =" << m_wheelRB1dStates[i].rotationAngle << "\n"
    //            << "[Dbg] tire wheelTorque [" << i << "] =" << m_tireForces[i].wheelTorque << "\n"
				//<< "[Dbg] a [" << i << "] =" << a << "\n"
    //            << "[Dbg] v_est [" << i << "] =" << v_est << "\n";
	


    //        std::cout << "[Dbg] wheel [" << i << "] " << "\n"
    //            << " driveApplied=" << m_wheelActuationState[i].isDriveApplied
    //            << ", brakeApplied=" << m_wheelActuationState[i].isBrakeApplied
    //            << "\n";

    //    //    std::cout << "[Dbg] brakeState[" << i << "]=" << m_brakeResponseState[i] << "\n";

    //    //    auto rad2deg = [](float r) {return r * 180.f / PxPi; };
    //    //    std::cout << "[Slip] wheel " << i
    //    //        << " longSlip=" << m_tireSlipStates[i].slips[PxVehicleTireDirectionModes::eLONGITUDINAL]
    //    //        << " latSlip=" << m_tireSlipStates[i].slips[PxVehicleTireDirectionModes::eLATERAL]
    //    //        << " camber(deg)=" << rad2deg(m_tireCamberAngleStates[i].camberAngle)
    //    //        << " load=" << m_tireGripStates[i].load
    //    //        << " mu=" << m_tireGripStates[i].friction
    //    //        << "\n";
    //    }


        //auto dumpWheelMap = [&]() {
        //    std::cout << "=== Wheel Map ===\n";
        //    for (PxU32 i = 0; i < m_axleDesc.nbWheels; ++i) {
        //        auto id = m_axleDesc.wheelIdsInAxleOrder[i];
        //        const auto& P = m_wheelLocalPoses[id].localPose.p;
        //        std::cout << "axleOrder[" << i << "] -> wheelId=" << id
        //            << " pos=(" << P.x << "," << P.y << "," << P.z << ")";
        //        if (id == kFL) std::cout << " [FL]";
        //        if (id == kFR) std::cout << " [FR]";
        //        if (id == kRL) std::cout << " [RL]";
        //        if (id == kRR) std::cout << " [RR]";
        //        std::cout << "\n";
        //    }

        //    std::cout << "frontWheelIds = (" << m_diffParams.frontWheelIds[0]
        //        << "," << m_diffParams.frontWheelIds[1] << ")\n";
        //    std::cout << "rearWheelIds  = (" << m_diffParams.rearWheelIds[0]
        //        << "," << m_diffParams.rearWheelIds[1] << ")\n";
        //    };
        //dumpWheelMap();
    }

    void Vehicle4WEngineDrive::DebugCheckGroundContactRaycast(PxScene* scene)
    {
        if (!scene || !m_physxActor.rigidBody) return;

        //const PxTransform chassis = m_physxActor.rigidBody->getGlobalPose();
        //const PxVec3 up(0, 1, 0), down(0, -1, 0);

        //for (PxU32 w = 0; w < kWheelCount; ++w)
        //{
        //    const PxVec3 local = m_wheelLocalPoses[w].localPose.p; // 장착점
        //    const PxVec3 world = chassis.transform(local);

        //    const float rayUp = 0.5f, rayDown = 2.5f;
        //    const PxVec3 origin = world + up * rayUp;
        //    const float  dist = rayUp + rayDown;

        //    // A) 드라이버블만
        //    {
        //        PxRaycastBuffer hitA;
        //        PxQueryFilterData qfdA;
        //        qfdA = MakeVehicleRoadQueryFilterData(true, true);
        //        PxQueryFilterCallback* cb = &gVehicleDrivablePreFilter;

        //        bool okA = scene->raycast(origin, down, dist, hitA, PxHitFlag::eDEFAULT, qfdA, cb, nullptr);
        //        if (okA && hitA.getNbAnyHits())
        //            std::cout << "[Dbg] wheel[" << w << "] A(drivable) hit dist=" << hitA.block.distance << "\n";
        //        else
        //            std::cout << "[Dbg] wheel[" << w << "] A(drivable) NO HIT\n";
        //    }
        //}

        //std::cout << "[Dbg] road gemotry states \n";
        //for (PxU32 i = 0; i < kWheelCount; ++i)
        //{
        //    std::cout << "wheel [" << i << "] : friction = " << m_roadGeomStates[i].friction << "\n";
        //}

        //std::cout << "[Dbg] road gemotry query states \n";
        //for (PxU32 i = 0; i < kWheelCount; ++i)
        //{
        //    std::cout << "wheel [" << i << "] : hit position = ("
        //        << m_roadGeometryQueryStates[i].hitPosition.x << ", "
        //        << m_roadGeometryQueryStates[i].hitPosition.y << ", "
        //        << m_roadGeometryQueryStates[i].hitPosition.z << ")\n";
        //}

    }
}
