#include "pch.h"
#include "MmrManager.h"

MmrManager GMmrManager;

uint32 MmrManager::GetOrCreateMmr(const string& name)
{
	WRITE_LOCK;

	auto findIt = _mmrByName.find(name);
	if (findIt != _mmrByName.end())
		return findIt->second;

	static thread_local mt19937 rng{ random_device{}() };
	uniform_int_distribution<uint32> dist(MMR_MIN, MMR_MAX);
	const uint32 mmr = dist(rng);

	_mmrByName[name] = mmr;
	return mmr;
}
