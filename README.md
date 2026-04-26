# JAM - Game Server Framework

## Overview

JAM is an experimental C++ game server framework focused on authoritative
multiplayer server architecture for MMO-like workloads.

JamNet is the main networking and synchronization layer. It is designed around
reliable UDP transport, IOCP-based network processing, job/fiber execution,
shard-owned state updates, ECS-based world state management, physics
integration, and server-authoritative replication.

## Architecture

JamNet separates global I/O work from owner-bound state changes. Global workers
handle I/O completion, offload work, timers, and fiber resume paths. Mutable
state is routed to a shard and processed serially through jobs and mailboxes.

This model is intended to reduce shared-state contention while preserving
ordering for sessions, worlds, actors, and other owner-bound objects.

JamNet also includes a Reliable UDP transport layer. It provides reliability and ordering where needed while still allowing latency-sensitive state streams to prefer freshness over strict delivery. 

This lets replication, lifecycle events, input, and control messages use different delivery policies instead of forcing every packet through one TCP-like path.

## Dependencies

Install the vcpkg libraries with the repository bootstrap script:

```powershell
.\bootstrap.ps1
```

The script clones vcpkg into `.vcpkg` when needed, bootstraps it, and installs
the manifest dependencies from `vcpkg.json`.

By default, the script uses this triplet:

```text
x64-windows-static-md
```

If you plan to use mimalloc, install the vcpkg packages with a `static-md`
triplet. Mixing mimalloc with a different CRT/runtime triplet can cause
allocator and runtime mismatches.

## Notes

PhysX is not installed by `bootstrap.ps1` and must be prepared separately for
JamPx and TestApp.

