#pragma once
#include "Types.h"

/*-----------------
	StringUtils
------------------*/

class StringUtils
{
public:
	// 문자열 앞뒤의 공백류(스페이스/탭/개행)를 제거한다.
	static string Trim(const string& str);

	// 닉네임 등으로 쓰기에 적합한 문자열인지 검사한다.
	// - Trim 이후 길이가 0이면 실패
	// - UTF-8 바이트 길이가 maxByteLen을 넘으면 실패
	// - 개행/탭 등 제어 문자가 섞여 있으면 실패
	// * name은 호출 전에 Trim된 상태라고 가정한다.
	static bool IsValidNickname(const string& name, size_t maxByteLen = 24);
};
