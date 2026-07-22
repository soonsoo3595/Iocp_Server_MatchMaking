#include "pch.h"
#include "Service.h"
#include "Session.h"
#include "Listener.h"

/*-------------
	Service
--------------*/

Service::Service(ServiceType type, NetAddress address, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount)
	: _type(type), _netAddress(address), _iocpCore(core), _sessionFactory(factory), _maxSessionCount(maxSessionCount)
{

}

Service::~Service()
{
}

void Service::CloseService()
{
	// TODO : Session 정리
}

void Service::Broadcast(SendBufferRef sendBuffer)
{
	// TODO : 여기서 순회만 하고 _sessions를 수정하지 않는데 WRITE_LOCK을 잡고 있음 -> READ_LOCK으로 바꿀 수 있는지 검토
	WRITE_LOCK;
	for (const auto& session : _sessions)
	{
		session->Send(sendBuffer);
	}
}

SessionRef Service::CreateSession()
{
	SessionRef session = _sessionFactory();
	session->SetService(shared_from_this());

	if (_iocpCore->Register(session) == false)
	{
		LOG_ERROR(L"Service::CreateSession failed to register session to IOCP");
		return nullptr;
	}

	LOG_VERBOSE(L"Service::CreateSession session created");
	return session;
}

void Service::AddSession(SessionRef session)
{
	WRITE_LOCK;
	_sessions.insert(session);
	LOG_VERBOSE(L"Service::AddSession session count=%d/%d", static_cast<int32>(_sessions.size()), _maxSessionCount);
}

void Service::ReleaseSession(SessionRef session)
{
	WRITE_LOCK;
	ASSERT_CRASH(_sessions.erase(session) != 0);
	LOG_VERBOSE(L"Service::ReleaseSession session count=%d/%d", static_cast<int32>(_sessions.size()), _maxSessionCount);
}

/*-----------------
	ClientService
------------------*/

ClientService::ClientService(NetAddress targetAddress, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount)
	: Service(ServiceType::Client, targetAddress, core, factory, maxSessionCount)
{
}

bool ClientService::Start()
{
	if (CanStart() == false)
		return false;

	const int32 maxSessionCount = GetMaxSessionCount();
	for (int32 i = 0; i < maxSessionCount; i++)
	{
		SessionRef session = CreateSession();

		// TODO : 세션 연결(Connect)에 실패했을 때 이전에 만든 세션들을 롤백/정리할지 결정 필요
		if (session != nullptr && session->Connect() == false)
		{
			LOG_ERROR(L"ClientService::Start session Connect failed");
			return false;
		}
	}

	LOG_INFO(L"ClientService::Start succeeded. sessionCount=%d", maxSessionCount);
	return true;
}

ServerService::ServerService(NetAddress address, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount)
	: Service(ServiceType::Server, address, core, factory, maxSessionCount)
{
}

bool ServerService::Start()
{
	if (CanStart() == false)
		return false;

	_listener = MakeShared<Listener>();
	if (_listener == nullptr)
	{
		LOG_ERROR(L"ServerService::Start failed to create Listener");
		return false;
	}

	ServerServiceRef service = static_pointer_cast<ServerService>(shared_from_this());
	if (_listener->StartAccept(service) == false)
	{
		LOG_ERROR(L"ServerService::Start Listener StartAccept failed");
		return false;
	}

	LOG_INFO(L"ServerService::Start succeeded");
	return true;
}

void ServerService::CloseService()
{
	if (_listener != nullptr)
	{
		_listener->CloseSocket();
		_listener = nullptr;
	}

	Service::CloseService();
}
