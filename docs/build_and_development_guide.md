# Przewodnik Budowania i Rozwoju Projektu (Build & Development Guide)

Niniejszy dokument stanowi kompletny podręcznik inżynierski opisujący procedury budowania, programowania mikrokontrolera, pracy z kontenerem Docker, debugowania oraz długoterminowej archiwizacji oprogramowania układowego **v_box** dla mikrokontrolera **STM32C542RCT6** (płytka **NUCLEO-C542RC**).

---

## Spis Treści
1. [Architektura Środowiska Budowania](#1-architektura-środowiska-budowania)
2. [Wymagania Wstępne (Prerequisites)](#2-wymagania-wstępne-prerequisites)
3. [Tabela Zadań VS Code (Tasks Reference)](#3-tabela-zadań-vs-code-tasks-reference)
4. [Budowanie Lokalne w Środowisku Windows](#4-budowanie-lokalne-w-środowisku-windows)
5. [Budowanie w Kontenerze Docker](#5-budowanie-w-kontenerze-docker)
6. [Programowanie Mikrokontrolera i Debugowanie (Flashing & Debugging)](#6-programowanie-mikrokontrolera-i-debugowanie-flashing--debugging)
7. [Automatyczny Raport Pamięci FLASH i RAM](#7-automatyczny-raport-pamięci-flash-i-ram)
8. [Długoterminowa Archiwizacja Środowiska (Reproducible Builds)](#8-długoterminowa-archiwizacja-środowiska-reproducible-builds)
9. [Zaawansowany Ekosystem Narzędziowy w Dockerze (QA, CAN, GUI)](#9-zaawansowany-ekosystem-narzędziowy-w-dockerze-qa-can-gui)
10. [Rozwiązywanie Problemów (Troubleshooting & FAQ)](#10-rozwiązywanie-problemów-troubleshooting--faq)

---

## 1. Architektura Środowiska Budowania

W projekcie wdrożono **symetryczną, dwuścieżkową architekturę kompilacji**:
1. **Ścieżka Windows Native:** Bezpośrednia kompilacja za pomocą narzędzi zainstalowanych na maszynie gospodarza (STM32CubeCLT, CMake, Ninja).
2. **Ścieżka Docker Container:** Kompilacja w odizolowanym kontenerze Linuksa (Ubuntu 24.04 LTS), gwarantująca 100% powtarzalność niezależnie od konfiguracji systemu operacyjnego.

```text
               +-------------------------------------------------------+
               |                  KOD ŹRÓDŁOWY (c:\WORK\5_v-box)       |
               +---------------------------+---------------------------+
                                           |
                    +----------------------+----------------------+
                    |                                             |
                    v                                             v
       [Środowisko Lokalne Windows]                   [Kontener Docker Linux]
        CMake + Ninja + ST GCC 14.3                   CMake + Ninja + ARM GCC 14.2
                    |                                             |
                    v                                             v
     build/debug_GCC_NUCLEO-C542RC/               build/docker/
             (CMake Cache Win)                          (CMake Cache Linux)
                    |                                             |
                    |                              (Kopiowanie pliku .elf)
                    |                                             |
                    +----------------------+----------------------+
                                           |
                                           v
                        Ujednolicony plik binarny:
                 build/debug_GCC_NUCLEO-C542RC/v_box.elf
                                           |
                                           v
                       Wspólne zadanie programowania:
                     "Flash Device" (STM32_Programmer_CLI)
```

### Kluczowe zasady architektury:
* **Izolacja pamięci podręcznej CMake:** Pliki `CMakeCache.txt` zawierają bezwzględne ścieżki systemowe. Z tego względu Windows operuje w `build/debug_GCC_NUCLEO-C542RC/`, a Docker w `build/docker/`. Zapobiega to kolizjom i uszkodzeniom drzewa kompilacji.
* **Ujednolicony plik docelowy:** Niezależnie od tego, czy kompilujesz w Windowsie czy w Dockerze, gotowy plik wykonywalny zawsze ląduje w:
  `build/debug_GCC_NUCLEO-C542RC/v_box.elf`
* **Jeden uniwersalny Flasher:** Zadanie `Flash Device` odwołuje się do jednego stałego pliku `.elf`, dzięki czemu procedury wgrywania (`Build & Flash`) działają identycznie dla obu ścieżek.

---

## 2. Wymagania Wstępne (Prerequisites)

### A. Dla budowania w Windows:
* **Visual Studio Code** z zestawem rozszerzeń:
  * **Wtyczki niezbędne:**
    * `stmicroelectronics.stm32-vscode-extension` (STM32 VS Code Extension) – oficjalne środowisko ST, zarządzanie toolchainem, serwer ST-Link GDB, widok rejestrów mikrokontrolera i wątków FreeRTOS.
    * `ms-vscode.cmake-tools` (CMake Tools) – integracja z CMake, parsowanie `CMakePresets.json`, obsługa zadań kompilacji w VS Code.
    * `llvm-vs-code-extensions.vscode-clangd` (clangd) – szybki silnik IntelliSense, nawigacja po kodzie C/C++ na bazie bazy `compile_commands.json` (w konfiguracji projektu domyślny `C_Cpp.intelliSenseEngine` jest celowo wyłączony).
  * **Wtyczki zalecane (produktywność i QA):**
    * `akiramiyakoda.cppincludeguard` (C/C++ Include Guard) – automatyczne wstawianie i aktualizacja include guardów nagłówków `.h`.
    * `obkoro1.korofileheader` (koroFileHeader) – automatyczne generowanie szablonów dokumentacji Doxygen (`@file`, `@brief`).
    * `eclipse-cdt.serial-monitor` (Serial Monitor) – zintegrowany terminal portu szeregowego do odczytu logów `safe_printf` (115200 baud).
    * `eclipse-cdt.memory-inspector` (Memory Inspector) – podgląd pamięci RAM/Flash na żywo podczas sesji debugera.
  *(Po otwarciu projektu VS Code automatycznie zaproponuje instalację tego zestawu na podstawie `.vscode/extensions.json`.)*
* **Narzędzia sprzętowe i kompilator:**
  * ARM GNU Toolchain (`arm-none-eabi-gcc` 14.x)
  * CMake $\ge$ 3.30 (np. Kitware CMake 3.31+)
  * Ninja Build
  * `STM32_Programmer_CLI` (z pakietu STM32CubeCLT lub STM32CubeProgrammer, dodany do zmiennej środowiskowej PATH)

### B. Dla budowania w Dockerze:
* **Docker Desktop** (wersja dla Windows z silnikiem WSL 2) - do pobrania z oficjalnej strony [Docker Desktop](https://www.docker.com/products/docker-desktop/).
* **Gotowy obraz środowiska:** Pobierz archiwum `vbox-builder_stm32c542.tar` z firmowego dysku: [v-box-tester SharePoint Archive](https://lincorspzoo-my.sharepoint.com/:f:/g/personal/j_lukaszewicz_lincor_pl/IgBjV5wdDW81QqDHNNTQZr4GARtXaX-XTG5QeiUvk5Fbme8?e=RkfN9G) i zaimportuj skryptem `.devcontainer\import_image.bat` (lub zadaniem `[Docker] Import Image from Archive`).
* **usbipd-win** (opcjonalnie, tylko jeśli chcesz przekazywać fizyczny programator ST-Link do wnętrza kontenera):
  ```powershell
  winget install dorssel.usbipd-win
  ```

---

## 3. Tabela Zadań VS Code (Tasks Reference)

Wszystkie operacje projektowe są zintegrowane w menu Visual Studio Code: **Terminal** $\rightarrow$ **Run Task...** (lub skrót `Ctrl+Shift+B` dla domyślnego buildu):

| Etykieta Zadania (Task Label) | Środowisko | Typ | Opis działania |
| :--- | :---: | :---: | :--- |
| **`[Windows] Build`** *(Domyślny `Ctrl+Shift+B`)* | Windows | CMake | Kompilacja przyrostowa w Windowsie. Automatycznie uruchamia `[Windows] Configure`, jeśli brak katalogu `build/`. |
| **`[Windows] Rebuild`** | Windows | Kompozycja | Pełna rekompilacja od zera (czyści katalog obiektów i kompiluje wszystkie jednostki C). |
| **`[Windows] Clean`** | Windows | CMake | Czyszczenie plików obiektowych w `build/debug_GCC_NUCLEO-C542RC/`. |
| **`[Windows] Configure`** | Windows | CMake | Generowanie plików Ninja na podstawie `CMakeLists.txt` i presetu `debug_GCC_NUCLEO-C542RC`. |
| **`[Windows] Build & Flash`** | Windows | Kompozycja | Kompilacja w Windowsie, a następnie automatyczne wgranie wsadu do mikrokontrolera. |
| **`[Docker] Build`** | Docker | Proces | Kompilacja w kontenerze Linux bez konieczności otwierania okna devcontainera. Kopiuje wynik do docelowego folderu. |
| **`[Docker] Rebuild`** | Docker | Proces | Usunięcie folderu `build/docker/` i pełna rekompilacja w kontenerze. |
| **`[Docker] Build & Flash`** | Docker | Kompozycja | Kompilacja w kontenerze Docker, a następnie automatyczne wgranie wsadu z poziomu Windowsa. |
| **`Flash Device`** | Host Win | Proces | Bezpośrednie wgranie `build/debug_GCC_NUCLEO-C542RC/v_box.elf` do STM32 przez ST-Link SWD (Under Reset). |
| **`Clean All`** | Host Win | Proces | Całkowite usunięcie całego katalogu `build/` (zarówno wersji Windows, jak i Docker). |
| **`[USB] Attach ST-Link to Docker`** | Host Win | Proces | Wykrywa programator ST-Link po masce VID:PID i przekazuje go do WSL/Dockera z flagą `--force`. |
| **`[USB] Detach ST-Link from Docker`**| Host Win | Proces | Odłącza programator od WSL i natychmiast przywraca go do Menedżera Urządzeń Windows (`unbind`). |
| **`[Docker] Export Image to Archive`** | Docker | Proces | Zapisuje cały obraz `vbox-builder` do pliku `vbox-builder_stm32c542.tar` z dynamicznym wskaźnikiem postępu w % i prędkością MB/s. |
| **`[Docker] Import Image from Archive`** | Docker | Proces | Przywraca obraz Dockera z archiwum z dynamicznym wskaźnikiem postępu w % i prędkością MB/s. |

---

## 4. Budowanie Lokalne w Środowisku Windows

### Metoda 1: Poprzez interfejs VS Code
1. Naciśnij `Ctrl+Shift+B` (uruchomi zadanie `[Windows] Build`).
2. Jeśli katalog `build/` został wcześniej skasowany, zadanie automatycznie wywoła `[Windows] Configure`, wygeneruje pliki `build.ninja` i przeprowadzi kompilację.
3. Na końcu kompilacji w terminalu pojawi się automatyczny raport pamięci FLASH i RAM.

### Metoda 2: Z poziomu wiersza poleceń (PowerShell / CMD)
```powershell
# 1. Konfiguracja presetu
cmake --preset debug_GCC_NUCLEO-C542RC

# 2. Kompilacja za pomocą Ninja
ninja -C build/debug_GCC_NUCLEO-C542RC
```

---

## 5. Budowanie w Kontenerze Docker

Budowanie w Dockerze nie wymaga otwierania nowego okna VS Code w trybie „Dev Containers”. Całość odbywa się w tle z zachowaniem pełnej wygody pracy w Windowsie.

### Krok po kroku: Pierwsze uruchomienie (Quick Start)
1. **Instalacja silnika Docker:** Zainstaluj [Docker Desktop](https://www.docker.com/products/docker-desktop/) w systemie Windows (z włączonym WSL 2 backend).
2. **Pobranie obrazu:** Ściągnij gotowe archiwum `vbox-builder_stm32c542.tar` z dysku firmowego:  
   👉 **[v-box-tester (SharePoint Archive)](https://lincorspzoo-my.sharepoint.com/:f:/g/personal/j_lukaszewicz_lincor_pl/IgBjV5wdDW81QqDHNNTQZr4GARtXaX-XTG5QeiUvk5Fbme8?e=RkfN9G)**  
   i umieść plik w katalogu głównym projektu (`c:\WORK\5_v-box\`).
3. **Import obrazu do Dockera:** Uruchom skrypt wsadowy `.devcontainer\import_image.bat` (lub zadanie VS Code: `[Docker] Import Image from Archive`). W kilkanaście sekund obraz `vbox-builder:latest` zostanie zarejestrowany lokalnie.
4. **Kompilacja firmware:** Uruchom zadanie VS Code **`[Docker] Build`** (lub skrypt `.devcontainer\docker_build.bat`).

### Metoda 1: Poprzez zadania VS Code
* Wybierz: `Terminal` $\rightarrow$ `Run Task...` $\rightarrow$ **`[Docker] Build`**
* Lub pełną rekompilację: `Terminal` $\rightarrow$ `Run Task...` $\rightarrow$ **`[Docker] Rebuild`**
* Aby zbudować i od razu wgrać do STM32: **`[Docker] Build & Flash`**

### Metoda 2: Ze skryptu wsadowego Windows
W terminalu głównym projektu uruchom:
```cmd
# Standardowa kompilacja przyrostowa
.devcontainer\docker_build.bat

# Pełny rebuild (czyści build/docker przed startem)
.devcontainer\docker_build.bat rebuild

# Samo czyszczenie katalogu dockera
.devcontainer\docker_build.bat clean
```

### Jak działa skrypt `docker_build.bat`:
1. Sprawdza, czy lokalny obraz `vbox-builder` istnieje. Jeśli nie – buduje go automatycznie z pliku `Dockerfile`.
2. Uruchamia jednorazowy kontener montujący katalog projektu w `/workspaces/5_v-box`.
3. Wykonuje kompilację CMake/Ninja do katalogu `build/docker/`.
4. Kopiuje gotowy plik wykonywalny do `build/debug_GCC_NUCLEO-C542RC/v_box.elf`.
5. Uruchamia analizator pamięci [tools/print_memory.py](file:///c:/WORK/5_v-box/tools/print_memory.py) i wyświetla czytelne podsumowanie.

---

## 6. Programowanie Mikrokontrolera i Debugowanie (Flashing & Debugging)

### Programowanie (Flashing)
Zadanie **`Flash Device`** korzysta z oficjalnego narzędzia `STM32_Programmer_CLI`:
* **Port:** SWD
* **Tryb (Mode):** `UR` (*Under Reset*) – umożliwia zaprogramowanie układu nawet wtedy, gdy procesor znajduje się w trybie uśpienia (Stop/Standby) lub piny SWD zostały omyłkowo zrekonfigurowane w kodzie.
* **Weryfikacja (`-v`):** Po wgraniu narzędzie weryfikuje sumę kontrolną w pamięci Flash.
* **Reset (`-rst`):** Po udanym programowaniu następuje sprzętowy reset i natychmiastowe uruchomienie firmware.

Aby zbudować i od razu wgrać kod jednym kliknięciem:
* Użyj zadania **`[Windows] Build & Flash`** (dla kompilacji w Windows)
* Użyj zadania **`[Docker] Build & Flash`** (dla kompilacji w Dockerze)

### Debugowanie w Visual Studio Code
W zakładce **Run and Debug** (`Ctrl+Shift+D`) dostępne są dedykowane profile debugowania:
1. **`STM32Cube: Debug Full System (Bootloader + App)`** *(Rekomendowany)*:
   - Wgrywa do Flash oba obrazy (`v_box_boot.elf` pod `0x08000000` i `v_box.elf` pod `0x08008000`).
   - Wczytuje tablice symboli dla obu programów jednocześnie.
   - Umożliwia płynne śledzenie krokowe od startu procesora w Bootloaderze, przez skok `boot_jump_to_application()`, aż po start schedulera FreeRTOS w Aplikacji.
2. **`STM32Cube: Debug Application (v_box)`**:
   - Wgrywa i debuguje wyłącznie Aplikację Główną (`0x08008000`).
   - Zatrzymuje procesor na początku funkcji `main()` w Aplikacji.
3. **`STM32Cube: Debug Bootloader (v_box_boot)`**:
   - Wgrywa i debuguje wyłącznie samodzielny Bootloader (`0x08000000`).
   - Zatrzymuje procesor na początku funkcji `main()` w `boot_main.c`.
4. **`STM32Cube: Attach to Target (Hotplug / No Reset)`**:
   - Dołącza sesję GDB do już działającego mikrokontrolera bez wywoływania resetu (tryb Hotplug).
   - Wczytuje symbole obu programów.

### Współdzielenie programatora ST-Link z kontenerem (USB Pass-through)
Jeśli chcesz debugować bezpośrednio z wnętrza Linuksa (np. przez `openocd` lub `st-info` w Dockerze):
1. Podłącz programator do Dockera: zadanie **`[USB] Attach ST-Link to Docker`** (lub skrypt `.devcontainer/stlink_attach.bat`). Skrypt automatycznie wymusza flagę `--force`, eliminując kolizje z filtrem Wireshark / USBPcap.
2. Po zakończeniu pracy zwróć programator do Windowsa: zadanie **`[USB] Detach ST-Link from Docker`** (lub skrypt `.devcontainer/stlink_detach.bat`).

---

## 7. Automatyczny Raport Pamięci FLASH i RAM

Zaraz po zakończeniu linkowania (zarówno pod Windows, jak i w Dockerze) skrypt [tools/print_memory.py](file:///c:/WORK/5_v-box/tools/print_memory.py) analizuje wygenerowany plik mapy linkera (`v_box.map`) i wypisuje precyzyjną tabelę:

```text
==============================================================================
                   STM32C542RCT6 Memory Footprint Summary
==============================================================================
  Memory Region        Used [B]     Total [B]     Free [B]   Usage [%]  Visual Bar
------------------------------------------------------------------------------
  FLASH (ROM)         47 856 B    256.0 KB   214 288 B    18.26%   [====----------------]
    |-- .text         46 424 B        -             -           -     (Code, ISR vectors)
    |-- .rodata         1 272 B        -             -           -     (Constants, strings)
    `-- .data (LMA)       112 B        -             -           -     (Flash initializers)
------------------------------------------------------------------------------
  RAM (SRAM)          19 912 B     64.0 KB    45 624 B    30.38%   [======--------------]
    |-- FreeRTOS Heap  16 384 B        -             -       25.00%   (ucHeap pool in .bss)
    |-- Static & BSS    1 992 B        -             -         3.04%   (Globals, buffers)
    |-- Main Stack      1 024 B        -             -         1.56%   (MSP / ISR stack)
    `-- Libc Heap         512 B        -             -         0.78%   (sysmem / _sbrk)
==============================================================================
  Target MCU : STM32C542RCT6 (ARM Cortex-M33 @ 144 MHz)
  Firmware   : v_box.elf [Debug]
  Status     : OK (FLASH: 81.7% free, RAM: 69.6% free)
==============================================================================
```

### Dlaczego ta tabela jest kluczowa:
1. **Prawdziwy FLASH:** Standardowe narzędzie `size` wypisuje `text, data, bss`. W rzeczywistości FLASH to $\text{text} + \text{data}$ (początkowe wartości zmiennych globalnych muszą być trwale zapisane we Flashu).
2. **Wyjaśnienie narzutu RAM dla FreeRTOS:** Aż **16 KB (82% zajętego RAM-u)** stanowi zaalokowana sterta FreeRTOS (`ucHeap` zdefiniowana przez `configTOTAL_HEAP_SIZE` w `FreeRTOSConfig.h`). Raport dynamicznie odnajduje symbol `ucHeap` – jeśli zmienisz rozmiar sterty, tabela od razu to odzwierciedli.
3. **Wsparcie dla nowych sekcji linkera:** Jeśli dodasz do pliku `.ld` własną sekcję w pamięci Flash (`> ROM`) lub RAM (`> RAM`), skrypt automatycznie wykryje jej adres, doliczy do właściwego regionu i wyświetli z nazwy w poddrzewie.
4. **Wskaźnik zdrowia:** Przy przekroczeniu 80% pamięci status zmienia się na `WARNING`, a powyżej 90% na `CRITICAL`.

---

## 8. Długoterminowa Archiwizacja Środowiska (Reproducible Builds)

Budowanie obrazu z samego pliku `Dockerfile` po upływie kilku lat niesie ze sobą ryzyko:
* Linki do pobierania kompilatorów z serwerów ARM mogą wygasnąć (tzw. *URL rot*),
* Repozytoria Ubuntu 24.04 po zakończeniu wsparcia (ok. 2029 r.) zostaną zarchiwizowane, co spowoduje błędy `apt-get update`.

Aby zagwarantować, że **za 5 lub 10 lat projekt zbuduje się w 15 sekund bez dostępu do internetu**, zastosuj procedurę archiwizacji obrazu.

### Krok 1: Eksport obrazu do pliku archiwum (.tar)
Uruchom zadanie VS Code: **`[Docker] Export Image to Archive`**
(lub wykonaj skrypt `.devcontainer\export_image.bat`).
Skrypt automatycznie wyświetla **procentowy pasek postępu**, bieżącą prędkość (MB/s) oraz czas do ukończenia (ETA).
Utworzy on w katalogu głównym plik:
`vbox-builder_stm32c542.tar` (~943 MB w formacie skompresowanych warstw OCI).
Zapisz ten plik w firmowym repozytorium wydań, na dysku sieciowym lub serwerze NAS.

### Krok 2: Odtworzenie środowiska na nowym komputerze za kilka lat
Gdy zainstalujesz na nowej maszynie Visual Studio Code i Dockera:
1. Pobierz plik `vbox-builder_stm32c542.tar` z firmowego repozytorium SharePoint ([v-box-tester SharePoint Archive](https://lincorspzoo-my.sharepoint.com/:f:/g/personal/j_lukaszewicz_lincor_pl/IgBjV5wdDW81QqDHNNTQZr4GARtXaX-XTG5QeiUvk5Fbme8?e=RkfN9G)) i umieść go w katalogu głównym projektu.
2. Uruchom zadanie VS Code: **`[Docker] Import Image from Archive`**
   (lub skrypt `.devcontainer\import_image.bat`).
3. W terminalu wyświetli się **pasek postępu od 0.0% do 100.0%** z transferem danych i prędkością ładowania warstw.
4. Po załadowaniu obrazu możesz od razu kompilować kod zadaniem **`[Docker] Build`** – bez pobierania czegokolwiek z sieci!

---

## 9. Zaawansowany Ekosystem Narzędziowy w Dockerze (QA, CAN, GUI)

Obraz kontenera `vbox-builder:latest` to kompletna, samowystarczalna stacja robocza embedded:

### A. Jakość Kodu i Analiza Statyczna (QA)
Wewnątrz kontenera zainstalowane są profesjonalne narzędzia audytu kodu C:
```bash
# Analiza statyczna pod kątem wycieków i reguł bezpieczeństwa MISRA-C
cppcheck --enable=all --suppress=missingIncludeSystem app/

# Automatyczne generowanie pełnego portalu dokumentacji Doxygen
doxygen Doxyfile

# Operacje na plikach binarnych (obliczanie CRC32, dopełnianie zerami, scalanie z bootloaderem)
srec_cat build/debug_GCC_NUCLEO-C542RC/v_box.bin -binary -crop 0x0000 0xFFFC -crc32-l-e 0xFFFC -o firmware_with_crc.bin -binary
```

### B. Magistrala CAN i Narzędzia Diagnostyczne
Kontener wspiera pełny linuksowy podsystem SocketCAN (`can-utils`):
* `cansend` – nadawanie pojedynczych ramek testowych,
* `candump` – ciągły nasłuch i logowanie ruchu magistrali,
* `canplayer` – odtwarzanie zarejestrowanych logów z pojazdu/stanowiska testowego,
* `cangen` – generator losowego ruchu magistrali (testy obciążeniowe).

### C. Aplikacje Okienkowe GUI w Pythonie (PyQt + pyqtgraph)
Kontener zawiera kompletny stos bibliotek graficznych X11/OpenGL kompatybilny z mechanizmem **WSLg w Windows 10/11**:
* Aplikacje okienkowe pisane w **PyQt6** lub **PyQt5** uruchomione w kontenerze **wyświetlają się bezpośrednio na Twoim pulpicie Windows jako zwykłe okna**.
* Dostępna jest biblioteka `pyqtgraph` do kreślenia wykresów telemetrii w czasie rzeczywistym.
* Dostępne są biblioteki sprzętowe: `python-can`, `cantools` (dekodowanie plików baz `.dbc`), `gs-usb` (obsługa konwerterów CANable/Candlelight) oraz `pyserial`.

---

## 10. Rozwiązywanie Problemów (Troubleshooting & FAQ)

### Q: Kompilacja w Windowsie zgłasza: `Error: not a CMake build directory (missing CMakeCache.txt)`
**Przyczyna:** Katalog `build/` został ręcznie skasowany lub projekt został świeżo sklonowany z repozytorium Git.  
**Rozwiązanie:** Uruchom zadanie **`[Windows] Configure`** lub po prostu **`[Windows] Build`** (posiada ono regułę automatycznego uruchomienia konfiguracji przed kompilacją).

### Q: Narzędzie `usbipd` wyświetla: `warning: USB filter 'USBPcap' is known to be incompatible with this software`
**Przyczyna:** Na komputerze zainstalowany jest program Wireshark wraz ze sterownikiem przechwytywania USBPcap, który blokuje standardowe przejmowanie portów USB.  
**Rozwiązanie:** Nasz skrypt `.devcontainer/stlink_manager.py` oraz `stlink_attach.bat` automatycznie dodaje flagę `bind --force`, dzięki czemu podłączenie ST-Linka przebiega bezbłędnie pomimo obecności filtra USBPcap.

### Q: Po odłączeniu od Dockera ST-Link nie pojawia się w Windowsie
**Przyczyna:** Standardowe polecenie `usbipd detach` odłącza urządzenie od WSL, ale pozostawia je w stanie `Shared (forced)`.  
**Rozwiązanie:** Skrypt `.devcontainer/stlink_detach.bat` wykonuje pełną procedurę `usbipd unbind`, co natychmiast przywraca oryginalny sterownik ST-Linka w Menedżerze Urządzeń Windows.

### Q: Dlaczego po kompilacji w Dockerze plik binarny pojawia się w katalogu Windowsa?
**Odpowiedź:** Jest to celowy zabieg architektoniczny. Skrypt `docker_build.bat` kompiluje kod w odizolowanym folderze `build/docker/`, po czym kopiuje gotowy plik wykonywalny do `build/debug_GCC_NUCLEO-C542RC/v_box.elf`. Dzięki temu uniwersalne zadanie **`Flash Device`** zawsze znajduje plik pod tą samą ścieżką.

