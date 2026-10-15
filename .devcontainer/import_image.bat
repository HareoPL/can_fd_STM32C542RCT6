@echo off
setlocal enabledelayedexpansion

set WORKSPACE_DIR=%~dp0..
pushd "%WORKSPACE_DIR%"
set CURRENT_DIR=%CD%
popd

set INPUT_FILE=%1
if "%INPUT_FILE%"=="" (
    set INPUT_FILE=%CURRENT_DIR%\vbox-builder_stm32c542.tar
    if not exist "!INPUT_FILE!" (
        if exist "%CURRENT_DIR%\vbox-builder_stm32c542.tar.gz" (
            set INPUT_FILE=%CURRENT_DIR%\vbox-builder_stm32c542.tar.gz
        )
    )
)

if not exist "%INPUT_FILE%" (
    echo ==============================================================================
    echo [BLAD] Nie znaleziono pliku archiwum obrazu!
    echo ==============================================================================
    echo Szukano w sciezce: %INPUT_FILE%
    echo.
    echo Umiesc plik vbox-builder_stm32c542.tar w katalogu glownym projektu
    echo lub podaj sciezke do pliku jako parametr skryptu, np.:
    echo   .devcontainer\import_image.bat D:\kopia\vbox-builder_stm32c542.tar
    echo ==============================================================================
    exit /b 1
)

where python >nul 2>&1
if %errorlevel% equ 0 (
    python "%~dp0docker_transfer.py" import "%INPUT_FILE%"
    exit /b %errorlevel%
)

echo ==============================================================================
echo             Import obrazu Docker z trwalego archiwum
echo ==============================================================================
echo [INFO] Plik zrodlowy : %INPUT_FILE%
echo.
echo [INFO] Trwa ladowanie warstw obrazu do Dockera (to moze potrwac kilkanascie sekund)...

docker load -i "%INPUT_FILE%"

if %errorlevel% equ 0 (
    echo.
    echo ==============================================================================
    echo [SUKCES] Obraz vbox-builder zostal pomyslnie zaladowany do lokalnego Dockera!
    echo ==============================================================================
    echo [INFO] Mozesz teraz natychmiast kompilowac projekt zadaniem: [Docker] Build
    echo ==============================================================================
) else (
    echo.
    echo [BLAD] Import obrazu zakonczyl sie niepowodzeniem. Kod bledu: %errorlevel%
    exit /b %errorlevel%
)
