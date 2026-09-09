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
	PKT_C_MATCH_CANCEL = 1008,
	PKT_S_MATCH_CANCELED = 1009,
	PKT_C_ENTER_GAME = 1010,
	PKT_S_ENTER_GAME = 1011,
	PKT_C_CHAT = 1012,
	PKT_S_CHAT = 1013,
};

// Custom Handlers
bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len);
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt);
bool Handle_C_MATCH_START(PacketSessionRef& session, Protocol::C_MATCH_START& pkt);
bool Handle_C_MATCH_ACCEPT(PacketSessionRef& session, Protocol::C_MATCH_ACCEPT& pkt);
bool Handle_C_MATCH_DECLINE(PacketSessionRef& session, Protocol::C_MATCH_DECLINE& pkt);
bool Handle_C_MATCH_CANCEL(PacketSessionRef& session, Protocol::C_MATCH_CANCEL& pkt);
bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt);
bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt);

class ClientPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
		GPacketHandler[PKT_C_LOGIN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_LOGIN>(Handle_C_LOGIN, session, buffer, len); };
		GPacketHandler[PKT_C_MATCH_START] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_MATCH_START>(Handle_C_MATCH_START, session, buffer, len); };
		GPacketHandler[PKT_C_MATCH_ACCEPT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_MATCH_ACCEPT>(Handle_C_MATCH_ACCEPT, session, buffer, len); };
		GPacketHandler[PKT_C_MATCH_DECLINE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_MATCH_DECLINE>(Handle_C_MATCH_DECLINE, session, buffer, len); };
		GPacketHandler[PKT_C_MATCH_CANCEL] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_MATCH_CANCEL>(Handle_C_MATCH_CANCEL, session, buffer, len); };
		GPacketHandler[PKT_C_ENTER_GAME] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_ENTER_GAME>(Handle_C_ENTER_GAME, session, buffer, len); };
		GPacketHandler[PKT_C_CHAT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_CHAT>(Handle_C_CHAT, session, buffer, len); };
	}

	static bool HandlePacket(PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
		return GPacketHandler[header->id](session, buffer, len);
	}
	static SendBufferRef MakeSendBuffer(Protocol::S_LOGIN& pkt) { return MakeSendBuffer(pkt, PKT_S_LOGIN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MATCH_QUEUED& pkt) { return MakeSendBuffer(pkt, PKT_S_MATCH_QUEUED); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MATCH_FOUND& pkt) { return MakeSendBuffer(pkt, PKT_S_MATCH_FOUND); }
	static SendBufferRef MakeSendBuffer(Protocol::S_CHAMPSELECT_START& pkt) { return MakeSendBuffer(pkt, PKT_S_CHAMPSELECT_START); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MATCH_CANCELED& pkt) { return MakeSendBuffer(pkt, PKT_S_MATCH_CANCELED); }
	static SendBufferRef MakeSendBuffer(Protocol::S_ENTER_GAME& pkt) { return MakeSendBuffer(pkt, PKT_S_ENTER_GAME); }
	static SendBufferRef MakeSendBuffer(Protocol::S_CHAT& pkt) { return MakeSendBuffer(pkt, PKT_S_CHAT); }

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