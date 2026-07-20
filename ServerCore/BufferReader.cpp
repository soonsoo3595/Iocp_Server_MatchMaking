#include "pch.h"
#include "BufferReader.h"

/*----------------
	BufferReader
-----------------*/

BufferReader::BufferReader()
{
}

BufferReader::BufferReader(BYTE* buffer, uint32 size, uint32 pos)
	: _buffer(buffer), _size(size), _pos(pos)
{

}

BufferReader::~BufferReader()
{

}

// 데이터를 보고 싶은데 커서를 조작하고 싶지는 않음
bool BufferReader::Peek(void* dest, uint32 len)
{
	if (FreeSize() < len)
		return false;

	::memcpy(dest, &_buffer[_pos], len);
	return true;
}

// 커서를 조작
bool BufferReader::Read(void* dest, uint32 len)
{
	if (Peek(dest, len) == false)
		return false;

	_pos += len;
	return true;
}
