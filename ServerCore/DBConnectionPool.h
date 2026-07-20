#pragma once
#include "DBConnection.h"

/*-------------------
	DBConnectionPool
--------------------*/

class DBConnectionPool
{
public:
	DBConnectionPool();
	~DBConnectionPool();

	bool					Connect(int32 connectionCount, const WCHAR* connectionString);
	void					Clear();

	// Pop 후 사용하고 바로 반납할거기에 스마트 포인터 x
	DBConnection*			Pop();
	void					Push(DBConnection* connection);

private:
	USE_LOCK;
	SQLHENV					_environment = SQL_NULL_HANDLE;		// DB 환경 담당
	Vector<DBConnection*>	_connections;
};

