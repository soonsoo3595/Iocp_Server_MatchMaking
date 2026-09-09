#include "pch.h"
#include "ThreadManager.h"
#include "Service.h"
#include "Session.h"
#include "ServerPacketHandler.h"
#include "ClientState.h"
#include <cstdlib>
#include <limits>

// 콘솔에서 입력받은 로그인 닉네임 (텍스트 기반 테스트용)
string GLoginName;

PacketSessionRef GSession = nullptr;
Atomic<ClientState> GClientState = ClientState::LOGOUT;

class ServerSession : public PacketSession
{
public:
	~ServerSession()
	{
		cout << "~ServerSession" << endl;
	}

	virtual void OnConnected() override
	{
		// 게임 서버로 로그인하는 상황
		LOG_INFO(L"로그인 시도 : 닉네임 = %hs", GLoginName.c_str());

		GSession = GetPacketSessionRef();

		Protocol::C_LOGIN pkt;
		pkt.set_name(GLoginName);
		auto sendBuffer = ServerPacketHandler::MakeSendBuffer(pkt);
		Send(sendBuffer);
	}

	virtual void OnRecvPacket(BYTE* buffer, int32 len) override
	{
		PacketSessionRef session = GetPacketSessionRef();
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);

		// TODO : packetId 대역 체크
		ServerPacketHandler::HandlePacket(session, buffer, len);
	}

	virtual void OnSend(int32 len) override
	{
		//cout << "OnSend Len = " << len << endl;
	}

	virtual void OnDisconnected() override
	{
		//cout << "Disconnected" << endl;
		GSession = nullptr;
		GClientState = ClientState::LOGOUT;
	}
};

// 로그/메뉴 출력용 포지션 이름
const char* PositionToString(Protocol::Position position)
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

// 포지션 하나를 콘솔에서 골라 받는다. label은 "1지망"/"2지망"처럼 프롬프트에 붙일 설명.
Protocol::Position SelectPosition(const char* label)
{
	while (true)
	{
		cout << "\n" << label << " 포지션을 선택하세요.\n"
			"0. 상관없음\n1. TOP\n2. JUG\n3. MID\n4. BOT\n5. SUP\n선택 : ";

		int32 choice = 0;
		if (!(cin >> choice))
		{
			cin.clear();
			cin.ignore(numeric_limits<streamsize>::max(), '\n');
			continue;
		}

		switch (choice)
		{
		case 0: return Protocol::POSITION_NONE;
		case 1: return Protocol::POSITION_TOP;
		case 2: return Protocol::POSITION_JUG;
		case 3: return Protocol::POSITION_MID;
		case 4: return Protocol::POSITION_BOT;
		case 5: return Protocol::POSITION_SUP;
		default:
			cout << "잘못된 선택입니다." << endl;
			break;
		}
	}
}

// 로비 화면 : 매칭 시작 전 (1. 매칭 시작 / 2. 로그아웃 후 게임 종료)
void RunLobbyMenu()
{
	cout << "\n1. 매칭 시작\n2. 로그아웃 후 게임 종료\n선택 : ";

	int32 choice = 0;
	if (!(cin >> choice))
	{
		// 숫자가 아닌 입력 -> 버퍼 비우고 다시 물어봄
		cin.clear();
		cin.ignore(numeric_limits<streamsize>::max(), '\n');
		return;
	}

	switch (choice)
	{
	case 1:
	{
		if (GSession == nullptr)
		{
			cout << "아직 서버에 연결되지 않았습니다." << endl;
			return;
		}

		const Protocol::Position primary = SelectPosition("1지망");

		// 1지망이 "상관없음"이면 2지망은 의미가 없으니 안 물어봄 (기획서 §7 C_MATCH_START 주석과 동일)
		Protocol::Position secondary = Protocol::POSITION_NONE;
		if (primary != Protocol::POSITION_NONE)
			secondary = SelectPosition("2지망 (상관없으면 0)");

		Protocol::C_MATCH_START startPkt;
		startPkt.set_primaryposition(primary);
		startPkt.set_secondaryposition(secondary);
		auto sendBuffer = ServerPacketHandler::MakeSendBuffer(startPkt);
		GSession->Send(sendBuffer);

		GClientState = ClientState::MATCHING;
		cout << "매칭 시작을 요청했습니다. (1지망=" << PositionToString(primary)
			<< ", 2지망=" << PositionToString(secondary) << ")" << endl;
		break;
	}
	case 2:
		cout << "게임을 종료합니다." << endl;
		// exit(0)은 정적 저장 기간 객체(GCoreGlobal 등)의 소멸자를 실행하는데,
		// ThreadManager::~ThreadManager()가 Join()을 호출하고, 네트워크 워커 스레드는
		// while(true) Dispatch() 무한 루프라 절대 안 끝나서 여기서 영원히 멈춰버린다.
		// 종료 신호로 루프를 빠져나오게 만드는 게 정석이지만, 지금은 테스트 클라이언트라
		// 정적 소멸자를 아예 안 돌리는 std::_Exit로 즉시 종료한다.
		std::_Exit(0);
		break;
	default:
		cout << "잘못된 선택입니다." << endl;
		break;
	}
}

// 매칭 대기 화면 : 매칭 성사를 기다리며, 취소만 가능
void RunMatchingMenu()
{
	cout << "\n매칭 대기 중입니다. 취소하려면 c 를 입력하세요 : ";

	string input;
	cin >> input;

	if (input != "c" && input != "C")
	{
		cout << "잘못된 입력입니다." << endl;
		return;
	}

	if (GSession != nullptr)
	{
		Protocol::C_MATCH_CANCEL cancelPkt;
		auto sendBuffer = ServerPacketHandler::MakeSendBuffer(cancelPkt);
		GSession->Send(sendBuffer);
	}

	GClientState = ClientState::LOBBY;
	cout << "매칭을 취소했습니다." << endl;
}

// 매칭 성사 화면 : 수락할지 거절할지 물어본다.
void RunMatchFoundMenu()
{
	cout << "\n1. 수락\n2. 거절\n선택 : ";

	int32 choice = 0;
	if (!(cin >> choice))
	{
		cin.clear();
		cin.ignore(numeric_limits<streamsize>::max(), '\n');
		return;
	}

	if (GSession == nullptr)
	{
		cout << "서버와의 연결이 끊겼습니다." << endl;
		return;
	}

	switch (choice)
	{
	case 1:
	{
		Protocol::C_MATCH_ACCEPT acceptPkt;
		auto sendBuffer = ServerPacketHandler::MakeSendBuffer(acceptPkt);
		GSession->Send(sendBuffer);

		GClientState = ClientState::WAITING_ACCEPT_RESULT;
		cout << "수락했습니다. 다른 플레이어들의 응답을 기다리는 중..." << endl;
		break;
	}
	case 2:
	{
		Protocol::C_MATCH_DECLINE declinePkt;
		auto sendBuffer = ServerPacketHandler::MakeSendBuffer(declinePkt);
		GSession->Send(sendBuffer);

		GClientState = ClientState::WAITING_ACCEPT_RESULT;
		cout << "거절했습니다." << endl;
		break;
	}
	default:
		cout << "잘못된 선택입니다." << endl;
		break;
	}
}

void RunConsoleMenuLoop()
{
	while (true)
	{
		switch (GClientState.load())
		{
		case ClientState::LOGOUT:
			// 아직 접속/로그인이 안 끝난 상태 -> 콘솔 입력을 받지 않고 대기.
			// (여기서 cin으로 뭔가 물어보면 연결되기도 전에 메뉴가 떠버림)
			this_thread::sleep_for(100ms);
			break;
		case ClientState::LOBBY:
			RunLobbyMenu();
			break;
		case ClientState::MATCHING:
			RunMatchingMenu();
			break;
		case ClientState::MATCH_FOUND:
			RunMatchFoundMenu();
			break;
		case ClientState::WAITING_ACCEPT_RESULT:
			// 서버가 S_CHAMPSELECT_START/S_MATCH_QUEUED/S_MATCH_CANCELED 중 하나를 보내줄
			// 때까지는 딱히 할 게 없다 - 그 핸들러들이 알아서 상태를 바꿔준다.
			this_thread::sleep_for(100ms);
			break;
		case ClientState::CHAMP_SELECT:
			// 챔피언 선택 UI는 아직 구현 전. 참가자 목록은 Handle_S_CHAMPSELECT_START에서 이미 출력함.
			this_thread::sleep_for(100ms);
			break;
		}
	}
}

int main()
{
	GLogger->Init(LogOutput::Console, LogLevel::Log);

	ServerPacketHandler::Init();

	cout << "닉네임을 입력하세요: ";

	// 콘솔 입력은 시스템 로캘(한국어 Windows면 CP949)로 들어오는데,
	// Protobuf의 string은 항상 UTF-8이어야 하므로 와이드 문자로 받아서 변환한다.
	wstring wideName;
	std::getline(wcin, wideName);

	const int32 utf8Len = ::WideCharToMultiByte(CP_UTF8, 0, wideName.c_str(), static_cast<int32>(wideName.size()), NULL, 0, NULL, NULL);
	GLoginName.resize(utf8Len);
	::WideCharToMultiByte(CP_UTF8, 0, wideName.c_str(), static_cast<int32>(wideName.size()), &GLoginName[0], utf8Len, NULL, NULL);

	this_thread::sleep_for(1s);

	ClientServiceRef service = MakeShared<ClientService>(
		NetAddress(L"127.0.0.1", 7777),
		MakeShared<IocpCore>(),
		MakeShared<ServerSession>, // TODO : SessionManager 등
		1);

	ASSERT_CRASH(service->Start());

	for (int32 i = 0; i < 2; i++)
	{
		GThreadManager->Launch([=]()
			{
				while (true)
				{
					service->Dispatch();
				}
			});
	}

	cout << "서버에 연결 중입니다..." << endl;

	// 네트워크 워커 스레드는 백그라운드에서 계속 돌고, 메인 스레드는 콘솔 메뉴를 담당한다.
	// GClientState가 LOGOUT인 동안은 접속/로그인 완료를 기다리기만 하고 메뉴를 띄우지 않는다.
	RunConsoleMenuLoop();

	GThreadManager->Join();
}
