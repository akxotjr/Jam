# Jam

Jam은 C++ 기반의 **authoritative multiplayer server framework**입니다.

Windows IOCP 기반 TCP/UDP 네트워크 처리, shard 기반 실행 모델, Reliable UDP, AOI replication, client prediction/reconciliation을 하나의 런타임으로 구성하고, Unity client와 shared data/code generation pipeline까지 연결한 개인 프로젝트입니다.

> **설계 과정, 구현 상세 및 성능 검증은 [Technical Documents](https://akxotjr.github.io)에서 확인할 수 있습니다.**

## 구현 범위

* Windows IOCP 기반 TCP / UDP 네트워크 처리
* Reliable UDP

  * ACK / retransmission
  * fragmentation
  * batching
  * ordered / unordered delivery
* ownership 기반 shard execution
* Job / Fiber 기반 비동기 실행
* owner mailbox를 통한 cross-owner dispatch
* Session / User / World lifecycle
* ECS 기반 actor state
* AOI 및 entity lifecycle replication
* full / delta snapshot 및 baseline 관리
* client prediction / reconciliation / replay
* PhysX 기반 server-side physics integration
* FlatBuffers 기반 RPC 및 schema
* JSON 기반 shared game data
* C++ / C# shared data code generation
* Native Unity bridge
* bot 기반 workload 및 runtime metrics

## 핵심 설계

| 문제                         | 접근                                   |
| -------------------------- | ------------------------------------ |
| 공유 mutable state에 대한 동시 접근 | owner shard 기반 execution boundary    |
| 서로 다른 packet 전달 요구사항       | channel별 delivery policy 분리          |
| TCP stream packet boundary | receive accumulator 기반 framing       |
| 서버 권위와 입력 반응성              | prediction / reconciliation / replay |
| AOI 후보 탐색 비용               | spatial partitioning 기반 pruning      |
| cross-owner 상태 변경          | mailbox dispatch                     |
| physics 실행과 world tick     | shard 단위 scheduling                  |
| 서버와 Unity 간 데이터 정의 중복      | shared schema + code generation      |

## 저장소 구성

| 경로                     | 설명                                                              |
| ---------------------- | --------------------------------------------------------------- |
| `JamBase/`             | 공통 타입과 기반 유틸리티                                                  |
| `JamNet/`              | 네트워크, execution, session, world, replication 등 서버 핵심 런타임        |
| `JamPx/`               | PhysX integration 및 physics runtime                             |
| `JamTools/`            | schema dump, validation, code generation 및 asset processing CLI |
| `JamTools.SharedData/` | shared data tooling에서 사용하는 schema / codegen library             |
| `SharedData/`          | 서버와 클라이언트가 공유하는 game data 및 schema                              |
| `JamUnity/`            | Unity client integration                                        |
| `JamUnityBridge/`      | C++ runtime과 Unity를 연결하는 native bridge                          |
| `SampleApp/`           | JamNet을 사용하는 M1 sample server / bot / shared code               |

### SampleApp

```text
SampleApp/
├─ M1_Server/    # sample game server
├─ M1_Bot/       # automated workload / load-test client
└─ M1_Shared/    # server와 bot이 공유하는 protocol / game definitions
```

## Technical Documents

저장소에서는 구현 코드를 제공하고, 구현 의도와 설계 과정 및 검증 결과는 별도의 문서에서 정리합니다.

**[→ Technical Documents](https://akxotjr.github.io)**

포트폴리오에서는 다음 내용을 다룹니다.

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

## 의존성 설치

vcpkg 의존성은 저장소의 bootstrap script로 설치합니다.

```powershell
.\bootstrap.ps1
```

기본 triplet:

```text
x64-windows-static-md
```

## English

English README: [README_en.md](./README_en.md)
