#pragma once

/*--------------
	RecvBuffer
----------------*/

// [][][][][][][][][][]
// read, write가 겹치면 데이터가 없다는 얘기 -> 다시 0번으로 위치
// write가 끝에 위치하면 read를 0번으로 이동하고 write도 다시 오프셋만큼 이동
class RecvBuffer
{
	enum { BUFFER_COUNT = 10 };

public:
	RecvBuffer(int32 bufferSize);
	~RecvBuffer();

	void			Clean();
	bool			OnRead(int32 numOfBytes);
	bool			OnWrite(int32 numOfBytes);

	BYTE*			ReadPos() { return &_buffer[_readPos]; }
	BYTE*			WritePos() { return &_buffer[_writePos]; }
	int32			DataSize() { return _writePos - _readPos; }
	int32			FreeSize() { return _capacity - _writePos; }

private:
	int32			_capacity = 0;
	int32			_bufferSize = 0;
	int32			_readPos = 0;			// 현재 읽는 위치
	int32			_writePos = 0;			// 현재 쓰는 위치
	Vector<BYTE>	_buffer;
};

