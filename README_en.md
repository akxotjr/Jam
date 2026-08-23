# Jam

Jam is a C++ **authoritative multiplayer server framework**.

It integrates Windows IOCP-based TCP/UDP networking, a shard-based execution model, Reliable UDP, AOI replication, and client prediction/reconciliation into a unified runtime, along with Unity client integration and a shared data/code generation pipeline.

> **For design decisions, implementation details, and performance validation, see the [Technical Documents](https://akxotjr.github.io).**

## Features

* Windows IOCP-based TCP / UDP networking
* Reliable UDP

  * ACK / retransmission
  * fragmentation
  * batching
  * ordered / unordered delivery
* ownership-based shard execution
* Job / Fiber-based asynchronous execution
* cross-owner dispatch through owner mailboxes
* Session / User / World lifecycle management
* ECS-based actor state
* AOI and entity lifecycle replication
* full / delta snapshots with baseline management
* client prediction / reconciliation / replay
* PhysX-based server-side physics integration
* FlatBuffers-based RPC and schemas
* JSON-based shared game data
* C++ / C# shared data code generation
* native Unity bridge
* bot-based workloads and runtime metrics

## Core Design

| Problem                                              | Approach                             |
| ---------------------------------------------------- | ------------------------------------ |
| Concurrent access to shared mutable state            | owner-shard execution boundaries     |
| Different packet delivery requirements               | per-channel delivery policies        |
| TCP stream packet boundaries                         | receive accumulator-based framing    |
| Server authority vs. input responsiveness            | prediction / reconciliation / replay |
| AOI candidate search cost                            | spatial partitioning-based pruning   |
| Cross-owner state mutation                           | mailbox dispatch                     |
| Physics execution and world tick                     | shard-level scheduling               |
| Duplicated data definitions between server and Unity | shared schema + code generation      |

## Repository Structure

| Path                   | Description                                                                            |
| ---------------------- | -------------------------------------------------------------------------------------- |
| `JamBase/`             | Common types and foundational utilities                                                |
| `JamNet/`              | Core server runtime including networking, execution, sessions, worlds, and replication |
| `JamPx/`               | PhysX integration and physics runtime                                                  |
| `JamTools/`            | CLI for schema dump, validation, code generation, and asset processing                 |
| `JamTools.SharedData/` | Schema and code generation library used by shared-data tooling                         |
| `SharedData/`          | Game data and schemas shared between the server and client                             |
| `JamUnity/`            | Unity client integration                                                               |
| `JamUnityBridge/`      | Native bridge between the C++ runtime and Unity                                        |
| `SampleApp/`           | M1 sample server, bot, and shared code built on JamNet                                 |

### SampleApp

```text
SampleApp/
├─ M1_Server/    # Sample game server
├─ M1_Bot/       # Automated workload / load-test client
└─ M1_Shared/    # Protocol and game definitions shared by the server and bot
```

## Technical Documents

The repository contains the implementation, while the design rationale, implementation details, and validation results are documented separately.

**[→ Technical Documents](https://akxotjr.github.io)**

The documentation covers:

* Execution Model
* Networking
* Reliable UDP
* Packet Framing & Fragmentation
* Replication & Client Synchronization
* PhysX Integration & Scheduling
* Shared Game Data & Code Generation
* Unity Integration
* Performance Validation
* Engineering Decisions

## Dependencies

Install the vcpkg dependencies using the repository bootstrap script:

```powershell
.\bootstrap.ps1
```

Default triplet:

```text
x64-windows-static-md
```

## Korean

Korean README: [README.md](./README.md)
