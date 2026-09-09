#pragma once

// 클라이언트가 지금 어느 화면(메뉴)에 있는지. 메인 스레드(콘솔 입력 루프)와 네트워크 워커 스레드
// (OnConnected/OnDisconnected, S_MATCH_QUEUED/S_MATCH_CANCELED 핸들러) 양쪽에서 건드릴 수 있어서
// Atomic으로 공유한다. 지금은 단순 store/load 수준이라 이걸로 충분하지만, 상태가 더 복잡해지면
// (여러 필드를 한 번에 묶어 바꿔야 하는 경우 등) 재검토가 필요하다.
enum class ClientState : int32
{
	LOGOUT,				// 접속/로그인 전 (또는 접속이 끊긴 상태)
	LOBBY,				// 로그인 완료, 매칭 시작 전
	MATCHING,			// 매칭 대기열에 등록된 상태
	MATCH_FOUND,		// 매칭 성사됨. 수락/거절을 물어보는 중.
	WAITING_ACCEPT_RESULT,	// 수락/거절 응답을 보냈고, 전원 결과(챔프선택 진입 or 재매칭/로비)를 기다리는 중
	CHAMP_SELECT,		// 전원 수락 완료, 챔피언 선택 진입 (아직 픽 UI는 없음)
};

extern Atomic<ClientState> GClientState;

// 지금 서버와 맺어진 세션. C_MATCH_START/C_MATCH_CANCEL처럼 패킷 핸들러 밖(메뉴 입력 루프)에서도
// 패킷을 보낼 수 있어야 해서 전역으로 하나 들고 있는다.
// (콘솔 클라이언트라 접속이 1개뿐이라는 전제 하에 단순화한 것 — 세션이 여러 개면 이 방식은 안 통함)
extern PacketSessionRef GSession;
