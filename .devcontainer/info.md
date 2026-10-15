# Środowisko Kontenera Deweloperskiego (Dev Container) - STM32C542 (V-Box)

Niniejszy folder zawiera kompletną konfigurację kontenera deweloperskiego (Dev Container) dla projektu **v_box** na mikrokontrolerze **STM32C542RCT6** (płytka **NUCLEO-C542RC**), w pełni odwzorowującą środowisko lokalne Visual Studio Code.

---

## 1. Specyfikacja Środowiska

| Komponent | Wersja / Szczegóły | Uwagi |
| :--- | :--- | :--- |
| **System bazowy** | Ubuntu 24.04 LTS | Obraz `mcr.microsoft.com/devcontainers/base:ubuntu-24.04` |
| **Mikrokontroler** | STM32C542RCT6 | ARM Cortex-M33 @ 144 MHz, DSP, SP_FPU |
| **Kompilator (Toolchain)** | ARM GNU Toolchain 14.2 / ST GCC 14.3.1 | `arm-none-eabi-gcc` w `/usr/local/bin` |
| **System budowania** | CMake >= 3.31 + Ninja | Spełnia wymóg `cmake_minimum_required(VERSION 3.30)` |
| **Język / Intellisense** | `clangd` + `compile_commands.json` | Ścieżka: `build/debug_GCC_NUCLEO-C542RC/` |
| **Programator / Debugger** | ST-LINK V3 (na płytce Nucleo-C542RC) | Obsługa przez USB pass-through (`usbipd-win`) |

---

## 2. Podłączanie ST-Link V3 do Kontenera (Windows -> WSL/Docker)

Programator ST-Link na płytce Nucleo jest urządzeniem USB podłączonym fizycznie do systemu Windows. Aby udostępnić go wewnątrz kontenera:

1. **Wymagania na hoście Windows:**
   - Zainstalowane narzędzie `usbipd-win` (np. przez `winget install dorssel.usbipd-win`).
   - Uruchomiony Docker Desktop (z silnikiem WSL 2).

2. **Dołączenie ST-Linka do kontenera:**
   - Kliknij dwukrotnie plik `.devcontainer/stlink_attach.bat` (lub uruchom w terminalu Windows z uprawnieniami Admina: `python .devcontainer/stlink_manager.py attach`).
   - Skrypt automatycznie wykryje urządzenie ST-Link po identyfikatorze `VID:PID 0483:374x / 0483:375x` i przekaże je do kontenera z flagą `--force`.

3. **Weryfikacja wewnątrz kontenera:**
   W terminalu wewnątrz kontenera wykonaj:
   ```bash
   lsusb
   ```
   Powinien pojawić się wpis:
   `Bus 001 Device 002: ID 0483:374e STMicroelectronics STLINK-V3` (lub o zbliżonym PID).

4. **Odłączenie ST-Linka (zwrot do Windowsa):**
   - Uruchom plik `.devcontainer/stlink_detach.bat` (lub `python .devcontainer/stlink_manager.py detach`).

---

## 3. Kompilacja Kodu

### Z poziomu interfejsu VS Code:
- Skrót `Ctrl+Shift+B` uruchamia domyślne zadanie **Build** zdefiniowane w `.vscode/tasks.json`.
- Wybierz preset CMake: `debug_GCC_NUCLEO-C542RC`.

### Z poziomu wiersza poleceń (w kontenerze):
```bash
# 1. Konfiguracja CMake z presetem dla Nucleo-C542RC
cmake --preset debug_GCC_NUCLEO-C542RC

# 2. Kompilacja projektu za pomocą Ninja
ninja -C build/debug_GCC_NUCLEO-C542RC
```
Plik wynikowy `.elf` powstanie w:
`build/debug_GCC_NUCLEO-C542RC/v_box.elf`

### Szybka kompilacja w Dockerze z poziomu hosta Windows (bez otwierania kontenera):
Możesz skompilować projekt w Dockerze bezpośrednio ze zwykłego okna VS Code lub z terminala Windows:
1. **Z poziomu VS Code Tasks:**
   - Wybierz: `Terminal` -> `Run Task...` -> **`Docker: Build Project`**
   - Lub z jednoczesnym wgraniem do mikrokontrolera: **`Docker: Build & Flash`**
2. **Z poziomu terminala Windows (PowerShell/CMD):**
   ```cmd
   .devcontainer\docker_build.bat
   ```
   Lub bezpośrednim poleceniem Dockera:
   ```bash
   docker run --rm -v "%cd%:/workspaces/5_v-box" -w /workspaces/5_v-box vbox-c5-test sh -c "cmake -B build_docker -G Ninja -DCMAKE_BUILD_TYPE=debug_GCC_NUCLEO-C542RC -DTOOLCHAIN_FILE=GCC.14.3.1.cmake && ninja -C build_docker"
   ```
Plik wynikowy zostanie skopiowany do `build/debug_GCC_NUCLEO-C542RC/v_box.elf`.

---

## 4. Programowanie i Debugowanie (ST-LINK V3)

### Opcja A: Oficjalna wtyczka STM32 VS Code Extension (Zalecana)
W menu **Run and Debug** (`Ctrl+Shift+D`) uruchom profil:
- **`STM32Cube: Launch ST-Link GDB Server`**
Profil ten automatycznie buduje projekt i uruchamia zintegrowany serwer GDB ST-Link.

### Opcja B: Programowanie z wiersza poleceń
Za pomocą `stlink-tools`:
```bash
# Konwersja ELF na binarny
arm-none-eabi-objcopy -O binary build/debug_GCC_NUCLEO-C542RC/v_box.elf build/debug_GCC_NUCLEO-C542RC/v_box.bin

# Zapis do pamięci Flash pod adres bazowy 0x08000000
st-flash write build/debug_GCC_NUCLEO-C542RC/v_box.bin 0x08000000
```

Lub z poziomu hosta Windows za pomocą `STM32_Programmer_CLI` (zadanie VS Code: `STM32: Flash Device`).

### Opcja C: Połączenie OpenOCD (Cortex-M33 SWD - zweryfikowane na Nucleo-C542RC)
Dla mikrokontrolera STM32C542 (rdzeń Cortex-M33, identyfikator DAP `0x6ba02477`) przez interfejs ST-Link HLA-SWD:
```bash
openocd -f interface/stlink.cfg \
  -c "transport select hla_swd" \
  -c "set CPUTAPID 0x6ba02477" \
  -f target/stm32u5x.cfg
```
Polecenie uruchamia serwer GDB na porcie `3333` oraz serwer telnet na porcie `4444`.


---

## 5. Synchronizacja Ustawień VS Code

Kontener deweloperski ma wstrzyknięte 1:1 ustawienia z Twojego środowiska hosta:
- Pełen pakiet wtyczek STM32Cube dla VS Code.
- Generator nagłówków include guard: `C/C++ Include Guard` (format makra na bazie nazwy pliku, styl blokowy).
- Generator nagłówków Doxygen: `koroFileHeader` (`@file`, `@brief`, `@author`, `@date`).
- Narzędzia inspekcji pamięci i portów szeregowych: `eclipse-cdt.memory-inspector`, `eclipse-cdt.serial-monitor`.
- Wyłączenie silnika `C_Cpp.intelliSenseEngine` na rzecz natywnego, szybkiego `clangd`.

---

## 6. Długoterminowa Archiwizacja Obrazu Dockera (Export / Import)

Aby zabezpieczyć projekt przed zmianami w repozytoriach zewnętrznych w perspektywie 4–10 lat, obraz `vbox-builder:latest` zawiera pełny, samowystarczalny stos narzędzi:
- **Kompilacja i sprzęt:** GCC 14.2 ARM, CMake 3.31.5, Ninja, OpenOCD, stlink-tools, udev rules.
- **Jakość kodu i dokumentacja:** `cppcheck` (analiza statyczna / MISRA), `doxygen` + `graphviz`, `srecord` (`srec_cat`), `lcov`.
- **Magistrala CAN & Diagnostyka:** `can-utils` (SocketCAN), `python-can`, `gs-usb` (CANable/Candlelight), `cantools` (bazy DBC).
- **Aplikacje okienkowe GUI:** `PyQt6` / `PyQt5`, `pyqtgraph` (wykresy na żywo), akceleracja OpenGL (natywne okna na pulpicie Windows przez WSLg).
- **Analiza danych:** `pandas`, `numpy`, `openpyxl` (zapis/odczyt Excel .xlsx).
- **Bezpieczny rozruch:** `imgtool` (MCUboot/TF-M), `cryptography` / OpenSSL.

### Eksport obrazu do archiwum (.tar):
W VS Code wybierz zadanie: **`[Docker] Export Image to Archive`**
(lub uruchom skrypt `.devcontainer/export_image.bat`).
Utworzony plik `vbox-builder_stm32c542.tar` można zapisać na dysku sieciowym, NAS lub w archiwum projektu.

### Odtwarzanie obrazu na nowym komputerze (bez internetu):
W VS Code wybierz zadanie: **`[Docker] Import Image from Archive`**
(lub uruchom skrypt `.devcontainer/import_image.bat`).
Po załadowaniu obraz jest od razu gotowy do pracy poleceniem `[Docker] Build`.

