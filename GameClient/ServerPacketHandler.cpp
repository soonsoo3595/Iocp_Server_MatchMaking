#include "pch.h"
#include "ServerPacketHandler.h"
#include "ClientState.h"
#include "FileUtils.h"

PacketHandlerFunc GPacketHandler[UINT16_MAX];

// 직접 컨텐츠 작업자

bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	// TODO : Log
	return false;
}

bool Handle_S_LOGIN(PacketSessionRef& session, Protocol::S_LOGIN& pkt)
{
	if (pkt.success() == false)
	{
		LOG_WARNING(L"로그인 실패");
		return true;
	}

	const Protocol::Player& player = pkt.player();
	LOG_INFO(L"로그인 성공 : id=%llu, 닉네임 = %s", player.id(), FileUtils::Convert(player.name()).c_str());

	GClientState = ClientState::LOBBY;

	return true;
}

bool Handle_S_MATCH_QUEUED(PacketSessionRef& session, Protocol::S_MATCH_QUEUED& pkt)
{
	LOG_INFO(L"매칭 대기열에 등록되었습니다. 매칭을 기다리는 중...");
	GClientState = ClientState::MATCHING;
	return true;
}

bool Handle_S_MATCH_CANCELED(PacketSessionRef& session, Protocol::S_MATCH_CANCELED& pkt)
{
	LOG_INFO(L"매칭이 취소되었습니다.");
	GClientState = ClientState::LOBBY;
	return true;
}

// 표시용 포지션 이름 (GameClient.cpp에도 같은 목적의 헬퍼가 있음 - 공용 헤더로 뺄 정도는 아니라 중복 허용)
static const char* PositionToString(Protocol::Position position)
{
	switch (position)
	{
	case Protocol::POSITION_TOP: return "TOP";
	case Protocol::POSITION_JUG: return "JUG";
	case Protocol::POSITION_MID: return "MID";
	case Protocol::POSITION_BOT: return "BOT";
	case Protocol::POSITION_SUP: return "SUP";
	default: return "상관없음";
	}
}

bool Handle_S_MATCH_FOUND(PacketSessionRef& session, Protocol::S_MATCH_FOUND& pkt)
{
	LOG_INFO(L"매칭 성사! matchId=%llu, 내 포지션=%hs", pkt.matchid(), PositionToString(pkt.myposition()));

	// name()은 UTF-8 std::string이라, 콘솔이 UTF-8을 받아주는 한 narrow cout으로 그대로 출력하면 된다.
	// (와이드 LOG_INFO의 %hs와 달리, cout은 로케일 변환 없이 바이트를 그대로 흘려보낸다)
	cout << "\n===== 매칭 성사 =====" << endl;
	cout << "내 팀:" << endl;
	for (const Protocol::PlayerInfo& info : pkt.myteam())
	{
		cout << "  - " << info.name() << " (" << PositionToString(info.position()) << ")" << endl;
	}
	cout << "상대 팀:" << endl;
	for (const Protocol::PlayerInfo& info : pkt.enemyteam())
	{
		cout << "  - " << info.name() << " (" << PositionToString(info.position()) << ")" << endl;
	}
	cout << "=====================" << endl;
	// TODO : 챔피언 선택(ChampSelect) 단계는 아직 구현 전. 지금은 결과만 보여주고 대기.

	GClientState = ClientState::MATCH_FOUND;

	return true;
}

bool Handle_S_CHAMPSELECT_START(PacketSessionRef& session, Protocol::S_CHAMPSELECT_START& pkt)
{
	LOG_INFO(L"전원 수락 완료! 챔피언 선택으로 진입합니다. matchId=%llu", pkt.matchid());

	cout << "\n===== 챔피언 선택 진입 (matchId=" << pkt.matchid() << ") =====" << endl;
	for (const Protocol::PlayerInfo& info : pkt.participants())
	{
		cout << "  - " << info.name() << " (" << PositionToString(info.position()) << ")" << endl;
	}
	cout << "=====================================" << endl;

	GClientState = ClientState::CHAMP_SELECT;

	return true;
}

bool Handle_S_PICK_UPDATE(PacketSessionRef& session, Protocol::S_PICK_UPDATE& pkt)
{
	LOG_INFO(L"픽 현황 : playerId=%llu, championId=%u", pkt.playerid(), pkt.championid());
	cout << "  [픽] playerId=" << pkt.playerid() << " -> championId=" << pkt.championid() << endl;
	return true;
}

bool Handle_S_PICK_FAILED(PacketSessionRef& session, Protocol::S_PICK_FAILED& pkt)
{
	LOG_WARNING(L"픽 실패 : 이미 픽했거나 다른 사람이 먼저 픽한 챔피언입니다. 다시 시도하세요.");
	cout << "픽 실패 - 이미 픽했거나 다른 사람이 먼저 픽한 챔피언입니다." << endl;

	// 다시 골라야 하니 챔피언 선택 화면으로 복귀 (WAITING_GAME_START로 넘어가있던 상태 되돌림)
	GClientState = ClientState::CHAMP_SELECT;

	return true;
}

bool Handle_S_GAME_START(PacketSessionRef& session, Protocol::S_GAME_START& pkt)
{
	LOG_INFO(L"전원 픽 완료! 게임을 시작합니다. matchId=%llu", pkt.matchid());

	cout << "\n===== 게임 시작 (matchId=" << pkt.matchid() << ") =====" << endl;
	for (const Protocol::PlayerInfo& info : pkt.finalteams())
	{
		cout << "  - " << info.name() << " (" << PositionToString(info.position())
			<< ", championId=" << info.championid() << ")" << endl;
	}
	cout << "===========================================" << endl;
	cout << "(실제 인게임 로직은 기획 범위 밖 - 매치메이킹 흐름은 여기서 끝입니다)" << endl;

	GClientState = ClientState::GAME_STARTED;

	return true;
}

bool Handle_S_CHAT(PacketSessionRef& session, Protocol::S_CHAT& pkt)
{
	cout << "[채팅] playerId=" << pkt.playerid() << " : " << pkt.msg() << endl;
	return true;
}