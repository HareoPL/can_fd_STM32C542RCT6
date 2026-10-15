@echo off
setlocal
title ST-Link Attach to Docker / WSL

net session >nul 2>&1
if %errorlevel% equ 0 goto :RUN_ADMIN

echo [INFO] Wymagane uprawnienia Administratora do dolaczenia ST-Linka.
echo [INFO] Uruchamianie okna Administratora...
powershell -NoProfile -Command "Start-Process cmd.exe -ArgumentList '/k \"%~f0\"' -Verb RunAs"
exit /b

:RUN_ADMIN
echo ================================================================
echo            ST-LINK ATTACH TO DOCKER / WSL
echo ================================================================
echo.

python "%~dp0stlink_manager.py" attach

echo.
echo ================================================================
echo [KROK] Aktualny status urzadzen w usbipd:
usbipd list | findstr /i /c:"ST-Link" /c:"BUSID"
echo ================================================================
echo.
pause
exit