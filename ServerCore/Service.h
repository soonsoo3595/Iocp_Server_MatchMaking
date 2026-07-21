#pragma once
#include "NetAddress.h"
#include "IocpCore.h"
#include <functional>

enum class ServiceType : uint8
{
	Server,
	Client
};

/*-------------
	Service
--------------*/

using SessionFactory = function<SessionRef(void)>;

class Service : public enable_shared_from_this<Service>
{
public:
	Service(ServiceType type, NetAddress address, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount = 1);
	virtual ~Service();

	virtual bool		Start() abstract;
	bool				CanStart()								{ return _sessionFactory != nullptr; }
	virtual void		CloseService();

	void				Broadcast(SendBufferRef sendBuffer);
	bool				Dispatch(uint32 timeoutMs = INFINITE)	{ return _iocpCore->Dispatch(timeoutMs); }
	bool				Register(IocpObjectRef iocpObject)		{ return _iocpCore->Register(iocpObject); }

	void				SetSessionFactory(SessionFactory func)	{ _sessionFactory = func; }
	SessionRef			CreateSession();
	void				AddSession(SessionRef session);
	void				ReleaseSession(SessionRef session);

public:
	ServiceType			GetServiceType()			{ return _type; }
	NetAddress			GetNetAddress()				{ return _netAddress; }

	int32				GetCurrentSessionCount()	{ READ_LOCK; return static_cast<int32>(_sessions.size()); }
	int32				GetMaxSessionCount()		{ return _maxSessionCount; }

protected:
	USE_LOCK;
	ServiceType			_type;
	NetAddress			_netAddress = {};
	IocpCoreRef			_iocpCore;

	Set<SessionRef>		_sessions;
	int32				_maxSessionCount = 0;
	SessionFactory		_sessionFactory;
};

/*-----------------
	ClientService
------------------*/

class ClientService : public Service
{
public:
	ClientService(NetAddress targetAddress, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount = 1);
	virtual ~ClientService() {}

	virtual bool	Start() override;
};


/*-----------------
	ServerService
------------------*/

class ServerService : public Service
{
public:
	ServerService(NetAddress targetAddress, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount = 1);
	virtual ~ServerService() {}

	virtual bool	Start() override;
	virtual void	CloseService() override;

private:
	ListenerRef		_listener = nullptr;
};