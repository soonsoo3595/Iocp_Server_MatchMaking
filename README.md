# IOCP Game Server

C++ / Windows IOCP(I/O Completion Port) 기반 비동기 게임 서버 엔진과, 그 위에 얹는 게임 서버 프로젝트입니다. IOCP 강의를 통해 만든 엔진(`ServerCore`)을 뼈대로, 코드를 한 줄씩 리뷰하며 구조를 소화하고 개선해나가는 과정을 거쳤고, 그 위에 리그오브레전드 스타일 매치메이킹 시스템을 직접 설계·구현하는 것을 목표로 합니다.

## 기술 스택

- **언어**: C++17
- **플랫폼**: Windows (IOCP)
- **빌드**: Visual Studio (`Server.sln`)
- **직렬화**: Protobuf (`.proto` → `protoc`로 코드 생성, 빌드 전 자동 실행)
- **DB**: ODBC + MSSQL LocalDB, 자체 ORM(XML 스키마 → 마이그레이션 쿼리 자동 생성)

## 폴더 구조

```
ServerCore/   클라이언트·서버 공통 엔진 (스레드, 메모리 풀, IOCP 네트워크, 패킷, DB/ORM)
GameServer/   게임 서버 메인 프로젝트
GameClient/   테스트/개발용 클라이언트
Common/       Protobuf(.proto), Procedure Generator(XML→SP 코드) 정의
Tools/        PacketGenerator, ProcedureGenerator (Python 코드 생성기)
Libraries/    서드파티 라이브러리
Docs/         엔진 각 모듈 학습 정리 + 매치메이킹 기획서
```

## ServerCore 엔진 하이라이트

- **IOCP 완료 포트 기반 비동기 I/O** — 소수의 워커 스레드로 다수의 소켓을 처리하는 이벤트 기반 네트워크 계층
- **커스텀 메모리 풀** — 크기 구간별 락프리(SLIST) 프리 리스트, 타입 전용 오브젝트 풀
- **직접 구현한 Reader-Writer 스핀락 + 런타임 데드락 탐지기** — 락 순서 그래프의 사이클을 실행 중에 감지
- **JobQueue 기반 액터 모델** — 락 없이 안전하게 게임 로직(Room 등)을 처리하는 스케줄링 시스템
- **Protobuf 기반 패킷 파이프라인** — `.proto` 스키마에서 직렬화 코드와 패킷 디스패치 테이블을 자동 생성

## 문서

엔진 각 모듈을 리뷰하며 정리한 학습 노트입니다.

| 문서 | 내용 |
|---|---|
| [Docs/ServerCore_Review.md](Docs/ServerCore_Review.md) | IocpCore/IocpEvent, Service, Listener, Session 전체 흐름 |
| [Docs/Buffer_Review.md](Docs/Buffer_Review.md) | RecvBuffer(선형 버퍼)와 SendBuffer(청크 풀링) |
| [Docs/SocketUtils_Review.md](Docs/SocketUtils_Review.md) | Winsock 유틸리티 계층 |
| [Docs/Lock_Review.md](Docs/Lock_Review.md) | 커스텀 Reader-Writer 스핀락, 런타임 데드락 탐지 |
| [Docs/Memory_Review.md](Docs/Memory_Review.md) | 크기별 메모리 풀, 오브젝트 풀, 할당 과정 추적 |
| [Docs/JobQueue_Review.md](Docs/JobQueue_Review.md) | Job/JobQueue/GlobalQueue/JobTimer — 락 없는 게임 로직 스케줄링 |
| [Docs/Protobuf_Review.md](Docs/Protobuf_Review.md) | proto3 문법, 코드 생성 파이프라인 |
| [Docs/PacketFlow_Review.md](Docs/PacketFlow_Review.md) | 소켓 도착 바이트가 게임 로직 실행까지 이어지는 전체 흐름 |
| [Docs/Matchmaking_Design.md](Docs/Matchmaking_Design.md) | 매치메이킹~챔피언 선택 시스템 기획서 |

## 진행 상황

- [x] `ServerCore` 엔진 리뷰 및 개선 (캡슐화, 널 포인터 방어, 순환 참조 해소, 락 점유 시간 단축 등)
- [x] 매치메이킹 시스템 기획서 작성
- [ ] 매치메이킹 시스템 구현 (진행 예정)

## 알려진 이슈 / 기술 부채

일부는 이미 개선했고, 남은 것들은 `CLAUDE.md`에서 계속 추적합니다.
- `Service::_sessions`와 `GameSessionManager::_sessions` 세션 추적 이중화
- DB 호출이 전부 동기(synchronous)라 잡 큐/워커 스레드와 분리되어 있지 않음
- 패킷 ID가 정적으로 매겨져 있어 리버스 엔지니어링에 취약함
- `Room::Enter`/`Leave`/`Broadcast`가 `public`으로 열려 있음
