#pragma once


namespace jam::px
{

	// ---- scalar ----

	using PxU8 						= physx::PxU8;
	using PxU16						= physx::PxU16;
	using PxU32						= physx::PxU32;
	using PxU64						= physx::PxU64;
	using PxReal					= physx::PxReal;
	using PxF32						= physx::PxF32;
	using PxF64						= physx::PxF64;

	// ---- math ----

	using PxVec2 					= physx::PxVec2;
	using PxVec3 					= physx::PxVec3;
	using PxVec4					= physx::PxVec4;
	using PxQuat 					= physx::PxQuat;
	using PxTransform 				= physx::PxTransform;
	using PxExtendedVec3 			= physx::PxExtendedVec3;
	using PxMat33					= physx::PxMat33;
	using PxMat44					= physx::PxMat44;


	// ---- foundation / core ----

	using PxFoundation				= physx::PxFoundation;
	using PxPhysics 				= physx::PxPhysics;
	using PxScene 					= physx::PxScene;
	using PxSceneDesc 				= physx::PxSceneDesc;
	using PxSceneFlags				= physx::PxSceneFlags;
	using PxSceneFlag				= physx::PxSceneFlag;
	using PxTolerancesScale			= physx::PxTolerancesScale;

	// ---- material / shape ----

	using PxMaterial 				= physx::PxMaterial;
	using PxShape 					= physx::PxShape;
	using PxShapeFlags 				= physx::PxShapeFlags;
	using PxShapeFlag 				= physx::PxShapeFlag;


	// ---- actors ----

	using PxBase 					= physx::PxBase;
	using PxActor 					= physx::PxActor;
	using PxRigidActor 				= physx::PxRigidActor;
	using PxRigidStatic 			= physx::PxRigidStatic;
	using PxRigidBody 				= physx::PxRigidBody;
	using PxRigidDynamic 			= physx::PxRigidDynamic;

	using PxActorFlags 				= physx::PxActorFlags;
	using PxActorFlag 				= physx::PxActorFlag;
	using PxRigidBodyFlags 			= physx::PxRigidBodyFlags;
	using PxRigidBodyFlag 			= physx::PxRigidBodyFlag;


	// ---- geometry / mesh ----

	using PxTriangleMesh 			= physx::PxTriangleMesh;
	using PxTriangleMeshDesc		= physx::PxTriangleMeshDesc;
	using PxConvexMesh 				= physx::PxConvexMesh;
	using PxConvexMeshDesc			= physx::PxConvexMeshDesc;
	using PxConvexFlags				= physx::PxConvexFlags;
	using PxConvexFlag				= physx::PxConvexFlag;

	using PxGeometry 				= physx::PxGeometry;
	using PxBoxGeometry 			= physx::PxBoxGeometry;
	using PxSphereGeometry 			= physx::PxSphereGeometry;
	using PxCapsuleGeometry			= physx::PxCapsuleGeometry;
	using PxPlaneGeometry 			= physx::PxPlaneGeometry;
	using PxTriangleMeshGeometry 	= physx::PxTriangleMeshGeometry;
	using PxConvexMeshGeometry 		= physx::PxConvexMeshGeometry;


	// ---- query / filter ----

	using PxFilterData 				= physx::PxFilterData;
	using PxFilterFlags 			= physx::PxFilterFlags;
	using PxFilterFlag 				= physx::PxFilterFlag;
	using PxFilterObjectAttributes	= physx::PxFilterObjectAttributes;

	using PxQueryFilterData			= physx::PxQueryFilterData;
	using PxQueryFlags 				= physx::PxQueryFlags;
	using PxQueryFlag 				= physx::PxQueryFlag;
	using PxQueryHit 				= physx::PxQueryHit;
	using PxQueryHitType 			= physx::PxQueryHitType;

	using PxPairFlags 				= physx::PxPairFlags;
	using PxPairFlag 				= physx::PxPairFlag;

	using PxHitFlags				= physx::PxHitFlags;
	using PxHitFlag					= physx::PxHitFlag;

	using PxRaycastHit 				= physx::PxRaycastHit;
	using PxRaycastBuffer 			= physx::PxRaycastBuffer;
	using PxSweepHit 				= physx::PxSweepHit;
	using PxSweepBuffer 			= physx::PxSweepBuffer;
	using PxOverlapHit				= physx::PxOverlapHit;
	using PxOverlapBuffer			= physx::PxOverlapBuffer;

	using PxSimulationEventCallback		= physx::PxSimulationEventCallback;
	using PxContactPairHeader			= physx::PxContactPairHeader;
	using PxContactPair					= physx::PxContactPair;
	using PxContactPairPoint			= physx::PxContactPairPoint;
	using PxContactPairFlags			= physx::PxContactPairFlags;
	using PxContactPairFlag				= physx::PxContactPairFlag;
	using PxTriggerPair					= physx::PxTriggerPair;
	using PxTriggerPairFlags			= physx::PxTriggerPairFlags;
	using PxTriggerPairFlag				= physx::PxTriggerPairFlag;
	
	using PxSimulationFilterCallback	= physx::PxSimulationFilterCallback;
	
	using PxQueryFilterCallback			= physx::PxQueryFilterCallback;


	// ---- cct ----

	using PxControllerManager			= physx::PxControllerManager;
	using PxController 					= physx::PxController;
	using PxCapsuleController 			= physx::PxCapsuleController;
	using PxCapsuleControllerDesc 		= physx::PxCapsuleControllerDesc;

	using PxObstacle					= physx::PxObstacle;

	using PxUserControllerHitReport		= physx::PxUserControllerHitReport;
	using PxControllerShapeHit			= physx::PxControllerShapeHit;
	using PxControllersHit				= physx::PxControllersHit;
	using PxControllerObstacleHit		= physx::PxControllerObstacleHit;

	using PxControllerBehaviorCallback	= physx::PxControllerBehaviorCallback;
	using PxControllerBehaviorFlags		= physx::PxControllerBehaviorFlags;
	using PxControllerBehaviorFlag		= physx::PxControllerBehaviorFlag;

	using PxControllerFilterCallback	= physx::PxControllerFilterCallback;



	// ---- task / dispatcher ----

	using PxCpuDispatcher 			= physx::PxCpuDispatcher;

	using PxTaskManager				= physx::PxTaskManager;
	using PxBaseTask 				= physx::PxBaseTask;
	using PxTask 					= physx::PxTask;
	using PxLightCpuTask 			= physx::PxLightCpuTask;


	// ---- cooking ----

	using PxCookingParams				= physx::PxCookingParams;
	using PxConvexMeshCookingType		= physx::PxConvexMeshCookingType;
	using PxConvexMeshCookingResult		= physx::PxConvexMeshCookingResult;
	using PxTriangleMeshCookingResult	= physx::PxTriangleMeshCookingResult;

}
