#include "pch.h"
#include "GameSession.h"
#include "GameSessionManager.h"
#include "ClientPacketHandler.h"
#include "FileUtils.h"
#include "Room.h"
#include "Player.h"

void GameSession::OnConnected()
{
	GSessionManager.Add(static_pointer_cast<GameSession>(shared_from_this()));
}

void GameSession::OnDisconnected()
{
	GSessionManager.Remove(static_pointer_cast<GameSession>(shared_from_this()));

	if (_player)
	{
		// _player->name은 UTF-8 바이트라 %hs(현재 로케일/ANSI 기준 변환)로 찍으면 한글이 깨진다.
		// FileUtils::Convert로 UTF-8 -> UTF-16 변환 후 %s로 찍어야 한다.
		LOG_INFO(L"Player logged out. playerId=%llu, name=%s", _player->playerId, FileUtils::Convert(_player->name).c_str());
		GSessionManager.ReleaseName(_player->name);
	}

	/*
	if (_player)
	{
		if(auto room = _room.lock())
		{
			room->DoAsync(&Room::Leave, _currentPlayer);
		}
	}

	_currentPlayer = nullptr;
	_players.clear();
	*/
}

void GameSession::OnRecvPacket(BYTE* buffer, int32 len)
{
	PacketSessionRef session = GetPacketSessionRef();
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);

	// ToDo : packetId 대역 체크
	ClientPacketHandler::HandlePacket(session, buffer, len);
}

void GameSession::OnSend(int32 len)
{
}