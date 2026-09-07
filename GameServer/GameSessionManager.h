#pragma once

class GameSession;

using GameSessionRef = shared_ptr<GameSession>;

class GameSessionManager
{
public:
	void Add(GameSessionRef session);
	void Remove(GameSessionRef session);
	void Broadcast(SendBufferRef sendBuffer);

	// 로그인 시 이름 중복을 원자적으로 체크+예약한다. 이미 쓰이고 있으면 false.
	bool TryReserveName(const string& name);

	// 접속 종료 시(로그인 성공했던 세션만) 이름을 반납한다.
	void ReleaseName(const string& name);

private:
	USE_LOCK;
	Set<GameSessionRef>		_sessions;
	HashSet<string>			_activeNames;
};

extern GameSessionManager GSessionManager;
