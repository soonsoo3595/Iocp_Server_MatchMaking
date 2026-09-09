#include "pch.h"
#include "Logger.h"
#include <filesystem>

namespace fs = std::filesystem;

Logger::Logger()
{
	_stdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
}

Logger::~Logger()
{
	if (_file != nullptr)
		::fclose(_file);
}

void Logger::Init(LogOutput output, LogLevel minLevel)
{
	LockGuard lockGuard(_lock);

	_output = output;
	_minLevel = minLevel;

	if (_output == LogOutput::File || _output == LogOutput::Both)
		OpenLogFile();
}

void Logger::OpenLogFile()
{
	if (_file != nullptr)
		return;

	fs::create_directories(L"Logs");

	SYSTEMTIME st;
	::GetLocalTime(&st);

	WCHAR path[MAX_PATH];
	::swprintf_s(path, MAX_PATH, L"Logs/Server_%04d%02d%02d_%02d%02d%02d.log",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	// ccs=UTF-8 : WCHAR를 그대로 UTF-8로 변환해서 파일에 씀
	::_wfopen_s(&_file, path, L"a, ccs=UTF-8");
}

void Logger::Write(LogLevel level, const char* funcName, const WCHAR* format, ...)
{
	if (format == nullptr)
		return;

	if (level < _minLevel)
		return;

	// BUFFER_SIZE(4096) WCHAR 버퍼 2개를 동시에 두면 스택프레임이 너무 커져서(C6262)
	// 스레드별로 한 번만 할당되는 정적 버퍼(TLS)로 옮김
	static thread_local WCHAR message[BUFFER_SIZE];
	static thread_local WCHAR line[BUFFER_SIZE];

	va_list ap;
	va_start(ap, format);
	::vswprintf_s(message, BUFFER_SIZE, format, ap);
	va_end(ap);

	SYSTEMTIME st;
	::GetLocalTime(&st);

	// %hs : narrow(char*) 문자열을 wide로 변환해서 출력 (MSVC CRT 확장)
	::swprintf_s(line, BUFFER_SIZE, L"[%02d:%02d:%02d.%03d][%s][%hs] %s\n",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetLevelText(level), funcName, message);

	LockGuard lockGuard(_lock);

	if (_output == LogOutput::Console || _output == LogOutput::Both)
		WriteConsole(level, line);

	if (_output == LogOutput::File || _output == LogOutput::Both)
		WriteFile(line);
}

void Logger::WriteConsole(LogLevel level, const WCHAR* line)
{
	::SetConsoleTextAttribute(_stdOut, GetConsoleColor(level));

	// fputws(stdout)는 CRT 로케일 기준으로 wide->narrow 변환을 하는데, 기본 "C" 로케일에서는
	// 한글처럼 변환 안 되는 문자를 만나면 그 지점에서 조용히 멈춰버려서 뒷부분이 통째로 안 찍힌다.
	// WriteConsoleW는 로케일을 안 거치고 UTF-16을 콘솔에 직접 넘기므로 이 문제가 없다.
	DWORD written = 0;
	::WriteConsoleW(_stdOut, line, static_cast<DWORD>(::wcslen(line)), &written, nullptr);

	::SetConsoleTextAttribute(_stdOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void Logger::WriteFile(const WCHAR* line)
{
	if (_file == nullptr)
		return;

	::fputws(line, _file);
	::fflush(_file);
}

WORD Logger::GetConsoleColor(LogLevel level)
{
	switch (level)
	{
	case LogLevel::Verbose:	return FOREGROUND_BLUE | FOREGROUND_INTENSITY;
	case LogLevel::Log:		return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	case LogLevel::Warning:	return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	case LogLevel::Error:	return FOREGROUND_RED | FOREGROUND_INTENSITY;
	case LogLevel::Fatal:	return FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_INTENSITY;
	default:				return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	}
}

const WCHAR* Logger::GetLevelText(LogLevel level)
{
	switch (level)
	{
	case LogLevel::Verbose:	return L"Verbose";
	case LogLevel::Log:		return L"Log";
	case LogLevel::Warning:	return L"Warning";
	case LogLevel::Error:	return L"Error";
	case LogLevel::Fatal:	return L"Fatal";
	default:				return L"Unknown";
	}
}
