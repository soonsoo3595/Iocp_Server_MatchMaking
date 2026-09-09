#pragma once
#include "Protocol.pb.h"

using PacketHandlerFunc = std::function<bool(PacketSessionRef&, BYTE*, int32)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];

enum : uint16
{
	PKT_C_LOGIN = 1000,
	PKT_S_LOGIN = 1001,
	PKT_C_MATCH_START = 1002,
	PKT_S_MATCH_QUEUED = 1003,
	PKT_S_MATCH_FOUND = 1004,
	PKT_C_MATCH_ACCEPT = 1005,
	PKT_C_MATCH_DECLINE = 1006,
	PKT_S_CHAMPSELECT_START = 1007,
	PKT_C_PICK_CHAMPION = 1008,
	PKT_S_PICK_UPDATE = 1009,
	PKT_S_PICK_FAILED = 1010,
	PKT_S_GAME_START = 1011,
	PKT_C_MATCH_CANCEL = 1012,
	PKT_S_MATCH_CANCELED = 1013,
	PKT_C_ENTER_GAME = 1014,
	PKT_S_ENTER_GAME = 1015,
	PKT_C_CHAT = 1016,
	PKT_S_CHAT = 1017,
};

// Custom Handlers
bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len);
bool Handle_S_LOGIN(PacketSessionRef& session, Protocol::S_LOGIN& pkt);
bool Handle_S_MATCH_QUEUED(PacketSessionRef& session, Protocol::S_MATCH_QUEUED& pkt);
bool Handle_S_MATCH_FOUND(PacketSessionRef& session, Protocol::S_MATCH_FOUND& pkt);
bool Handle_S_CHAMPSELECT_START(PacketSessionRef& session, Protocol::S_CHAMPSELECT_START& pkt);
bool Handle_S_PICK_UPDATE(PacketSessionRef& session, Protocol::S_PICK_UPDATE& pkt);
bool Handle_S_PICK_FAILED(PacketSessionRef& session, Protocol::S_PICK_FAILED& pkt);
bool Handle_S_GAME_START(PacketSessionRef& session, Protocol::S_GAME_START& pkt);
bool Handle_S_MATCH_CANCELED(PacketSessionRef& session, Protocol::S_MATCH_CANCELED& pkt);
bool Handle_S_ENTER_GAME(PacketSessionRef& session, Protocol::S_ENTER_GAME& pkt);
bool Handle_S_CHAT(PacketSessionRef& session, Protocol::S_CHAT& pkt);

class ServerPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
		GPacketHandler[PKT_S_LOGIN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_LOGIN>(Handle_S_LOGIN, session, buffer, len); };
		GPacketHandler[PKT_S_MATCH_QUEUED] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MATCH_QUEUED>(Handle_S_MATCH_QUEUED, session, buffer, len); };
		GPacketHandler[PKT_S_MATCH_FOUND] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MATCH_FOUND>(Handle_S_MATCH_FOUND, session, buffer, len); };
		GPacketHandler[PKT_S_CHAMPSELECT_START] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_CHAMPSELECT_START>(Handle_S_CHAMPSELECT_START, session, buffer, len); };
		GPacketHandler[PKT_S_PICK_UPDATE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_PICK_UPDATE>(Handle_S_PICK_UPDATE, session, buffer, len); };
		GPacketHandler[PKT_S_PICK_FAILED] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_PICK_FAILED>(Handle_S_PICK_FAILED, session, buffer, len); };
		GPacketHandler[PKT_S_GAME_START] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_GAME_START>(Handle_S_GAME_START, session, buffer, len); };
		GPacketHandler[PKT_S_MATCH_CANCELED] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_MATCH_CANCELED>(Handle_S_MATCH_CANCELED, session, buffer, len); };
		GPacketHandler[PKT_S_ENTER_GAME] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_ENTER_GAME>(Handle_S_ENTER_GAME, session, buffer, len); };
		GPacketHandler[PKT_S_CHAT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::S_CHAT>(Handle_S_CHAT, session, buffer, len); };
	}

	static bool HandlePacket(PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
		return GPacketHandler[header->id](session, buffer, len);
	}
	static SendBufferRef MakeSendBuffer(Protocol::C_LOGIN& pkt) { return MakeSendBuffer(pkt, PKT_C_LOGIN); }
	static SendBufferRef MakeSendBuffer(Protocol::C_MATCH_START& pkt) { return MakeSendBuffer(pkt, PKT_C_MATCH_START); }
	static SendBufferRef MakeSendBuffer(Protocol::C_MATCH_ACCEPT& pkt) { return MakeSendBuffer(pkt, PKT_C_MATCH_ACCEPT); }
	static SendBufferRef MakeSendBuffer(Protocol::C_MATCH_DECLINE& pkt) { return MakeSendBuffer(pkt, PKT_C_MATCH_DECLINE); }
	static SendBufferRef MakeSendBuffer(Protocol::C_PICK_CHAMPION& pkt) { return MakeSendBuffer(pkt, PKT_C_PICK_CHAMPION); }
	static SendBufferRef MakeSendBuffer(Protocol::C_MATCH_CANCEL& pkt) { return MakeSendBuffer(pkt, PKT_C_MATCH_CANCEL); }
	static SendBufferRef MakeSendBuffer(Protocol::C_ENTER_GAME& pkt) { return MakeSendBuffer(pkt, PKT_C_ENTER_GAME); }
	static SendBufferRef MakeSendBuffer(Protocol::C_CHAT& pkt) { return MakeSendBuffer(pkt, PKT_C_CHAT); }

private:
	template<typename PacketType, typename ProcessFunc>
	static bool HandlePacket(ProcessFunc func, PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketType pkt;
		if (pkt.ParseFromArray(buffer + sizeof(PacketHeader), len - sizeof(PacketHeader)) == false)
			return false;

		return func(session, pkt);
	}

	template<typename T>
	static SendBufferRef MakeSendBuffer(T& pkt, uint16 pktId)
	{
		const uint16 dataSize = static_cast<uint16>(pkt.ByteSizeLong());
		const uint16 packetSize = dataSize + sizeof(PacketHeader);

		SendBufferRef sendBuffer = GSendBufferManager->Open(packetSize);
		PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
		header->size = packetSize;
		header->id = pktId;
		ASSERT_CRASH(pkt.SerializeToArray(&header[1], dataSize));
		sendBuffer->Close(packetSize);

		return sendBuffer;
	}
};