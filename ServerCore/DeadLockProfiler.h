#pragma once
#include <stack>
#include <map>
#include <vector>

/*--------------------
	DeadLockProfiler

	처음 설계될 때 멀티스레드 환경 고려하지 않고 TLS 영역에 들어갈거로 예상하고 만들었다가 문제가 발생
	락 스택(_lockStack)이라는 것 자체가 스레드마다 자기가 호출하고 있는 그 락의 순서가 달라지게 됨
	이 락 스택을 공용으로 사용하고 있다가 문제가 발생함
	스레드마다 자신의 락 스택이 있어야 한다 -> TLS로 변경함
	히스토리는 공용으로 관리
---------------------*/

class DeadLockProfiler
{
public:
	void PushLock(const char* name);
	void PopLock(const char* name);
	void CheckCycle();

private:
	void Dfs(int32 index);

private:
	unordered_map<const char*, int32>	_nameToId;
	unordered_map<int32, const char*>	_idToName;
	map<int32, set<int32>>				_lockHistory;		// 락을 잡은 기록

	Mutex _lock;

private:
	// 사이클을 돌리기 위한 변수
	vector<int32>	_discoveredOrder; // 노드가 발견된 순서를 기록하는 배열
	int32			_discoveredCount = 0; // 노드가 발견된 순서
	vector<bool>	_finished; // Dfs(i)가 종료 되었는지 여부
	vector<int32>	_parent;	// 자식 노드를 발견한 부모 노드
};

