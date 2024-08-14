title set IP address
@echo off
setlocal enabledelayedexpansion
color 71
chcp 65001 > nul

rem WIN10 system CMD obtains administrator privileges
%1 start "" mshta vbscript:CreateObject("Shell.Application").ShellExecute("cmd.exe","/c ""%~s0"" ::","","runas",1)(window.close)&&exit

set /p "NAME= Adapter name:"
set /p "IPin= The first three segments of the IP address:" 

for /L %%i in (1,1,254) do  (
	echo.%IPin%%%i%...
	netsh interface ip add address %NAME% %IPin%%%i% 255.255.255.0
)

echo.Add completed...

set /p "clear= clear ip:"
if "%clear%"=="clear" (
	goto clear IP
)
else (
	echo "not clear -> exit"
	choice /t 2 /d y /n >nul
	goto exit
)

:clear IP
cls
echo The set IP address will be cleared...
netsh interface ip set address %NAME% source=dhcp
choice /t 2 /d y /n >nul

:exit