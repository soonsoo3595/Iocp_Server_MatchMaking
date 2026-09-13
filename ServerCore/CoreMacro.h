#pragma once

#define OUT

#define NAMESPACE_BEGIN(name)	namespace name {
#define NAMESPACE_END			}

/*---------------
	  Encoding
---------------*/

// wstring(UTF-16) -> UTF-8 std::string 변환.
// protobuf의 string 필드는 항상 UTF-8이어야 하는데 콘솔/Win32 API는 wstring을
// 쓰는 경우가 많아서, 그 사이를 메우는 변환을 매크로로 뺀다.
#define WSTR_TO_UTF8(wstr, outStr)												\
{																				\
	const int32 _wstrToUtf8Len = ::WideCharToMultiByte(CP_UTF8, 0, (wstr).c_str(),	\
		static_cast<int32>((wstr).size()), NULL, 0, NULL, NULL);					\
	(outStr).resize(_wstrToUtf8Len);											\
	::WideCharToMultiByte(CP_UTF8, 0, (wstr).c_str(),							\
		static_cast<int32>((wstr).size()), &(outStr)[0], _wstrToUtf8Len, NULL, NULL);	\
}

/*---------------
	  Lock
---------------*/

#define USE_MANY_LOCKS(count)	Lock _locks[count];
#define USE_LOCK				USE_MANY_LOCKS(1)
#define	READ_LOCK_IDX(idx)		ReadLockGuard readLockGuard_##idx(_locks[idx], typeid(this).name());
#define READ_LOCK				READ_LOCK_IDX(0)
#define	WRITE_LOCK_IDX(idx)		WriteLockGuard writeLockGuard_##idx(_locks[idx], typeid(this).name());
#define WRITE_LOCK				WRITE_LOCK_IDX(0)

/*---------------
	  Crash
---------------*/

// 인위적으로 CRASH를 내고 싶을 때 사용
#define CRASH(cause)						\
{											\
	uint32* crash = nullptr;				\
	__analysis_assume(crash != nullptr);	\
	*crash = 0xDEADBEEF;					\
}

// 조건부 크래시
#define ASSERT_CRASH(expr)			\
{									\
	if (!(expr))					\
	{								\
		CRASH("ASSERT_CRASH");		\
		__analysis_assume(expr);	\
	}								\
}