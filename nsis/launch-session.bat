@echo off
REM Log launch details for debugging
set LOGFILE=%TEMP%\zodiac-launch.log
echo ================================== >> "%LOGFILE%"
echo Launch Time: %DATE% %TIME% >> "%LOGFILE%"
echo Batch Dir: %~dp0 >> "%LOGFILE%"
echo Session File: %~1 >> "%LOGFILE%"
echo Current Dir Before CD: %CD% >> "%LOGFILE%"

cd /d "%~dp0"
echo Current Dir After CD: %CD% >> "%LOGFILE%"
echo Full Command: "%~dp0zodiac.exe" "--load-session" "%~1" >> "%LOGFILE%"
echo ================================== >> "%LOGFILE%"

start "" "%~dp0zodiac.exe" "--load-session" "%~1"
