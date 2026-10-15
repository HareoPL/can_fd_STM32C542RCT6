@echo off
setlocal
title ST-Link Detach / Restore to Windows

net session >nul 2>&1
if %errorlevel% equ 0 goto :RUN_ADMIN

echo [INFO] Wymagane uprawnienia Administratora do przywrocenia sterownikow Windows.
echo [INFO] Uruchamianie okna Administratora...
powershell -NoProfile -Command "Start-Process cmd.exe -ArgumentList '/k \"%~f0\"' -Verb RunAs"
exit /b

:RUN_ADMIN
echo ================================================================
echo       ST-LINK DETACH AND RESTORE TO WINDOWS
echo ================================================================
echo.

echo [KROK 1] Odpinanie urzadzenia z WSL/Docker...
usbipd detach --all 2>nul

echo.
echo [KROK 2] Przywracanie natywnych sterownikow ST-Link w Windows (unbind)...
usbipd unbind --all

echo.
echo [KROK 3] Aktualny status urzadzen w usbipd:
usbipd list

echo.
echo ================================================================
echo [GOTOWE] ST-Link powrocil do Menedzera Urzadzen Windows.
echo ================================================================
echo.
pause
exit