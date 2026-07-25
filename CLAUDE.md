# IOCP 게임 서버 프로젝트

## 프로젝트 개요
C++ IOCP 강의를 통해 만든 서버 엔진(`ServerCore`)을 기반으로, 간단한 게임을 직접 만들어보며 프로젝트 전체 구조를 소화하기 위한 프로젝트.
강의 진행용 레포(`CPP_Server\Server`)에서 재사용 가능한 코드만 가져와 새로 시작했으며, 강의용 데모 코드(DB Gold 예제, 채팅 브로드캐스트 데모 등)는 정리했다.

## 폴더 구조
- `ServerCore/` — 클라이언트·서버 공통 엔진 모듈 (스레드, 메모리, 네트워크(IOCP), 패킷, DB/ORM 등)
- `GameServer/` — 게임 서버 메인 프로젝트 (`Room`/`Player`/`GameSession`/`ClientPacketHandler` 골격 포함)
- `GameClient/` — 테스트/개발용 클라이언트 (구 DummyClient)
- `Common/` — Protobuf(`.proto`), Procedure Generator(XML→SP 코드) 관련 스크립트·정의
- `Tools/` — `PacketGenerator`, `ProcedureGenerator` (Python + Jinja2 코드 생성기)
- `Libraries/` — 서드파티 라이브러리 (include/lib)

## 기술 스택
- 언어: C++17
- 플랫폼: Windows (IOCP)
- 빌드: Visual Studio (`Server.sln`, vcxproj)
- 직렬화: Protobuf (`.proto` → `protoc`로 `Protocol.pb.h/.cc` 생성, 빌드 전 이벤트에서 자동 실행)
- 패킷 핸들러: `PacketGenerator`(Python+Jinja2)가 `.proto`를 읽어 `ClientPacketHandler`/`ServerPacketHandler` 자동 생성
- DB: ODBC + MSSQL LocalDB (`DBConnection`/`DBConnectionPool`), 연결 문자열의 DB명은 `IocpGameDb` (강의 레포의 `ServerDb`와 분리)
- 자체 ORM: `GameDB.xml`에 원하는 스키마/프로시저를 선언하면 `DBSynchronizer`가 실제 DB(`sys.columns`/`sys.tables`/`sys.indexes`/`sys.procedures`)와 비교해서 마이그레이션 쿼리를 생성/실행. `ProcedureGenerator`가 `<Procedure>`를 읽어 `SP::XXX`(`DBBind` 파생) 클래스를 자동 생성
- 로깅: `ServerCore/Logger.h/.cpp`의 `GLogger` 전역(구 `ConsoleLog` 대체). 콘솔/파일 선택, 스레드 세이프(TLS 버퍼), 로그 레벨(`Verbose`~`Fatal`, `Verbose`/`Log`는 Release 빌드에서 컴파일 자체가 빠짐), 타임스탬프·호출 함수명(`__FUNCTION__`) 자동 기록. `GameServer`/`GameClient`는 별개 프로세스라 각자 `main()`에서 `GLogger->Init()` 호출 필요. 에러 경로뿐 아니라 IOCP 등록/접속/세션 증감 같은 핵심 로직에도 로그를 계속 추가해나가는 중

## 시작 상태
`GameDB.xml`은 빈 스키마(`<GameDB></GameDB>`)로 시작한다. 테이블/프로시저를 추가하려면 XML에 `<Table>`/`<Procedure>`를 선언하고 빌드하면 `DBSynchronizer`와 `ProcedureGenerator`가 자동으로 DB 스키마 동기화 + `SP::` 코드 생성을 처리한다.

`GameServer.cpp`의 `main()`은 DB 연결 + 스키마 동기화 + `ClientPacketHandler::Init()` + IOCP 서비스 부트스트랩만 남긴 최소 골격이다. `Room`/`Player`/`GameSession`/`ClientPacketHandler`(로그인/입장/채팅 핸들러)는 강의 골격이 참고용으로 남아있으니, 실제 게임 로직으로 교체/확장하면 된다.

## 알려진 이슈 / 기술 부채 (강의 레포에서 이관, 여전히 유효)
- `ServerService`↔`Listener` 순환 참조 위험 미해소
- `Service::_sessions`와 `GameSessionManager::_sessions` 세션 추적이 이중화되어 있음
- `GameSessionManager::Broadcast`가 대규모 동접 시 크래시할 수 있음 — 잡 큐가 `GameSessionManager`까지는 적용되지 않음
- `Player`↔`GameSession` 참조 순환: 구조적으로 `weak_ptr`로 끊어내지 않고 `OnDisconnected`에서 수동 정리하는 임시 처방만 되어 있음
- DB 호출이 전부 동기(synchronous)라 잡 큐/워커 스레드와 분리되어 있지 않음
- `WCHAR[N]` 배열이 `DBBind::BindParam`에서 문자열이 아닌 이진 오버로드로 라우팅될 수 있음(미확인)
- `FileUtils::ReadFile`이 텍스트 모드로 열려 CRLF 파일에서 버퍼 크기가 어긋날 수 있음
- `XmlNode::GetStringValue()`에 `first_node()` null 체크 없음
- `DBSynchronizer`의 메타 쿼리 클래스(`GetDBTables` 등)가 아직 수동 작성 상태
- `.proto` 필드명이 카멜케이스라 접근자 이름이 어색함
- 패킷 ID가 정적으로 매겨져 있어 리버스 엔지니어링에 취약함
- `Room::Enter`/`Leave`/`Broadcast`가 `public`으로 열려 있음

## 커밋 규칙
커밋 메시지는 `태그 | 커밋 내용 요약` 형식으로 작성한다.
- `Feat` : 뭔가 새로운 기능 추가
- `Fix` : 기능 수정
- `Refactor` : 리팩토링
- `Debug` : 버그 수정
- 위 태그로 애매하면 상황에 맞는 태그를 적절히 새로 사용

## 참고
강의 진행 과정과 각 개념에 대한 학습 노트는 원래 강의 레포(`CPP_Server\Server`)의 `Note/` 폴더에 남아있다. 필요하면 해당 레포를 참고할 것.
