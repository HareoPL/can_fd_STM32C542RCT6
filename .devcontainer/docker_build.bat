@echo off
setlocal enabledelayedexpansion

set WORKSPACE_DIR=%~dp0..
pushd "%WORKSPACE_DIR%"
set CURRENT_DIR=%CD%
popd

:: Upewnij sie, ze obraz vbox-builder istnieje; jesli nie, zbuduj go automatycznie
docker image inspect vbox-builder >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO] Budowanie obrazu kontenera vbox-builder...
    docker build -t vbox-builder -f "%~dp0Dockerfile" "%~dp0"
    if %errorlevel% neq 0 (
        echo [BLAD] Nie udalo sie zbudowac obrazu Dockera.
        exit /b %errorlevel%
    )
)

:: Obsluga flagi clean
if /i "%1"=="clean" (
    echo [INFO] Czyszczenie katalogu kompilacji build/docker...
    docker run --rm -v "%CURRENT_DIR%:/workspaces/5_v-box" -w /workspaces/5_v-box vbox-builder sh -c "rm -rf build/docker"
    echo [SUKCES] Wyczyszczono katalog build/docker.
    exit /b 0
)

:: Obsluga flagi rebuild (czyszczenie przed kompilacja)
if /i "%1"=="rebuild" (
    echo [INFO] Rebuild: Czyszczenie katalogu build/docker przed pelna kompilacja...
    docker run --rm -v "%CURRENT_DIR%:/workspaces/5_v-box" -w /workspaces/5_v-box vbox-builder sh -c "rm -rf build/docker"
)

echo [INFO] Rozpoczynanie kompilacji projektu w kontenerze Docker...

docker run --rm -v "%CURRENT_DIR%:/workspaces/5_v-box" -w /workspaces/5_v-box vbox-builder sh -c "cmake -B build/docker -G Ninja -DCMAKE_BUILD_TYPE=debug_GCC_NUCLEO-C542RC -DTOOLCHAIN_FILE=GCC.14.3.1.cmake && ninja -C build/docker && mkdir -p build/debug_GCC_NUCLEO-C542RC && cp build/docker/v_box.elf build/debug_GCC_NUCLEO-C542RC/v_box.elf"

if %errorlevel% equ 0 (
    echo.
    echo [SUKCES] Kompilacja w Dockerze zakonczona pomyslnie!
    echo [INFO] Gotowy plik binarny: build\debug_GCC_NUCLEO-C542RC\v_box.elf
) else (
    echo.
    echo [BLAD] Kompilacja w Dockerze zakonczona niepowodzeniem.
    exit /b %errorlevel%
)
