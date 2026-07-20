#include "pch.h"
#include "SendBuffer.h"

/*----------------
	SendBuffer
-----------------*/

SendBuffer::SendBuffer(SendBufferChunkRef owner, BYTE* buffer, uint32 allocSize)
	: _owner(owner), _buffer(buffer), _allocSize(allocSize)
{
}

SendBuffer::~SendBuffer()
{
}

void SendBuffer::Close(uint32 writeSize)
{
	ASSERT_CRASH(_allocSize >= writeSize);
	_writeSize = writeSize;
	_owner->Close(writeSize);
}

/*--------------------
	SendBufferChunk
--------------------*/

SendBufferChunk::SendBufferChunk()
{
}

SendBufferChunk::~SendBufferChunk()
{
}

void SendBufferChunk::Reset()
{
	_open = false;
	_usedSize = 0;
}

SendBufferRef SendBufferChunk::Open(uint32 allocSize)
{
	ASSERT_CRASH(allocSize <= SEND_BUFFER_CHUNK_SIZE);
	ASSERT_CRASH(_open == false);

	if (allocSize > FreeSize())
		return nullptr;

	_open = true;
	return ObjectPool<SendBuffer>::MakeShared(shared_from_this(), Buffer(), allocSize);
}

void SendBufferChunk::Close(uint32 writeSize)
{
	ASSERT_CRASH(_open == true);
	_open = false;
	_usedSize += writeSize;
}

/*---------------------
	SendBufferManager
----------------------*/
// [*****       ](SendBuffer) 중에 사용할만큼
// 데이터를 어느정도 채워준 다음 Close를 나중에 해줘서 클로즈 한 영역이 실질적으로 사용하는 영역이 됨
// TLS를 통해 자신만의 Send 버퍼를 갖고 있게 할 것임
// 청크의 경우 왼쪽에서부터 쓰는데 한 번 쓰면 재사용하지 않고 선형적으로 사용함
SendBufferRef SendBufferManager::Open(uint32 size)
{
	if (LSendBufferChunk == nullptr)
	{
		LSendBufferChunk = Pop(); // WRITE_LOCK
		LSendBufferChunk->Reset();
	}

	ASSERT_CRASH(LSendBufferChunk->IsOpen() == false);

	// 다 썼으면 버리고 새거로 교체
	// 다 쓴거는 어떻게 되느냐? -> 이미 레퍼런스 카운팅이 걸려있는 상태
	// WSASend를 통해 Send가 끝나면 레퍼런스 카운팅이 어느순간 0이 됨 -> 다시 풀에 돌아옴
	if (LSendBufferChunk->FreeSize() < size)
	{
		LSendBufferChunk = Pop(); // WRITE_LOCK
		LSendBufferChunk->Reset();
	}

	return LSendBufferChunk->Open(size);
}

SendBufferChunkRef SendBufferManager::Pop()
{
	{
		WRITE_LOCK;
		if (_sendBufferChunks.empty() == false)
		{
			SendBufferChunkRef sendBufferChunk = _sendBufferChunks.back();
			_sendBufferChunks.pop_back();
			return sendBufferChunk;
		}
	}

	// 위에서 여유분이 없으면 새로 만들어야 함
	// SendBufferChunkRef는 레퍼런스 카운트가 0이 되면 소멸되는게 아니라 PushGlobal을 통해
	// 풀에 다시 넣음
	return SendBufferChunkRef(Xnew<SendBufferChunk>(), PushGlobal);
}

void SendBufferManager::Push(SendBufferChunkRef buffer)
{
	WRITE_LOCK;
	_sendBufferChunks.push_back(buffer);
}

void SendBufferManager::PushGlobal(SendBufferChunk* buffer)
{
	cout << "PushGlobal SENDBUFFERCHUNK" << endl;

	GSendBufferManager->Push(SendBufferChunkRef(buffer, PushGlobal));
}