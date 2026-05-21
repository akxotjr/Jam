#pragma once


#include <jambase/JamMacro.h>
#include <jambase/JamTypes.h>
#include <jambase/Logger.h>
#include <jambase/JamAssert.h>


// -- Core --

// memory
#include "jamnet/core/memory/DefaultAllocator.h"
#include "jamnet/core/memory/ObjectPool.h"

// time
#include "jamnet/core/utils/TimeUnits.h"
#include "jamnet/core/utils/Clock.h"

// executor
#include "jamnet/core/executor/LockQueue.h"
#include "jamnet/core/executor/LockDeque.h"
#include "jamnet/core/executor/Job.h"
#include "jamnet/core/executor/Mailbox.h"
#include "jamnet/core/executor/ShardExecutor.h"
#include "jamnet/core/executor/ShardRoutingPolicy.h"
#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/executor/MainExecutor.h"
#include "jamnet/core/executor/GlobalEventBus.h"

// network
#include "jamnet/core/net/Buffer.h"
#include "jamnet/core/net/Service.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/core/net/TcpSession.h"
#include "jamnet/core/net/UdpSession.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/core/net/RPCAPI.h"
#include "jamnet/core/net/NetRuntime.h"


// -- Sync --

// WorldBase
#include "jamnet/sync/networld/ClientPhysicalWorld.h"
#include "jamnet/runtime/world/WorldBase.h"
#include "jamnet/sync/networld/ServerPhysicalWorld.h"

// bridge
#include "jamnet/sync/physics/ShardJobBridge.h"

// replication
#include "jamnet/sync/replication/ClientInputSystem.h"
#include "jamnet/sync/replication/ClientPhysicsSystem.h"
#include "jamnet/sync/replication/ClientReplaySystem.h"
#include "jamnet/sync/replication/ClientReplicationSystem.h"
#include "jamnet/sync/replication/ClientSamplingSystem.h"
#include "jamnet/sync/replication/CorrectionReplayRunner.h"
#include "jamnet/sync/replication/IReplayRunner.h"
#include "jamnet/sync/replication/NetActorComponents.h"
#include "jamnet/sync/replication/WorldContext.h"
#include "jamnet/runtime/AppRuntimeEvents.h"
#include "jamnet/sync/replication/ReplicationTypes.h"
#include "jamnet/sync/replication/ReplicationCodec.h"
#include "jamnet/sync/replication/ServerInputSystem.h"
#include "jamnet/sync/replication/ServerPhysicsSystem.h"
#include "jamnet/sync/replication/ServerAoiSystem.h"
#include "jamnet/sync/replication/ServerReplicationSystem.h"

// transport
#include "jamnet/sync/transport/CustomPacketHelper.h"
#include "jamnet/sync/transport/ITransportEndpoint.h"


// -- Runtime --

#include "jamnet/runtime/ClientSession.h"
#include "jamnet/runtime/ClientNetworkManager.h"
#include "jamnet/runtime/ClientRuntime.h"

#include "jamnet/runtime/ServerSession.h"
#include "jamnet/runtime/ServerNetworkManager.h"

#include "jamnet/runtime/world/DefaultWorldAssignmentPolicy.h"
#include "jamnet/runtime/world/ClientWorldActionSystem.h"
#include "jamnet/runtime/world/ServerWorldActionSystem.h"
#include "jamnet/runtime/world/IWorldAssignmentPolicy.h"
#include "jamnet/runtime/world/IWorldActionSystem.h"
#include "jamnet/runtime/world/WorldActionTypes.h"
#include "jamnet/runtime/world/WorldDirectory.h"
