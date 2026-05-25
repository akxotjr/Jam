# JamNet

JamNet is a C++ **authoritative multiplayer server framework**.

The project connects IOCP-based networking, shard-based execution, Reliable UDP transport, server-authoritative replication, and prediction/replay-based client synchronization into one runtime.

This repository focuses on problems above a simple socket wrapper:

- how network events cross into execution boundaries
- which shard owns session, user, world, and actor state
- how snapshot, lifecycle, input, and RPC traffic can avoid sharing one delivery policy
- how to preserve client responsiveness while keeping server authority
- how AOI and baseline/delta snapshots connect in replication

## Implemented Scope

- TCP/UDP network processing based on Windows IOCP
- Reliable UDP: ACK/NACK, retransmission, fragmentation, batching
- Job/Fiber-based shard execution and owner mailbox
- Server runtime model for Session/User/World
- PhysicalWorld/VirtualWorld based world assignment pipeline
- AOI, lifecycle, and full/delta snapshot replication
- prediction/reconcile/replay based client correction pipeline
- Packet buffer and hot object reuse for network hot paths
- ECS actor state and PhysX integration

## Core Design

| Problem | Choice |
| --- | --- |
| Shared-state lock contention | ownership execution boundary |
| Different delivery semantics per payload | separated channel policy |
| Server authority vs input responsiveness | prediction/reconcile/replay |
| Growing AOI cost | spatial pub/sub |
| Duplicated world enter/transfer logic | WorldAction pipeline |

## Implementation / Validation Status

| Item | Implemented | Basic Validation | Stress Validation |
| --- | --- | --- | --- |
| IOCP TCP/UDP | O | O | △ |
| RUDP retransmit | O | O | △ |
| fragmentation | O | △ | X |
| shard execution | O | O | △ |
| mailbox routing | O | O | △ |
| AOI replication | O | O | X |
| replay correction | O | △ | X |
| world transfer | △ | X | X |

```text
O : implemented and verified in normal operation
△ : partially implemented or validated in a limited scope
X : not implemented or not validated
```

## Repository Layout

| Path | Description |
| --- | --- |
| `JamNet/` | Network runtime, executor, session, world, replication |
| `JamPx/` | PhysX integration |
| `TestApp/` | Test server/client and sample content |

## Documentation

- [Portfolio](./docs/portfolio/JamNet_Portfolio.md)
- [Execution Model](./docs/architectures/JamNet_ExecutionModel.md)
- [Network Model](./docs/architectures/JamNet_NetworkModel.md)
- [Replication](./docs/architectures/JamNet_Replication.md)

## Dependencies

Install vcpkg dependencies with the repository bootstrap script.

```powershell
.\bootstrap.ps1
```

The default triplet is:

```text
x64-windows-static-md
```

PhysX is not installed by `bootstrap.ps1`. It must be prepared separately for `JamPx` and `TestApp`.

## Korean

Korean README: [README.md](./README.md)
