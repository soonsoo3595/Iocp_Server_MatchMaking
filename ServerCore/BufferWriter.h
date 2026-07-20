#pragma once

/*----------------
	BufferWriter
-----------------*/

class BufferWriter
{
public:
	BufferWriter();
	BufferWriter(BYTE* buffer, uint32 size, uint32 pos = 0);
	~BufferWriter();

	BYTE*			Buffer() { return _buffer; }
	uint32			Size() { return _size; }
	uint32			WriteSize() { return _pos; }
	uint32			FreeSize() { return _size - _pos; }

	template<typename T>
	bool			Write(T* src) { return Write(src, sizeof(T)); }
	bool			Write(void* src, uint32 len);

	template<typename T>
	T*				Reserve(uint16 count = 1);

	// template<typename T>
	// BufferWriter& operator<<(const T& src);

	// 템플릿이 붙은 순간 보편 참조가 된다 -> 모든 오버로딩이 여기로 왔음
	template<typename T>
	BufferWriter& operator<<(T&& src);

private:
	BYTE*			_buffer = nullptr;
	uint32			_size = 0;
	uint32			_pos = 0;
};

// 예약하고 있는 위치를 리턴
template<typename T>
T* BufferWriter::Reserve(uint16 count)
{
	if (FreeSize() < sizeof(T) * count)
		return nullptr;

	T* ret = reinterpret_cast<T*>(&_buffer[_pos]);
	_pos += (sizeof(T) * count);
	return ret;
}

template<typename T>
BufferWriter& BufferWriter::operator<<(T&& src)
{
	using DataType = std::remove_reference_t<T>;	// 참조값을 빼겠다
	*reinterpret_cast<DataType*>(&_buffer[_pos]) = std::forward<DataType>(src);
	_pos += sizeof(T);
	return *this;
}