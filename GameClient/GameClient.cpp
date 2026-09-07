#include "pch.h"
#include "ThreadManager.h"
#include "Service.h"
#include "Session.h"
#include "ServerPacketHandler.h"

// 콘솔에서 입력받은 로그인 닉네임 (텍스트 기반 테스트용)
string GLoginName;

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
	}
};

int main()
{
	GLogger->Init(LogOutput::Console, LogLevel::Verbose);

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

	GThreadManager->Join();
}