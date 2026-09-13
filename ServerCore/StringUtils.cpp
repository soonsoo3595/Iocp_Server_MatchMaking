#include "pch.h"
#include "StringUtils.h"

/*-----------------
	StringUtils
------------------*/

string StringUtils::Trim(const string& str)
{
	static const char* whitespace = " \t\r\n";

	const size_t begin = str.find_first_not_of(whitespace);
	if (begin == string::npos)
		return string();

	const size_t end = str.find_last_not_of(whitespace);
	return str.substr(begin, end - begin + 1);
}

bool StringUtils::IsValidNickname(const string& name, size_t maxByteLen)
{
	if (name.empty())
		return false;

	if (name.size() > maxByteLen)
		return false;

	for (const char ch : name)
	{
		// ASCII 제어 문자(개행, 탭 등)가 섞여 있으면 안 됨.
		// UTF-8 멀티바이트 문자의 후속 바이트는 0x80 이상이라 여기 걸리지 않는다.
		if (static_cast<unsigned char>(ch) < 0x20)
			return false;
	}

	return true;
}

String StringUtils::Utf8ToWide(const string& str)
{
	const int32 srcLen = static_cast<int32>(str.size());

	String ret;
	if (srcLen == 0)
		return ret;

	const int32 retLen = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), srcLen, NULL, 0);
	ret.resize(retLen);
	::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), srcLen, &ret[0], retLen);

	return ret;
}
