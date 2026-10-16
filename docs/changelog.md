# Changelog

Wszystkie znaczące zmiany w projekcie będą dokumentowane w tym pliku.

## [2026-10-16]
### Dodane
- **Architektura podwójnego targetu budowania Dual-Target oraz skrypty linkera (`linker/stm32c542xc_boot_flash.ld`, `linker/stm32c542xc_app_flash.ld`, `CMakeLists.txt`, `stm32c5xx_dfp/CMakeLists.txt`):**
  - Podział pamięci Flash na region Bootloadera (32 KB, `0x08000000`–`0x08007FFF`) oraz Aplikacji Głównej (224 KB, `0x08008000`–`0x0803FFFF`).
  - Alokacja 64-bajtowego bufora pamięci nieresetowalnej `.noinit.boot_shared` (`SHARED_RAM` pod adresem `0x2000FFC0`) do bezpiecznej komunikacji międzyprocesowej IPC (Bootloader <-> Aplikacja) bez utraty danych przy `NVIC_SystemReset()`.
  - Dedykowane targety kompilacji w CMake: `v_box` (Aplikacja z FreeRTOS, limit 224 KB) oraz `v_box_boot` (samodzielny Bootloader bare-metal z flagą optymalizacji `-Os`, rozmiar 20 064 B, 61.2% zajętości).
- **Samodzielny Bootloader magistrali CAN (`app/bootloader/`):**
  - `boot_main.c`: Lekki, bezsystemowy punkt wejścia ze 144 MHz zegarem bazowym, obsługą peryferium FDCAN2 i USART2, 300 ms oknem startowym nasłuchu na CAN2 oraz automatycznym skokiem do aplikacji.
  - `boot_shared.h`, `boot_shared.c`: Moduł współdzielonej struktury IPC (`boot_shared_ram_t`, `BOOT_SHARED_MAGIC = 0xB00710AD`) zabezpieczonej sumą kontrolną CRC32.
  - `boot_jump.h`, `boot_jump.c`: Bezpieczna procedura tranzycji do Aplikacji Głównej (`boot_jump_to_application`) z wyłączeniem przerwań, deinitializacją FDCAN2 i UART, czyszczeniem rejestrów limitu stosu Cortex-M33 (`MSPLIM`, `PSPLIM`), relokacją tablicy wektorów przerwań (`SCB->VTOR = 0x08008000`), ustawieniem wskaźnika MSP i skokiem do `Reset_Handler`.
  - `openbl_core.h`, `openbl_core.c`: Bare-metal silnik protokołu OpenBootloader obsługujący komendy `CMD_DISCOVER_NODES (0xA0)`, `CMD_SELECT_TARGET (0xA1)`, `CMD_ENTER_BOOTLOADER (0xA2)`, `CMD_ERASE_APP (0x44)`, `CMD_WRITE_MEMORY (0x31)`, `CMD_VERIFY_FIRMWARE (0xA3)` oraz `CMD_JUMP_TO_APP (0x21)`.
  - `openbl_flash_c5.h`, `openbl_flash_c5.c`: Sterownik operacji Flash STM32C542 realizujący odblokowanie/blokowanie kontrolera Flash, kasowanie stron (`HAL_FLASH_EraseByAddr`) oraz adaptacyjne programowanie bloków pamięci z natychmiastową weryfikacją odczytu.
- **Mechanizm Anti-Brick oraz automatyczna telemetria awarii Recovery (`app/ota_update/fault_recovery.h`, `fault_recovery.c`):**
  - Przechwytywanie wyjątków `HardFault_Handler`, `MemManage_Handler`, `BusFault_Handler`, `UsageFault_Handler` oraz asercji FreeRTOS `configASSERT` w Aplikacji Głównej.
  - Zrzut adresu instrukcji błędu (`stacked_pc`) i rejestru statusu rdzenia (`SCB->CFSR`) do pamięci `SHARED_RAM` przed programowym resetem mikrokontrolera.
  - Licznik nieudanych uruchomień (`boot_fail_counter`): po 3 nieudanych startach Aplikacji mikrokontroler permanentnie blokuje dalsze pętle resetu, wchodzi w tryb `BOOT_MODE_RECOVERY`, transmituje co 1 s ramkę alarmową `0xEE` (`CRASH_RECOVERY_BEACON`) na magistralę CAN2 i oczekuje na wgranie poprawionej wersji oprogramowania.
  - Mechanizm potwierdzania stabilnego rozruchu (`fault_recovery_confirm_app_healthy()`): po 3 sekundach bezawaryjnej pracy scheduler zeruje licznik błędów.
- **Narzędzie scalania wsadów binarnych i zadania programowania (`tools/merge_binaries.py`, `.vscode/tasks.json`, `.devcontainer/docker_build.bat`):**
  - Implementacja skryptu `tools/merge_binaries.py` generującego połączone obrazy produkcyjne `v_box_full.bin` oraz `v_box_full.hex` łączące Bootloader pod `0x08000000` i Aplikację pod `0x08008000` (z dopełnieniem przestrzeni bajtami `0xFF`).
  - Nowe zadania w VS Code: `[Flash] Full Merged Image (Bootloader + App)`, `[Flash] Bootloader Only`, `[Flash] MainApp Only`.
  - Automatyczne generowanie i kopiowanie wszystkich artefaktów (`.elf`, `.bin`, `.hex`) do katalogu wyjściowego `build/debug_GCC_NUCLEO-C542RC/`.
- **Narzędzie CLI aktualizacji i diagnostyki CAN Updater w Pythonie (`tools/can_updater.py`, `tools/requirements_ota.txt`):**
  - Implementacja wieloplatformowego skryptu CLI (`can_updater.py`) opartego na `python-can` wspierającego adaptery SLCAN (np. CANable) oraz natywne SocketCAN (Linux).
  - Wbudowany, czysty stos transportowy ISO 15765-2 (ISO-TP) w Pythonie realizujący segmentację i asemblację ramek SF, FF, CF z dynamiczną obsługą Flow Control (Block Size oraz STmin) bez potrzeby zewnętrznych sterowników jądra systemu.
  - Procedura skanowania magistrali (`--scan`) z odpytywaniem na ID `0x7DF` i parsowaniem dwuramkowych ogłoszeń na ID `0x7E9` (ekstrakcja 96-bitowego fabrycznego UID, trybu pracy urządzenia oraz wersji oprogramowania).
  - Tryb symulacji Dummy Update (`--dummy`, `--size`) testujący pełną ścieżkę aktualizacji (Target Select -> Erase -> Stream Blocks z weryfikacją CRC32 pojedynczych bloków 256B -> weryfikacja całościowego obrazu CRC32 -> Jump) z dynamicznym paskiem postępu na żywo, pomiarem przepustowości (KB/s) oraz czasem operacji.
  - Obsługa programowania właściwych plików binarnych (`--file`) z automatycznym podziałem na bloki i do 3 ponowień transmisji przy błędach szyny.
- **Inicjalizacja i uruchomienie peryferium FDCAN2 dla Bootloadera / OTA (`generated/hal/mx_fdcan2.h`, `mx_fdcan2.c`, `mx_system.c`, `mx_hal_def.h`, `STM32_HAL_Driver_codegen.cmake`):**
  - Implementacja niskopoziomowego sterownika `FDCAN2` na mikrokontrolerze STM32C542RCT6 (zegar 144 MHz, prescaler 1, timing: seg1=251, seg2=36, sjw=36 -> 500 kbps Classic CAN 2.0, próbkowanie 87.5%).
  - Konfiguracja wyprowadzeń GPIO na Porcie B: `PB5` (RX, złącze CN10 pin 6) oraz `PB6` (TX, złącze CN10 pin 3) w trybie Alternate Function AF9.
  - Konfiguracja przerwania `FDCAN2_IT0_IRQn` z bezpiecznym priorytetem FreeRTOS równym 5 (`HAL_CORTEX_NVIC_PREEMP_PRIORITY_5` >= `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`) oraz rejestracja procedury obsługi `FDCAN2_IT0_IRQHandler()`.
- **Profil parsera CAN `cp_boot_ota` (`app/can_bus/can_parser/parsers/cp_boot_ota.h`, `cp_boot_ota.c`, `CMakeLists.txt`):**
  - Wygenerowanie szkieletu narzędziem `tools/create_parser.py boot_ota` i rejestracja identyfikatora `CAN_PARSER_ID_BOOT_OTA` w dyspozytorze `can_parser.c`.
  - Konfiguracja filtrów sprzętowych M_CAN Message RAM dla identyfikatorów `CAN_ID_OTA_BROADCAST` (`0x7DF`) oraz `CAN_ID_ISOTP_REQ` (`0x7E0`).
  - Implementacja nieblokujących callbacków odbiorczych logujących odebrane ramki diagnostyczne i transportowe przez `safe_printf`.
- **Wdrożenie instancji `can2_inst` w aplikacji (`main.c`):**
  - Alokacja instancji `can2_inst` oraz inicjalizacja za pomocą `can_manager_init(&can2_inst, stm32c5_can2_interfaceGet(), "CAN2")`.
  - Automatyczne powiązanie instancji z profilem `CAN_PARSER_ID_BOOT_OTA` i uruchomienie dedykowanych zadań FreeRTOS `CAN2_tx` i `CAN2_rx`.
- **Warstwa transportowa ISO 15765-2 (ISO-TP) w czystym C (`app/can_bus/isotp/isotp.h`, `isotp.c`):**
  - Implementacja przenośnego stosu ISO-TP (SF, FF, CF, FC) z buforem reasemblacji do 540 bajtów.
  - Automatyczne generowanie ramek Flow Control (`0x30, BS=0, STmin=1ms`) na ID `0x7E8` po odebraniu First Frame.
  - Weryfikacja ciągłości numeracji sekwencyjnej Sequence Number (SN) ramek Consecutive Frame.
- **Moduł sum kontrolnych CRC32 IEEE 802.3 (`app/ota_update/crc32.h`, `crc32.c`):**
  - Tablicowa implementacja sumy kontrolnej Ethernet/ZIP/zlib (wielomian `0xEDB88320`) zgodna w 100% z narzędziami PC w Pythonie.
- **Asynchroniczny menedżer OTA i silnik weryfikacji Dummy Update (`app/ota_update/ota_manager.h`, `ota_manager.c`, `ota_protocol.h`):**
  - Implementacja dedykowanego zadania FreeRTOS `ota_worker_task` (kolejka `s_ota_rx_queue`) gwarantującego nieblokujące wykonanie `can_rx_task` (Reguła 14).
  - Obsługa procedury Discovery (`0xA0`) z antykolizyjnym slotem czasowym opartym na 96-bitowym fabrycznym UID (`0x08FFF800`) i odpowiedzią na `0x7E9`.
  - Obsługa wyboru węzła Target Select (`0xA1`), symulowanego kasowania Erase (`0x44`), strumieniowego odbioru bloków pamięci (`0x31`) z weryfikacją CRC32 bloku oraz finalnej weryfikacji całości wsadu (`0xA3`).
  - Przekierowanie callbacków z profilu `cp_boot_ota.c` do kolejki workera w czasie < 1 µs.

### Zmienione
- **Aktywacja sterowników HAL Flash (`generated/hal/stm32c5xx_hal_conf.h`, `CMakeLists.txt`):**
  - Odblokowanie makr `USE_HAL_FLASH_PROGRAM_BY_ADDR`, `USE_HAL_FLASH_ERASE_BY_ADDR`, `USE_HAL_FLASH_ERASE_PAGE`, `USE_HAL_FLASH_GET_LAST_ERRORS` oraz włączenie pliku `stm32c5xx_drivers/hal/stm32c5xx_hal_flash.c` do kompilacji obu targetów.
- **Wieloplatformowość modułu `safe_printf` dla systemów RTOS i bare-metal (`app/logging/safe_printf.c`):**
  - Uzależnienie muteksu FreeRTOS i nagłówków `FreeRTOS.h`/`semphr.h` od makra `CMSIS_USE_RTOS_FreeRTOS_Core_11_3_1`. Umożliwiło to bezkolizyjne współdzielenie tego samego kodu logowania przez USART2 w Aplikacji z FreeRTOS oraz w bare-metal Bootloaderze.
- **Rozszerzenie adaptera portowego `stm32c5_can_adapter` o obsługę CAN2 (`app/port/stm32c5/stm32c5_can_adapter.h`, `stm32c5_can_adapter.c`):**
  - Wystawienie publicznego gettera `const can_interface_t *stm32c5_can2_interfaceGet(void)` zgodnie z regułą zero-extern i hermetyzacją.
  - Refaktoryzacja procedur inicjalizacji, transmisji, odbioru oraz programowania filtrów Message RAM na wspólny silnik parametryczny `stm32c5_can_*_generic()`.
  - Rozbudowa procedury obsługi przerwania `HAL_FDCAN_RxFifo0Callback()` o dyskryminację instancji peryferyjnej (`FDCAN1` vs `FDCAN2`) i wywoływanie właściwego callbacku powiadomienia.
- **Dostosowanie pamięci sterty FreeRTOS (`generated/middleware/FreeRTOSConfig.h`):**
  - Zwiększenie `configTOTAL_HEAP_SIZE` z 16 KB (16384 B) do 24 KB (24576 B) z zachowaniem ponad 37 KB wolnej pamięci SRAM mikrokontrolera, zabezpieczające zasoby na kolejki i zadania obu instancji CAN.

### Naprawione
- **Obsługa odczytu 96-bitowego fabrycznego Device UID (`app/ota_update/ota_manager.c`):**
  - Wyeliminowano krytyczny błąd szyny (BusFault) i HardFault mikrokontrolera Cortex-M33, zastępując dostęp bajtowy (`uint8_t *` / `ldrb`) odczytem 32-bitowym słownym (`uint32_t *` / `ldr`) z wyrównanych adresów bloku informacyjnego Flash (`0x08FFF800`, `0x08FFF804`, `0x08FFF808`), zgodnie ze specyfikacją szyny systemowej STM32C5.

## [2026-10-15]
### Dodane
- **Kompletny ekosystem narzędziowy w kontenerze Docker (`.devcontainer/Dockerfile`):**
  - Rozbudowa obrazu `vbox-builder` o narzędzia jakości kodu i QA: `cppcheck` (analiza statyczna / reguły MISRA-C), `doxygen` + `graphviz` (automatyczna dokumentacja HTML i grafy relacji), `srecord` (`srec_cat` - sumy kontrolne CRC32, scalanie bootloadera), `lcov` (analiza pokrycia kodu).
  - Wsparcie dla magistrali CAN: `can-utils` (SocketCAN: `cansend`, `candump`, `canplayer`, `cangen`).
  - Pełny stos Pythona pod aplikacje okienkowe GUI i telemetrię laboratoryjną (kompatybilny z WSLg): `python3-pyqt6`, `python3-pyqt5`, `pyqtgraph` (wykresy czasu rzeczywistego), `python-can`, `gs-usb` (adaptery CANable/Candlelight), `cantools` (bazy sygnałów `.dbc`), `pyserial`, `pyusb`, `pandas`, `numpy`, `openpyxl` (raporty Excel) oraz `pytest`.
  - Narzędzia bezpiecznego rozruchu i kryptografii: `imgtool` (standard podpisywania obrazów MCUboot / ARM TF-M z wersjonowaniem i anti-rollback) oraz `python3-cryptography` / OpenSSL.
- **Narzędzia długoterminowej archiwizacji i odtwarzania obrazu (`.devcontainer/export_image.bat`, `.devcontainer/import_image.bat`, `.devcontainer/docker_transfer.py`, `.vscode/tasks.json`):**
  - Wprowadzenie dedykowanych skryptów oraz zadań w VS Code: `[Docker] Export Image to Archive` (zapis całego zamrożonego środowiska do pliku `vbox-builder_stm32c542.tar`) oraz `[Docker] Import Image from Archive` (natychmiastowe odtworzenie obrazu na dowolnym nowym komputerze bez dostępu do internetu).
  - Opracowano dedykowany skrypt strumieniujący `.devcontainer/docker_transfer.py`, który wzbogaca `docker save` i `docker load` o **dynamiczny, procentowy pasek postępu (0.0% – 100.0%)**, wskaźnik przepustowości dyskowej w MB/s, szacowany czas zakończenia (ETA), bezpieczną obsługę przerwania `Ctrl+C` oraz atomowy zapis pliku wyjściowego (z automatycznym fallbackiem do natywnego Dockera przy braku Pythona).
  - Integracja bezpośredniego łącza do firmowego archiwum SharePoint (`vbox-builder_stm32c542.tar`) oraz 4-krokowej procedury uruchomienia w `README.md` i `docs/build_and_development_guide.md`.
- **Zautomatyzowany moduł wersjonowania Firmware i Hardware (`app/version/`, `CMakeLists.txt`):**
  - Implementacja modułu `app/version/version.c` oraz `version.h` zgodnego z Semantic Versioning (SemVer 2.0).
  - Automatyczna generacja metadanych w CMake (`version_config.h`): wstrzykiwanie taga i hasha commita Git (`git describe`, `git rev-parse`), znacznika czasu kompilacji (UTC), typu buildu oraz identyfikatora i rewizji sprzętu (`NUCLEO-C542RC`).
  - Dodanie czytelnego banera diagnostycznego UART (`version_printBootBanner()`) wyświetlanego w `main()` natychmiast po resecie procesora.
  - Rozszerzenie `safe_printf.c` o bezpieczne nadawanie przez UART również w fazie wczesnego rozruchu (przed wystartowaniem schedulera FreeRTOS).
  - Wdrożono regułę `* text=auto` oraz wymuszenie `LF` dla całego kodu źródłowego (`*.c`, `*.h`, `*.py`, `*.json`, `*.md`, `CMakeLists.txt`, `.clangd`), przy jednoczesnym zachowaniu `CRLF` dla skryptów wsadowych Windows (`*.bat`, `*.cmd`) oraz flag binarnych dla archiwów i wsadów (`*.bin`, `*.tar`, `*.elf`, `*.hex`).
  - Wyeliminowano problem oznaczania plików jako zmodyfikowanych w Git po operacjach `rebase` na systemie Windows.
- **Kompleksowy podręcznik budowania i rozwoju projektu (`docs/build_and_development_guide.md`, `README.md`, `.vscode/extensions.json`):**
  - Opracowanie wyczerpującego przewodnika inżynierskiego opisującego architekturę kompilacji, budowanie natywne w Windows, budowanie w kontenerze Docker, procedurę programowania i debugowania ST-Link V3, archiwizację obrazów oraz rozwiązywanie problemów (FAQ).
  - Przebudowa głównego pliku `README.md` z sekcją szybkiego startu, tabelą wszystkich zadań VS Code, opisem ujednoliconej ścieżki pliku binarnego oraz kompletnym indeksem linków do dokumentacji szczegółowej w katalogu `docs/`.
  - Wprowadzenie pliku rekomendacji rozszerzeń `.vscode/extensions.json` oraz wyodrębnienie w `README.md` listy wtyczek niezbędnych (`stmicroelectronics.stm32-vscode-extension`, `ms-vscode.cmake-tools`, `llvm-vs-code-extensions.vscode-clangd`) i zalecanych (`cppincludeguard`, `korofileheader`, `serial-monitor`, `memory-inspector`).
- **Automatyczny raport zajętości pamięci FLASH/RAM (`tools/print_memory.py`, `CMakeLists.txt`):**
  - Opracowano dedykowany skrypt analityczny `tools/print_memory.py` uruchamiany po linkowaniu (`POST_BUILD`) zarówno pod Windows, jak i w kontenerze Docker.
  - Generowanie czytelnej tabeli pamięci z podziałem na FLASH i RAM, procentowym zużyciem, wolną przestrzenią oraz wizualnym paskiem postępu.
  - Dynamiczna detekcja zaalokowanej sterty FreeRTOS (`.bss.ucHeap` z `FreeRTOSConfig.h`), stosu głównego (`STACK_SIZE` / MSP), sterty libc (`HEAP_SIZE`) oraz sekcji autorskich (np. DMA, konfiguracja).
  - Dodanie stopki podsumowującej z informacją o platformie (STM32C542RCT6 Cortex-M33 @ 144 MHz), pliku binarnym oraz statusie zdrowia pamięci (`OK` / `WARNING` / `CRITICAL`).

- **Wytyczne zarządzania dokumentacją markdown (Docs-as-Code) w `.md`:**
  - Wprowadzenie Sekcji 15 definiującej regułę *Single Source of Truth* i atomowej aktualizacji dokumentacji.
  - Określenie ścisłej matrycy odpowiedzialności plików (`README.md`, `docs/project_documentation.md`, `docs/build_and_development_guide.md`, `docs/can_bus/can_parser_guide.md`, `docs/changelog.md`), zobowiązującej AI do równoległej synchronizacji właściwych dokumentów `.md` przy każdej zmianie kodu i architektury.

### Zmienione
- **Modernizacja konfiguracji kontenera deweloperskiego pod środowisko STM32C542 (`.devcontainer/`):**
  - **Dopasowanie wtyczek VS Code (`.devcontainer/devcontainer.json`):** Synchronizacja 1:1 z działającym środowiskiem deweloperskim: dodano oficjalny pakiet STM32 VS Code Extension (`stmicroelectronics.stm32-vscode-extension` oraz moduły zależne: build-analyzer, build-cmake, bundles-manager, clangd, debug-core, debug-stlink-gdbserver, project-manager, registers, rtos), wtyczki inspekcyjne `eclipse-cdt.memory-inspector` i `eclipse-cdt.serial-monitor`, a także narzędzia formatowania `akiramiyakoda.cppincludeguard` i `obkoro1.korofileheader`.
  - **Przeniesienie konfiguracji formatowania i Intellisense (`.devcontainer/devcontainer.json`):** Wstrzyknięto reguły generatora include guardów oraz szablonów Doxygen zgodne z `.vscode/settings.json`, a także skorygowano ścieżkę bazodanową clangd na `build/debug_GCC_NUCLEO-C542RC`.
  - **Kompilacja i narzędzia bazowe (`.devcontainer/Dockerfile`):**
    - Zainstalowano wersję CMake 3.31.5 (oficjalny pakiet binarny Kitware), eliminując krytyczny błąd blokujący konfigurację projektu wynikający z wymogu `cmake_minimum_required(VERSION 3.30)` (domyślny pakiet Ubuntu 24.04 zawierał zbyt starą wersję 3.28.3).
    - Zaktualizowano toolchain ARM GNU do wersji 14.2.rel1, zgodnej z konfiguracją `cmake/GCC.14.3.1.cmake` oraz architekturą ARM Cortex-M33 (DSP, FPU).
  - **Wsparcie dla programowania i debugowania ST-LINK V3 (`.devcontainer/Dockerfile`, `.devcontainer/stlink_manager.py`):**
    - Rozszerzono reguły udev w kontenerze o pełną rodzinę identyfikatorów ST-LINK V2/V3 (PIDs: 3744, 3748, 374a, 374b, 374d, 374e, 374f, 3752, 3753, 3754, 3755, 3757).
    - Dodano automatyczne wykrywanie programatorów ST-Link po masce VID:PID oraz obsłużono flagę `--force` w `stlink_manager.py`, co zapobiega błędom kolizji filtrów USB (np. USBPcap/Wireshark) podczas przekazywania urządzenia przez `usbipd-win`.
  - **Aktualizacja dokumentacji kontenera (`.devcontainer/info.md`):** Zastąpiono przestarzałe instrukcje dla STM32H5 wyczerpującym poradnikiem dla mikrokontrolera STM32C542RCT6, procedurą podłączania ST-Link V3, kompilacją przez CMake Presets/Ninja oraz profilami debugera GDB Server.
  - **Uporządkowanie i unifikacja zadań w VS Code (`.vscode/tasks.json`, `.devcontainer/docker_build.bat`):**
    - Wprowadzono ujednoliconą ścieżkę dla gotowego pliku binarnego (`build/debug_GCC_NUCLEO-C542RC/v_box.elf`), do której kompiluje bezpośrednio Windows oraz do której kopiuje wynik Docker (z odizolowanego `build/docker/`).
    - Zredukowano listę zadań do 6 czytelnych poleceń z prefiksami `[Windows]`, `[Docker]` oraz `[USB]`.
    - Wdrożono jedno uniwersalne zadanie programowania mikrokontrolera `Flash Device` (`STM32_Programmer_CLI`), współdzielone przez kompozycje `[Windows] Build & Flash` oraz `[Docker] Build & Flash`.
    - Usunięto nadmiarowe wywołanie `arm-none-eabi-size` ze skryptu `docker_build.bat`, eliminując powielony wydruk sekcji pamięci (CMakeLists.txt natywnie realizuje raport pamięci po zakończeniu linkowania).

## [2026-10-14]
### Dodane
- **Kompleksowa dokumentacja techniczna projektu (`docs/project_documentation.md`, `README.md`):**
  - Opracowanie wyczerpującej, technicznej dokumentacji projektu w języku angielskim opisującej wszystkie peryferia sprzętowe (FDCAN1, ADC1, USART2, GPIO, NVIC, SysTick), szczegółową tabelę GPIO (wejścia, wyjścia, ADC, piny złącza ST-Morpho CN7/CN10, aliasy Arduino), tekstowo-graficzny diagram wyprowadzeń Nucleo-64.
  - Szczegółowy opis architektury podsystemu CAN (pipeline odbiorczy oparty na przerwaniach i semaforach, profile parserów, zero-extern getter, specyfikacja ramek `0x01F2F3F0` i `0x01F2F3F4`).
  - Dokumentacja sterowania przekaźnikiem bistabilnym (generowanie zboczy, impuls przygotowawczy 40 ms, sprzężenie zwrotne PB12 z timeoutem 200 ms, obsługa zaniku zasilania, synchronizacja rozruchowa bez klikania, asynchroniczny worker).
  - Architektura warstwy abstrakcji sprzętowej (kontrakty portów w czystym C, adaptery sprzętowe STM32C5, wzorzec gettera interfejsu).
  - Pełny model współbieżności FreeRTOS (tabela zadań z priorytetami i stosem, diagram komunikacji międzyzadaniowej IPC, kolejki, mutexy oraz bezwzględna reguła priorytetów przerwań NVIC >= 5).
- **Asynchroniczny model sterowania przekaźnikiem z kolejką zdarzeń (`app/io/io_manager.c`, `app/io/io_manager.h`, `app/tasks_config.h`, `app/can_bus/can_parser/parsers/cp_v_box.c`):**
  - Wprowadzenie dedykowanego zadania `relay_worker_task` (priorytet `tskIDLE_PRIORITY + 2`, stos 256 słów) oraz kolejki zleceń FreeRTOS `s_relay_cmd_queue`.
  - Dodanie nieblokującej funkcji `io_manager_setRelayStateAsync()` o czasie wykonania < 1 µs, dzięki czemu callback CAN `on_v_box_relay_cmd_rx` nie wstrzymuje zadania `can_rx_task` ani na milisekundę.
  - Całkowita eliminacja ryzyka przepełnienia bufora sprzętowego FDCAN (Rx FIFO 0 Overflow) podczas generowania zboczy i oczekiwania na sprzężenie zwrotne przekaźnika.

### Zmienione
- **Reallokacja wejść cyfrowych i rozszerzenie do 9 pinów (`app/port/bsp_io.h`, `app/port/stm32c5/bsp_io.c`):**
  - Przeniesienie wejść krańcówek ze względu na konflikt z FDCAN2: Drzwi KIER na pin **PC8** (CN10 pin 31), Drzwi INNE na pin **PC7** (CN10 pin 19), a dodatkowego 9. wejścia na pin **PB2** (CN10 pin 22).
  - Piny **PB5** (`FDCAN2_RX` / CN10 pin 6) oraz **PB6** (`FDCAN2_TX` / CN10 pin 3) zostały całkowicie zwolnione w konfiguracji GPIO i zabezpieczone na potrzeby przyszłej magistrali FDCAN2.
  - Zmiana typu zwracanego przez `bsp_io_buttons_read()` z `uint8_t` na `uint16_t` oraz implementacja odczytu rejestrów `GPIOB->IDR` i `GPIOC->IDR` z mapowaniem 9 bitów (SW0..SW8).
- **Rozszerzenie debouncingu i telemetrii I/O do 16 bitów (`app/io/io_manager.c`, `app/io/io_manager.h`):**
  - Aktualizacja struktury `io_debounce_t` i funkcji `io_debounce_update()` do 16-bitowych masek bitowych, zapewniając pełną odporność na drgania styków dla wszystkich 9 wejść równolegle.
  - Aktualizacja publicznego API `io_manager_getAllBtnState()` do zwracania `uint16_t`.
  - Rozszerzenie ramki CAN telemetrii `0x01F2F3F4`: młodsze bity wejść (SW0..SW7) w `data[0]`, starsze bity (SW8 w bit 0) w `data[2]`, zachowując pełną kompatybilność wsteczną.
  - Aktualizacja formatu logowania UART w `io_manager_task` do postaci `BTN: 0x000 [SW8..0: 0 0000 0000]`.
- **Porządki w pliku głównym (`main.c`):**
  - Usunięcie tymczasowego zadania testowego `gpio_test_task` i przywrócenie minimalistycznej struktury pliku.
- **Aktualizacja raportu GPIO mikrokontrolera (`docs/GPIO_Report.md`):**
  - Odnotowanie nowej alokacji pinów (PC8, PC7, PB2), zwolnienia pinów PA9 i PA10 do puli wolnych oraz rezerwacji PB5 i PB6 dla FDCAN2.
- **Sterowanie przekaźnikiem bistabilnym sterowanym zboczami (`app/io/io_manager.c`, `app/io/io_manager.h`):**
  - Implementacja funkcji `io_manager_setRelayState()` realizującej deterministyczne generowanie zboczy (LOW->HIGH dla załączenia, HIGH->LOW dla wyłączenia z impulsem przygotowawczym 40 ms), pętlę aktywnego oczekiwania na potwierdzenie z pinu sprzężenia zwrotnego (timeout 200 ms) oraz do 2 prób przełączenia.
  - Wdrożenie automatu stanów odpornego na awarie zasilania przekaźnika (`s_relay_last_attempt_failed`, `s_relay_confirmed`, `s_relay_last_target`): w przypadku nieudanego przełączenia (np. odcięte zasilanie główne przekaźnika) logika oznacza stan jako niepotwierdzony i po przywróceniu zasilania oraz ponownym nadesłaniu komendy bezwzględnie ponawia sekwencję sterowania od zera (wymuszenie stanu przeciwnego, odczekanie, zbocze właściwe), ignorując fałszywie pasujący stan na wejściu sprzężenia.
  - Bezpieczny powrót pinu sterującego PA8 do stanu spoczynkowego zgodnego z rzeczywistym odczytem w przypadku niepowodzenia (zapobieganie ciągłemu wystawianiu 3.3V na niezasilonym module).
  - Zabezpieczenie procedury przełączania przekaźnika dedykowanym mutexem FreeRTOS `s_relay_mutex`.
  - Natychmiastowa inicjalizacja zmiennej `btn` w `io_manager_init()` za pomocą `bsp_io_buttons_read()`, co pozwala odczytać zapamiętany stan przekaźnika zaraz po resecie procesora.

### Naprawione
- **Eliminacja niepotrzebnego cyklu załączenia/wyłączenia przy rozruchu (`app/io/io_manager.c`):**
  - Rozwiązano problem niepotrzebnego załączania i natychmiastowego wyłączania przekaźnika przy pierwszej komendzie „otwórz”, gdy przekaźnik po resecie znajdował się już w stanie otwartym (0).
  - W `io_manager_init()` stan `s_relay_last_target`, `s_relay_confirmed = true` oraz poziom wyjścia PA8 są natychmiast synchronizowane z zapamiętaną pozycją odczytaną z pinów.
  - W `io_manager_setRelayState()` wprowadzono regułę: jeśli fizyczny stan styków odpowiada stanowi zadanemu (`current_fb == target_state`), funkcja natychmiast wyrównuje pin bez generowania zbocza przeciwnego, zapobiegając niepotrzebnemu zużyciu styków i klikaniu przekaźnika.
- **Błędna wartość zwracana przez `io_manager_getRelayState()` (`app/io/io_manager.c`):**
  - Zmiana zwracania maski bitowej `btn & (1 << 4U)` (wartość `16` / `0x10`) na znormalizowane `0` lub `1` (`(btn & (1U << 4U)) != 0U`). Poprzednio warunek `relay_fb != relay_state` zawsze ewaluował się do prawdy przy stanie załączonym (`16 != 1`).
- **Sterowanie przekaźnikiem i zgodność z Regułą 13 w parserze V-Box (`app/can_bus/can_parser/parsers/cp_v_box.c`):**
  - Rozwiązanie problemu desynchronizacji pinu PA8 (stan LOW po resecie) z bistabilnym przekaźnikiem pamiętającym stan załączenia po zaniku zasilania.
  - Usunięcie wywołania `CAN_msg_get_can_id()` w procedurze odbiorczej RX (zastąpione przez `msg->ident & CAN_EFF_MASK` zgodnie z Regułą 13 wytycznych).
  - Dodanie kontroli długości ramki CAN (`msg->len >= 3U`) przed odczytem bajtu `msg->data[2]`.

- **Optymalizacja synchronizacji bitowej FDCAN i stromości zboczy TX (`app/port/stm32c5/stm32c5_can_adapter.c`, `generated/hal/mx_fdcan1.c`):**
  - Zwiększono parametr `nominal_jump_width` (SJW) z minimalnej wartości 1 (6.94 ns) do wartości równej `nominal_time_seg2` (36 tq = 250 ns dla 500 kbps), co umożliwia pełną resynchronizację fazy zegara i zapobiega błędom braku potwierdzenia (ACK Error / `TEC 120`) zgłaszanym przez zewnętrzne analizatory magistrali (np. Vehicle Spy).
  - Przestawiono prędkość wyprowadzeń GPIO FDCAN1 (PB8/PB9) z domyślnego `HAL_GPIO_SPEED_FREQ_LOW` na `HAL_GPIO_SPEED_FREQ_VERY_HIGH` w celu eliminacji opóźnień zboczy na wąskim impulsie bitu ACK.

## [2026-10-13]
### Dodane
- **Warstwa BSP dla I/O oraz ADC (`app/port/bsp_io.h`, `app/port/stm32c5/bsp_io.c`, `app/port/bsp_adc.h`, `app/port/stm32c5/bsp_adc.c`):**
  - Hermetyzacja niskopoziomowego sterowania 8 przyciskami na GPIOB (`bsp_io_buttons_read()`), wyjściem cyfrowym PA8 (`bsp_io_output_setPin()`, `bsp_io_output_get_state()`) oraz przetwornikiem ADC1 na pinie PA0 (`bsp_adc_read_raw()`).
- **Menedżer wejść/wyjść FreeRTOS (`app/io/io_manager.c`, `app/io/io_manager.h`):**
  - Utworzenie dedykowanego zadania `io_manager` próbkującego wejścia co 10 ms.
  - Implementacja równoległego debouncingu 8 przycisków w oknie 40 ms (4 kolejne zgodne próbki) przy użyciu operacji bitowych na buforze kołowym.
  - Liniowe mapowanie surowego odczytu 12-bit ADC (0..4095) na wartości procentowe 0..100%.
  - Okresowa wysyłka ramki CAN co 100 ms z rozszerzonym identyfikatorem `0x01F2F3F4` z aktualnym stanem przycisków, wartością ADC oraz licznikiem transmisji.
  - Czytelne raportowanie statusu na UART przez `safe_printf` co 500 ms oraz natychmiastowo przy każdej zmianie stanu przycisków (znacznik `*`).
- **Filtry sprzętowe CAN oraz callbacki odbiorcze (`app/port/can_port.h`, `app/port/stm32c5/stm32c5_can_adapter.c`):**
  - Dodanie kontraktu struktury filtrów `can_filter_t` (ID `arb`, maska `mask`, wskaźnik callbacku `cb`) oraz wskaźnika `set_filter` w `can_interface_t`.
  - Sprzętowa implementacja filtrowania klasycznego (`HAL_FDCAN_FILTER_TYPE_CLASSIC`) w adapterze STM32C5 dla identyfikatorów 11-bit Standard i 29-bit Extended z konfiguracją rejestru `FDCAN_RXGFC` oraz odrzucaniem niepasujących ramek (`HAL_FDCAN_NO_MATCH_REJECT`).
- **Dyspozytor odbiorczy CAN (`app/can_bus/can_manager.c`, `app/can_bus/can_manager.h`):**
  - Dodanie funkcji `can_manager_setFilter()` przekazującej filtry do adaptera sprzętowego i zapamiętującej je w instancji `can_instance_t`.
  - Wdrożenie automatycznego dopasowywania odebranych ramek do filtrów i wywoływania dedykowanego callbacku `cb(&rx_msg)` w kontekście zadania FreeRTOS `can_rx_task`.
- **Moduł parsera CAN (`app/can_bus/can_parser/can_parser.c`, `app/can_bus/can_parser/can_parser.h`):**
  - Utworzenie dedykowanego modułu parsera w podkatalogu `app/can_bus/can_parser/`.
  - Wprowadzenie architektury profili parserów opartej na czystych identyfikatorach numerycznych (`can_parser_id_t`) bez narzutu pamięciowego ciągów znaków (Flash/RAM: 8 bajtów na profil).
  - Zdefiniowanie tablicy profili `s_parsers` z osobnymi regułami: `CAN_PARSER_ID_0` (przekaźnik `0x7FF` i telemetria I/O `0x01F2F3F4`) oraz `CAN_PARSER_ID_1` (diagnostyka `0x7E0` i kalibracja `0x18DAF110`).
  - Implementacja funkcji dynamicznego przełączania w locie `can_parser_select(&can1_inst, id)` oraz inicjalizacji profilu domyślnego w `main.c`.
- **Modularne parsery CAN (`app/can_bus/can_parser/parsers/cp_v_box.c`, `cp_v_box.h`, `cp_example.c`, `cp_example.h`):**
  - Wydzielenie logiki i callbacków poszczególnych profili do dedykowanych plików w podkatalogu `parsers/` według konwencji nazewnictwa `cp_*.c` / `cp_*.h`.
  - Wdrożenie wzorca profilu z getterem (`cp_v_box_profile_get()`, `cp_example_profile_get()`) eliminującego słowo kluczowe `extern` – filtry oraz deskryptor `can_parser_profile_t` są hermetyzowane jako `static const` wewnątrz pliku implementacji danego parsera.
  - Automatyczne zliczanie reguł filtrów w kompilacji (`sizeof(s_filters) / sizeof(s_filters[0])`) bez potrzeby ręcznego utrzymywania stałych `#define CP_*_COUNT`.
  - Profil V-Box (`cp_v_box`): obsługa komend przekaźnika (`0x7FF`) oraz telemetrii I/O (`0x01F2F3F4`).
  - Profil Example (`cp_example`): demonstracyjna obsługa żądań diagnostycznych OBD2/UDS (`0x7E0`) oraz kalibracji (`0x18DAF110`).
- **Przewodnik tworzenia parserów CAN (`docs/can_bus/can_parser_guide.md`):**
  - Kompletna instrukcja krok po kroku opisująca tworzenie nowych parserów w `app/can_bus/can_parser/parsers/` według wzorca profilu z getterem, strukturę plików `cp_*.c`/`cp_*.h`, hermetyzację `static const`, rejestrację w `can_parser.h` / `can_parser.c` oraz dynamiczną aktywację przez `can_parser_select()`.
  - Aktualizacja pliku indeksującego `docs/can_bus/README.md`.
- **Automatyczny generator parserów CAN (`tools/create_parser.py`):**
  - Narzędzie w języku Python automatyzujące generowanie szkieletu nowego parsera (`cp_<nazwa>.h`, `cp_<nazwa>.c`) z szablonem filtrów, callbacków i instrukcjami TODO.
  - Automatyczna rejestracja identyfikatora `CAN_PARSER_ID_<NAZWA>` w `can_parser.h` (bez aliasów), rejestracja wskaźnika gettera w `s_parsers` w `can_parser.c` oraz dopisanie pliku źródłowego do `CMakeLists.txt`.
  - Wbudowana walidacja kolizji nazw, istniejących plików i enumów oraz obsługa trybu `--dry-run`.
- **Konfiguracja zadań (`app/tasks_config.h`):** Dodano definicje priorytetu `TASK_PRIO_IO_MANAGER` oraz rozmiaru stosu `TASK_STACK_IO_MANAGER` (512 słów).
- **Wytyczne architektoniczne :**
  - Dodano regułę 12 definiującą standard tworzenia i rejestracji parserów CAN oraz wzorzec profilu z getterem bez `extern`.
  - Dodano regułę 13: ścisły nakaz używania funkcji pomocniczych `CAN_msg_get_can_id`, `CAN_msg_is_extended_id`, `CAN_msg_set_extended_id`, `CAN_msg_set_standard_id` wyłącznie podczas tworzenia struktury `CAN_msg_t` do wysłania (TX) oraz zakaz ich stosowania przy odbiorze (RX).

### Zmienione
- **Refaktoryzacja dyspozytora parsera (`app/can_bus/can_parser/can_parser.c`):** Tablica `s_parsers` przechowuje teraz bezpośrednio wskaźniki na funkcje-gettery profili (`can_profile_getter_fn`), co obniża zużycie pamięci Flash do 4 bajtów na profil i uniezależnia dyspozytor od globalnych zmiennych poszczególnych modułów.
- **Logika testowa w `main_task` (`generated/middleware/mx_freertos_app.c`):** Usunięto/zakomentowano testowe wysyłanie sztucznych ramek CAN – ruch na magistrali CAN1 jest teraz generowany bezpośrednio przez moduł `io_manager`.

## [2026-10-12]
### Dodane
- **Weryfikacja sprzętowa 1x ADC Input (PA0 / ADC1_IN0):** Włączono moduł HAL ADC w `stm32c5xx_hal_conf.h` oraz dodano sterownik `stm32c5xx_hal_adc.c` do `CMakeLists.txt`. Zaimplementowano funkcje testowe `test_adc_init()`, `test_adc_read_raw()` oraz `test_adc_read_mapped()` (skalowanie 12-bit 0..4095 do 0..100%) na pinie PA0 (Arduino A0 / CN8 pin 1 / ST-Morpho CN7 pin 28) oraz logowanie wyników w `main_task`.
- **Weryfikacja sprzętowa 1x GPIO Output (PA8):** Dodano funkcje `test_output_init()`, `test_output_toggle()` oraz `test_output_get_state()` w `main.c`/`main.h` (pin PA8 / Arduino D7 / CN10 pin 23 jako wyjście Push-Pull) oraz cykliczne przełączanie stanu wyjścia w `main_task`.
- **Weryfikacja sprzętowa 8 GPIO Input (Wariant B na GPIOB):** Wdrożono funkcje testowe `test_buttons_init()` i `test_buttons_read()` w `main.c`/`main.h` (piny PB4..PB7 oraz PB12..PB15 z wewnętrznym `HAL_GPIO_PULL_DOWN` na złączu CN10) oraz cykliczne logowanie ich stanów w `main_task` (`mx_freertos_app.c`). Zaktualizowano mapowanie fizyczne pinu PB5 na CN10 pin 6 (zgodnie z konfiguracją SB22 w rewizji C5).
- **Warstwa abstrakcji sprzętowej portu CAN (`app/port/can_port.h`):** Wprowadzono czysty kontrakt C definiujący interfejs operacji na kontrolerach CAN/FDCAN (`init`, `transmit`, `receive`, `attach_rx_callback`) bez zależności od STM32 HAL i FreeRTOS.
- **Adapter sprzętowy dla STM32C5 (`app/port/stm32c5/stm32c5_can_adapter.c`, `stm32c5_can_adapter.h`):** Implementacja adaptera FDCAN z hermetyzacją statyczną i wzorcem gettera `stm32c5_can1_interfaceGet()`.
- **Wieloinstancyjny Menedżer CAN (`app/can_bus/can_manager.c`, `can_manager.h`):** Obiektowa architektura `can_instance_t` z niezależnymi zadaniami TX/RX per magistralę, kolejką nadawczą oraz semaforem odbiorczym sterowanym przerwaniem (ISR).
- **Przewodnik portowania i architektury (`docs/can_bus/can_porting_guide.md`):** Kompletna dokumentacja architektury warstwowej CAN, diagramy sekwencji i instrukcja dodawania kolejnych instancji.
- **Konfiguracja zadań FreeRTOS (`app/tasks_config.h`):** Centralizacja priorytetów i rozmiarów stosu dla zadań aplikacji.

### Zmienione
- **Uproszczenie API `can_manager_init`:** Ograniczenie liczby parametrów do 3 (`can_manager_init(&inst, hw_if, "NAME")`) dzięki osadzeniu konfiguracji `can_config` w `can_instance_t`. W przypadku niewypełnienia konfiguracji stosowane są domyślne parametry (500 kbps Classic CAN).
- **Wytyczne architektoniczne :** Dodano regułę 10 (komentarze wyłącznie po angielsku + Doxygen) oraz regułę 11 (aktualizacja `docs/changelog.md`).

### Naprawione
- **Priorytety przerwań NVIC (FreeRTOS):** Poprawiono priorytet przerwania FDCAN1 z domyślnego 0 na bezpieczny priorytet 5 (`HAL_CORTEX_NVIC_PREEMP_PRIORITY_5`), eliminując ryzyko `configASSERT` przy wywołaniach `FromISR`.
- **Logika Bit Rate Switching (BRS) w CAN FD:** Naprawiono warunek załączania BRS. Tryb BRS (`HAL_FDCAN_FRAME_FORMAT_FD_BRS` oraz bit `bit_rate_switch = ON`) jest aktywowany wyłącznie, gdy `baudrate_FD > baudrate_normal`. Gdy prędkości są równe (`baudrate_FD == baudrate_normal`), magistrala pracuje bezpiecznie w standardzie CAN FD bez BRS (`HAL_FDCAN_FRAME_FORMAT_FD_NO_BRS` i `bit_rate_switch = OFF`), obsługując ramki do 64 bajtów bez niepotrzebnego przełączania zegara.

## [2026-10-09]
### Dodane
- Moduł logowania `safe_printf` zabezpieczony za pomocą FreeRTOS Mutex (`printf_mutex`), uodporniony na kolizje pomiędzy wątkami.
- Mechanizm obowiązkowych tagów (znaczników) dla wiadomości logowania w `safe_printf` (np. `safe_printf("MAIN", "msg");`).
- Pełna obsługa wysyłki ramek FDCAN (`can1_tx_task`) przy użyciu specyficznych struktur nowego HALa (rodzina STM32C5xx, m.in. użycie struktury bitowej `hal_fdcan_tx_header_t` oraz funkcji `HAL_FDCAN_ReqTransmitMsgFromFIFOQ`).
- Kolejka wiadomości `CAN_1_queue` wykorzystująca ujednolicony typ danych `CAN_msg_t`.
- Moduł `can_bus` został dodany do list ścieżek include w `CMakeLists.txt`.
- Generowanie testowych ramek CAN w pętli `main_task`.

### Zmienione
- Naprawa procesu budowania w `.vscode/tasks.json` na systemie Windows (zmiana na `"type": "process"` unikająca błędu interpretacji ścieżek z backslashami).
- Rozszerzenie pamięci sterty FreeRTOS (`configTOTAL_HEAP_SIZE`) z 8 KB do 16 KB w celu usunięcia problemów z brakiem pamięci (HardFault / errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) po dodaniu nowych kolejek i zwiększeniu stosów.
- Poprawienie inicjalizacji środowiska uruchomieniowego RTOS – wywołanie `app_synctasks_init()` z poziomu `main.c`.
- Zwiększenie rozmiarów stosu dla poszczególnych zadań (np. `can1_tx_task` do 512 słów) z uwagi na duże narzuty zmiennych lokalnych i wywołań w standardowej bibliotece `printf`.

### Rozwiązane Problemy
- Ustalenie przyczyny HardFault wywoływanego przez brak pamięci stosu i nieprawidłową inicjalizację funkcji systemowych przed odpaleniem schedulera RTOS.
- Dostosowanie starych nawyków z serii G4 (typy np. `UART_HandleTypeDef`, stare funkcje CAN) do nowej biblioteki HAL dostarczanej z rodziną STM32C5xx.

