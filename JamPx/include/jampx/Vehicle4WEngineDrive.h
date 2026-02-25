#pragma once

#include <physx/vehicle2/PxVehicleAPI.h>

namespace jam::px
{
	using namespace physx::vehicle2;

    inline constexpr int32      kWheelCount  = 4;  // FL, FR, RL, RR
    inline constexpr PxU32      kFL          = 0;
    inline constexpr PxU32      kFR          = 1;
    inline constexpr PxU32      kRL          = 2;
    inline constexpr PxU32      kRR          = 3;
                                
    inline constexpr PxU32      kFootBrake   = 0;
    inline constexpr PxU32      kHandBrake   = 1;




    struct Vehicle4WEngineDriveDesc
    {
        PxScene*                scene           = nullptr;
        PxTransform             pose            = PxTransform(PxIdentity);

        // Chassis geometry
        float                   chassisMass     = 1400.0f;                                // 1400 kg
        PxVec3                  chassisSize     = PxVec3(2.0f, 1.2f, 4.4f);     
        float                   comLowering     = 0.3f;  
        PxVec3                  comOffset       = PxVec3(0.0f, -0.3f, 0.0f);

        // Wheel geometry
        float                   wheelRadius     = 0.34f;    // 34cm 
        float                   wheelWidth      = 0.26f;    // 265mm tire     
        float                   wheelMass       = 18.0f;    // (kg) per-wheel (tire + rim) 

        // Layout (Y-up, Z-forward, X-right)
        float                   frontTrackWidth = 1.6f;
        float                   rearTrackWidth  = 1.6f;
        float                   frontWheelBase  = 1.3f;
        float                   rearWheelBase   = 1.3f;

        // Suspension travel
        float                   compressionMax  = 0.15f;    // compression limit
        float                   droopMax        = 0.15f;    // droop limit

        bool                    isRwd           = false;     // AWD by default (set true for RWD)
    };

    struct Vehicle4WEngineDriveControl
    {
        float                   throttle    = 0;
        float                   brake       = 0;
        float                   handbrake   = 0;
    	float                   steer       = 0;
        bool                    autoMode    = true;

        bool                    shiftUp     = false;    // if auto mode is true
    	bool                    shiftDown   = false; 
    };

    class Vehicle4WEngineDrive :
        public PxVehiclePhysXActorBeginComponent,
        public PxVehicleEngineDriveCommandResponseComponent,
        public PxVehicleFourWheelDriveDifferentialStateComponent,
        public PxVehicleEngineDriveActuationStateComponent,
        public PxVehiclePhysXRoadGeometrySceneQueryComponent,
        public PxVehicleSuspensionComponent,
        public PxVehicleTireComponent,
        public PxVehiclePhysXConstraintComponent,
        public PxVehicleEngineDrivetrainComponent,
        public PxVehicleRigidBodyComponent,
        public PxVehicleWheelComponent,
        public PxVehiclePhysXActorEndComponent
    {
    public:
        void                            Init(const Vehicle4WEngineDriveDesc& desc);
        void                            UpdateInput(const Vehicle4WEngineDriveControl& input);
        void                            UpdateSequence();

        PxRigidActor*                   Actor() const { return m_physxActor.rigidBody; }
        const PxVehicleRigidBodyState&  RigidBodyState() const { return m_rigidBodyState; }

    private:
        void                            BuildRigidBody(const Vehicle4WEngineDriveDesc& desc);
        void                            BuildWheelsSuspension(const Vehicle4WEngineDriveDesc& desc);
        void                            BuildTiresAndFricition(const Vehicle4WEngineDriveDesc& desc);
        void                            BuildDrivetrain(const Vehicle4WEngineDriveDesc& desc);
        void                            BuildSteerAndAckermann(const Vehicle4WEngineDriveDesc& desc);
        void                            BuildBrakes(const Vehicle4WEngineDriveDesc& desc);
        void                            BuildAntiRollBars(const Vehicle4WEngineDriveDesc& desc);
        void                            BuildRoadAndFriction(const Vehicle4WEngineDriveDesc& desc);


        void                            ConfigureDifferentialAndDriveWheels(bool isRwd);

        void                            CreateSequence();


        float                           GetSpeedKmh() const;
        float                           ShapeSteer(float raw, float speedKmh, float dt) const;
        float                           ShapeThrottle(float raw, float steerShaped, float speedKmh, float dt) const;
        float                           ShapeBrake(float raw, float dt) const;




        // Component interface implementations

        virtual void                    getDataForPhysXActorBeginComponent(const PxVehicleAxleDescription*& axleDescription, const PxVehicleCommandState*& commands, const PxVehicleEngineDriveTransmissionCommandState*& transmissionCommands, const PxVehicleGearboxParams*& gearParams, const PxVehicleGearboxState*& gearState, const PxVehicleEngineParams*& engineParams, PxVehiclePhysXActor*& physxActor, PxVehiclePhysXSteerState*& physxSteerState, PxVehiclePhysXConstraints*& physxConstraints, PxVehicleRigidBodyState*& rigidBodyState, PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates, PxVehicleEngineState*& engineState) override;
        virtual void                    getDataForEngineDriveCommandResponseComponent(const PxVehicleAxleDescription*& axleDescription, PxVehicleSizedArrayData<const PxVehicleBrakeCommandResponseParams>& brakeResponseParams, const PxVehicleSteerCommandResponseParams*& steerResponseParams, PxVehicleSizedArrayData<const PxVehicleAckermannParams>& ackermannParams, const PxVehicleGearboxParams*& gearboxParams, const PxVehicleClutchCommandResponseParams*& clutchResponseParams, const PxVehicleEngineParams*& engineParams, const PxVehicleRigidBodyState*& rigidBodyState, const PxVehicleEngineState*& engineState, const PxVehicleAutoboxParams*& autoboxParams, const PxVehicleCommandState*& commands, const PxVehicleEngineDriveTransmissionCommandState*& transmissionCommands, PxVehicleArrayData<PxReal>& brakeResponseStates, PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState, PxVehicleArrayData<PxReal>& steerResponseStates, PxVehicleGearboxState*& gearboxResponseState, PxVehicleClutchCommandResponseState*& clutchResponseState, PxVehicleAutoboxState*& autoboxState) override;
        virtual void                    getDataForFourWheelDriveDifferentialStateComponent(const PxVehicleAxleDescription*& axleDescription, const PxVehicleFourWheelDriveDifferentialParams*& differentialParams, PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidbody1dStates, PxVehicleDifferentialState*& differentialState, PxVehicleWheelConstraintGroupState*& wheelConstraintGroupState) override;
        virtual void                    getDataForEngineDriveActuationStateComponent(const PxVehicleAxleDescription*& axleDescription, const PxVehicleGearboxParams*& gearboxParams, PxVehicleArrayData<const PxReal>& brakeResponseStates, const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState, const PxVehicleGearboxState*& gearboxState, const PxVehicleDifferentialState*& differentialState, const PxVehicleClutchCommandResponseState*& clutchResponseState, PxVehicleArrayData<PxVehicleWheelActuationState>& actuationStates) override;
        virtual void                    getDataForPhysXRoadGeometrySceneQueryComponent(const PxVehicleAxleDescription*& axleDescription, const PxVehiclePhysXRoadGeometryQueryParams*& roadGeomParams, PxVehicleArrayData<const PxReal>& steerResponseStates, const PxVehicleRigidBodyState*& rigidBodyState, PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams, PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams, PxVehicleArrayData<const PxVehiclePhysXMaterialFrictionParams>& materialFrictionParams, PxVehicleArrayData<PxVehicleRoadGeometryState>& roadGeometryStates, PxVehicleArrayData<PxVehiclePhysXRoadGeometryQueryState>& physxRoadGeometryStates) override;
        virtual void                    getDataForSuspensionComponent(const PxVehicleAxleDescription*& axleDescription, const PxVehicleRigidBodyParams*& rigidBodyParams, const PxVehicleSuspensionStateCalculationParams*& suspensionStateCalculationParams, PxVehicleArrayData<const PxReal>& steerResponseStates, const PxVehicleRigidBodyState*& rigidBodyState, PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams, PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams, PxVehicleArrayData<const PxVehicleSuspensionComplianceParams>& suspensionComplianceParams, PxVehicleArrayData<const PxVehicleSuspensionForceParams>& suspensionForceParams, PxVehicleSizedArrayData<const PxVehicleAntiRollForceParams>& antiRollForceParams, PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates, PxVehicleArrayData<PxVehicleSuspensionState>& suspensionStates, PxVehicleArrayData<PxVehicleSuspensionComplianceState>& suspensionComplianceStates, PxVehicleArrayData<PxVehicleSuspensionForce>& suspensionForces, PxVehicleAntiRollTorque*& antiRollTorque) override;
        virtual void                    getDataForTireComponent(const PxVehicleAxleDescription*& axleDescription, PxVehicleArrayData<const PxReal>& steerResponseStates, const PxVehicleRigidBodyState*& rigidBodyState, PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates, PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams, PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams, PxVehicleArrayData<const PxVehicleTireForceParams>& tireForceParams, PxVehicleArrayData<const PxVehicleRoadGeometryState>& roadGeomStates, PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates, PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates, PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces, PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1DStates, PxVehicleArrayData<PxVehicleTireGripState>& tireGripStates, PxVehicleArrayData<PxVehicleTireDirectionState>& tireDirectionStates, PxVehicleArrayData<PxVehicleTireSpeedState>& tireSpeedStates, PxVehicleArrayData<PxVehicleTireSlipState>& tireSlipStates, PxVehicleArrayData<PxVehicleTireCamberAngleState>& tireCamberAngleStates, PxVehicleArrayData<PxVehicleTireStickyState>& tireStickyStates, PxVehicleArrayData<PxVehicleTireForce>& tireForces) override;
        virtual void                    getDataForPhysXConstraintComponent(const PxVehicleAxleDescription*& axleDescription, const PxVehicleRigidBodyState*& rigidBodyState, PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams, PxVehicleArrayData<const PxVehiclePhysXSuspensionLimitConstraintParams>& suspensionLimitParams, PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates, PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates, PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates, PxVehicleArrayData<const PxVehicleTireDirectionState>& tireDirectionStates, PxVehicleArrayData<const PxVehicleTireStickyState>& tireStickyStates, PxVehiclePhysXConstraints*& constraints) override;
        virtual void                    getDataForEngineDrivetrainComponent(const PxVehicleAxleDescription*& axleDescription, PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams, const PxVehicleEngineParams*& engineParams, const PxVehicleClutchParams*& clutchParams, const PxVehicleGearboxParams*& gearboxParams, PxVehicleArrayData<const PxReal>& brakeResponseStates, PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates, PxVehicleArrayData<const PxVehicleTireForce>& tireForces, const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState, const PxVehicleClutchCommandResponseState*& clutchResponseState, const PxVehicleDifferentialState*& differentialState, const PxVehicleWheelConstraintGroupState*& constraintGroupState, PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates, PxVehicleEngineState*& engineState, PxVehicleGearboxState*& gearboxState, PxVehicleClutchSlipState*& clutchState) override;
        virtual void                    getDataForRigidBodyComponent(const PxVehicleAxleDescription*& axleDescription, const PxVehicleRigidBodyParams*& rigidBodyParams, PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces, PxVehicleArrayData<const PxVehicleTireForce>& tireForces, const PxVehicleAntiRollTorque*& antiRollTorque, PxVehicleRigidBodyState*& rigidBodyState) override;
        virtual void                    getDataForWheelComponent(const PxVehicleAxleDescription*& axleDescription, PxVehicleArrayData<const PxReal>& steerResponseStates, PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams, PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams, PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates, PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates, PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates, PxVehicleArrayData<const PxVehicleTireSpeedState>& tireSpeedStates, PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates, PxVehicleArrayData<PxVehicleWheelLocalPose>& wheelLocalPoses) override;
        virtual void                    getDataForPhysXActorEndComponent(const PxVehicleAxleDescription*& axleDescription, const PxVehicleRigidBodyState*& rigidBodyState, PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams, PxVehicleArrayData<const PxTransform>& wheelShapeLocalPoses, PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates, PxVehicleArrayData<const PxVehicleWheelLocalPose>& wheelLocalPoses, const PxVehicleGearboxState*& gearState, const PxReal*& throttle, PxVehiclePhysXActor*& physxActor) override;


        void                            DebugDumpVehicleState() const;
        void                            DebugCheckGroundContactRaycast(PxScene* scene);

    private:
        Vehicle4WEngineDriveDesc                                                m_desc{};
        Vehicle4WEngineDriveControl                                             m_control{};


        PxVehicleComponentSequence                                              m_sequence{};


        // PhysX Actor

        PxVehiclePhysXActor                                                     m_physxActor{};

        PxVehicleRigidBodyParams                                                m_rigidBodyParams{};
        PxVehicleRigidBodyState                                                 m_rigidBodyState{};

        // Command
        PxVehicleCommandState                                                   m_commands{};
        PxVehicleEngineDriveTransmissionCommandState                            m_transmissionCommands{};

        // Frame
        PxVehicleFrame                                                          m_frame{};  // Y-up, Z-forward, X-right

        // Simulation
        PxVehiclePhysXSimulationContext                                         m_simCtx{}; // Vehicle sim context(좌표축/스케일/씬/중력/명령 등)

        // Axle
        PxVehicleAxleDescription                                                m_axleDesc{};

        // Wheel
        PxVehicleWheelParams                                                    m_wheelParams[kWheelCount]{};
        PxVehicleWheelLocalPose                                                 m_wheelLocalPoses[kWheelCount]{};
        PxVec3                                                                  m_wheelLocalPositions[kWheelCount]{};
        PxTransform                                                             m_wheelShapeLocalPoses[kWheelCount]{};

        PxVehicleWheelRigidBody1dState                                          m_wheelRB1dStates[kWheelCount]{};     // 각속도 등
        PxVehicleWheelActuationState                                            m_wheelActuationState[kWheelCount]{}; // 드라이브/브레이크 적용 여부

        // Suspension
        PxVehicleSuspensionForce                                                m_suspForces[kWheelCount]{};
        PxVehicleSuspensionParams                                               m_suspParams[kWheelCount]{};
        PxVehicleSuspensionComplianceParams                                     m_suspComplianceParams[kWheelCount]{};
        PxVehicleSuspensionStateCalculationParams                               m_suspCalcParams[kWheelCount]{};
        PxVehicleSuspensionForceParams                                          m_suspForceParams[kWheelCount]{};
        PxVehiclePhysXSuspensionLimitConstraintParams                           m_suspLimitParams[kWheelCount]{};

        PxVehicleSuspensionState                                                m_suspStates[kWheelCount]{};
        PxVehicleSuspensionComplianceState                                      m_suspComplianceStates[kWheelCount]{};

        // Tire
        PxVehicleTireForceParams                                                m_tireForceParams[kWheelCount]{};

        PxVehicleTireForce                                                      m_tireForces[kWheelCount]{};
        PxVehicleTireDirectionState                                             m_tireDirectionStates[kWheelCount]{};
        PxVehicleTireSpeedState                                                 m_tireSpeedStates[kWheelCount]{};
        PxVehicleTireSlipState                                                  m_tireSlipStates[kWheelCount]{};
        PxVehicleTireStickyState                                                m_tireStickyStates[kWheelCount]{};
        PxVehicleTireGripState                                                  m_tireGripStates[kWheelCount]{};
        PxVehicleTireCamberAngleState                                           m_tireCamberAngleStates[kWheelCount]{};

        // Road geometry query 
        PxVehiclePhysXRoadGeometryQueryParams                                   m_roadQueryParams{ PxQueryFilterData() };
        PxVehicleRoadGeometryState                                              m_roadGeometryStates[kWheelCount]{};
        PxVehiclePhysXRoadGeometryQueryState                                    m_roadGeometryQueryStates[kWheelCount]{};

        PxVehiclePhysXMaterialFrictionParams                                    m_frictionParams[kWheelCount]{};
        std::vector<PxVehiclePhysXMaterialFriction>                             m_materialFrictions{};

        // Constraints
        PxVehiclePhysXConstraints                                               m_constraints{};
        PxVehicleWheelConstraintGroupState                                      m_constraintGroupState{};

        // Steer
        PxVehicleSteerCommandResponseParams                                     m_steerResponseParams{};
        PxVehiclePhysXSteerState                                                m_steerState{};
        PxReal                                                                  m_steerResponseStates[kWheelCount]{};

        // Ackermann
        PxVehicleAckermannParams                                                m_ackermannParams{};

        // Brakes
        PxVehicleBrakeCommandResponseParams                                     m_brakeCommandResponseParams[2]{}; // [0] brake, [1] handbrake
        PxReal                                                                  m_brakeResponseStates[kWheelCount]{};

        // Engine
        PxVehicleEngineParams                                                   m_engineParams{};

        PxVehicleEngineState                                                    m_engineState{};
        PxVehicleEngineDriveThrottleCommandResponseState                        m_engineThrottleResponseState{};

        // Clutch
        PxVehicleClutchParams                                                   m_clutchParams{};
        PxVehicleClutchCommandResponseParams                                    m_clutchCommandResponseParams{};

        PxVehicleClutchCommandResponseState                                     m_clutchCommandResponseState{};
        PxVehicleClutchSlipState                                                m_clutchSlipState{};

        // Gearbox
        PxVehicleGearboxParams                                                  m_gearboxParams{};
        PxVehicleGearboxState                                                   m_gearboxState{};

        // Autobox
        PxVehicleAutoboxParams                                                  m_autoboxParams{};
        PxVehicleAutoboxState                                                   m_autoboxState{};

        // Differential
        PxVehicleFourWheelDriveDifferentialParams                               m_diffParams{};
        PxVehicleDifferentialState                                              m_diffState{};

		// Anti-roll bars
        PxVehicleAntiRollTorque                                                 m_antiRollTorque{};
        PxVehicleAntiRollForceParams                                            m_arbParams[2]{};



        // Input shaping state
        struct FilteredInputs
        {
            float steer     = 0.0f; // [-1, 1]
            float throttle  = 0.0f; // [0, 1]
            float brake     = 0.0f; // [0, 1]
            float handbrake = 0.0f; // [0, 1]
        };

        FilteredInputs                                                          m_filteredInput{};
    };

}
