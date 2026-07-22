#pragma once

/*---------------
	  Logger
----------------*/

enum class LogLevel : uint8
{
	Verbose,	// 상세 추적용, 평소엔 꺼둠
	Log,		// 일반 정보성 로그
	Warning,	// 문제 소지가 있지만 진행은 가능
	Error,		// 실패했지만 서버는 계속 동작
	Fatal,		// 복구 불가능한 치명적 오류
};

enum class LogOutput : uint8
{
	Console,
	File,
	Both,
};

class Logger
{
	enum : int32 { BUFFER_SIZE = 4096 };

public:
	Logger();
	~Logger();

public:
	// 서버 시작 시 한 번 호출해서 출력 방식/최소 레벨을 설정
	void	Init(LogOutput output, LogLevel minLevel = LogLevel::Log);
	void	Write(LogLevel level, const char* funcName, const WCHAR* format, ...);

private:
	void	WriteConsole(LogLevel level, const WCHAR* line);
	void	WriteFile(const WCHAR* line);
	void	OpenLogFile();

	static WORD			GetConsoleColor(LogLevel level);
	static const WCHAR*	GetLevelText(LogLevel level);

private:
	Mutex		_lock;
	LogOutput	_output = LogOutput::Console;
	LogLevel	_minLevel = LogLevel::Log;
	HANDLE		_stdOut = nullptr;
	FILE*		_file = nullptr;
};

/*---------------
	Log Macros
----------------*/

// __FUNCTION__은 MSVC에서 클래스 멤버 함수의 경우 "ClassName::MethodName" 형태로 확장됨
// Verbose/Info는 릴리즈 빌드에서 컴파일 자체가 빠짐 (오버헤드 없음)
#ifdef _DEBUG
#define LOG_VERBOSE(format, ...)	GLogger->Write(LogLevel::Verbose, __FUNCTION__, format, __VA_ARGS__)
#define LOG_INFO(format, ...)		GLogger->Write(LogLevel::Log, __FUNCTION__, format, __VA_ARGS__)
#else
#define LOG_VERBOSE(format, ...)	__noop
#define LOG_INFO(format, ...)		__noop
#endif

#define LOG_WARNING(format, ...)	GLogger->Write(LogLevel::Warning, __FUNCTION__, format, __VA_ARGS__)
#define LOG_ERROR(format, ...)		GLogger->Write(LogLevel::Error, __FUNCTION__, format, __VA_ARGS__)
#define LOG_FATAL(format, ...)		GLogger->Write(LogLevel::Fatal, __FUNCTION__, format, __VA_ARGS__)
