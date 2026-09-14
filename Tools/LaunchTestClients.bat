@echo off
REM 매칭 테스트용 : GameClient.exe 여러 개를 자동 매칭 옵션으로 한꺼번에 띄운다.
REM 사용법 : LaunchTestClients.bat [클라이언트 수(기본 10)] [Debug|Release(기본 Debug)]

set COUNT=%1
if "%COUNT%"=="" set COUNT=10

set CONFIG=%2
if "%CONFIG%"=="" set CONFIG=Debug

set EXE=%~dp0..\Binary\%CONFIG%\GameClient.exe

if not exist "%EXE%" (
    echo [오류] %EXE% 를 찾을 수 없습니다. 먼저 빌드하세요.
    exit /b 1
)

for /L %%i in (1,1,%COUNT%) do (
    start "GameClient_test%%i" "%EXE%" test%%i --auto-match
)
