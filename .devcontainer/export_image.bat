@echo off
setlocal enabledelayedexpansion

set WORKSPACE_DIR=%~dp0..
pushd "%WORKSPACE_DIR%"
set CURRENT_DIR=%CD%
popd

set OUTPUT_FILE=%CURRENT_DIR%\vbox-builder_stm32c542.tar

where python >nul 2>&1
if %errorlevel% equ 0 (
    python "%~dp0docker_transfer.py" export vbox-builder:latest "%OUTPUT_FILE%"
    exit /b %errorlevel%
)

echo ==============================================================================
echo             Eksport obrazu Docker do trwalego archiwum
echo ==============================================================================
echo [INFO] Obraz zrodlowy : vbox-builder:latest
echo [INFO] Plik docelowy  : %OUTPUT_FILE%
echo.
echo [INFO] Trwa zapisywanie warstw obrazu (to moze potrwac ok. 30-60 sekund)...

docker save -o "%OUTPUT_FILE%" vbox-builder:latest

if %errorlevel% equ 0 (
    echo.
    echo ==============================================================================
    echo [SUKCES] Obraz zostal pomyslnie wyeksportowany!
    echo [PLIK]   %OUTPUT_FILE%
    echo ==============================================================================
    echo [WSKAZOWKA] Ten plik mozesz zarchiwizowac na dysku sieciowym, NAS lub chmurze.
    echo             Aby zaimportowac go na nowej maszynie w przyszlosci, uzyj:
    echo             .devcontainer\import_image.bat
    echo ==============================================================================
) else (
    echo.
    echo [BLAD] Eksport obrazu nie powiodl sie. Kod bledu: %errorlevel%
    exit /b %errorlevel%
)
