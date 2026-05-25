# JamNet

JamNet은 C++ 기반의 **authoritative multiplayer server framework**입니다.

IOCP 네트워크 처리, shard 기반 실행 모델, Reliable UDP transport, 서버 authoritative replication, prediction/replay 기반 client sync를 하나의 런타임으로 연결하는 것을 목표로 만든 개인 프로젝트입니다.

이 저장소는 단순한 socket wrapper보다 한 단계 위의 문제를 다룹니다.

- 네트워크 이벤트를 어떤 실행 경계로 넘길 것인가
- session, user, world, actor 상태를 어느 shard가 소유할 것인가
- snapshot, lifecycle, input, RPC를 같은 전송 정책으로 묶지 않으려면 어떻게 나눌 것인가
- 서버 권위를 유지하면서 클라이언트 조작감을 어떻게 보존할 것인가
- AOI와 baseline/delta snapshot을 어떻게 연결할 것인가

## 구현 범위

- Windows IOCP 기반 TCP/UDP 네트워크 처리
- Reliable UDP: ACK/NACK, retransmit, fragmentation, batching
- Job/Fiber 기반 shard execution 및 owner mailbox
- Session/User/World 기반 서버 런타임 구조
- PhysicalWorld/VirtualWorld 기반 world assignment pipeline
- AOI, lifecycle, full/delta snapshot replication
- prediction/reconcile/replay 기반 client correction pipeline
- packet buffer 및 hot object 재사용 메모리 관리
- ECS actor state 및 PhysX 연동 구조

## 핵심 설계

| 문제                | 선택                           |
| ----------------- | ---------------------------- |
| 공유 상태 lock 경합     | ownership execution boundary |
| payload별 전달 의미 차이 | channel policy 분리            |
| 서버 권위와 입력 반응성 충돌  | prediction/reconcile/replay  |
| AOI 비용 증가         | spatial pub/sub              |
| world 입장/이동 로직 중복 | WorldAction pipeline         |

## 구현/검증 상태 표

| 항목                | 구현  | 기본 검증 | Stress 검증 |
| ----------------- | --- | ----- | --------- |
| IOCP TCP/UDP      | O   | O     | △         |
| RUDP retransmit   | O   | O     | △         |
| fragmentation     | O   | △     | X         |
| shard execution   | O   | O     | △         |
| mailbox routing   | O   | O     | △         |
| AOI replication   | O   | O     | X         |
| replay correction | O   | △     | X         |
| world transfer    | △   | X     | X         |
```
O : 구현 및 정상 동작 확인  
△ : 제한적 검증 또는 일부 구현  
X : 미구현 또는 미검증
```


## 저장소 구성

| 경로 | 설명 |
| --- | --- |
| `JamNet/` | 네트워크 런타임, executor, session, world, replication |
| `JamPx/` | PhysX 기반 physics integration |
| `TestApp/` | 테스트 서버/클라이언트와 샘플 컨텐츠 |


## 문서

- [Portfolio](./docs/portfolio/JamNet_Portfolio.md)
- [Execution Model](./docs/architecture/JamNet_ExecutionModel.md)
- [Network Model](./docs/architecture/JamNet_NetworkModel.md)
- [Replication](./docs/architecture/JamNet_Replication.md)

## 의존성 설치

vcpkg 의존성은 저장소 bootstrap script로 설치합니다.

```powershell
.\bootstrap.ps1
```

기본 triplet은 다음과 같습니다.

```text
x64-windows-static-md
```

PhysX는 `bootstrap.ps1`로 설치되지 않습니다. `JamPx`와 `TestApp`을 사용하려면 별도로 준비해야 합니다.

## English

English README: [README_en.md](./README_en.md)
