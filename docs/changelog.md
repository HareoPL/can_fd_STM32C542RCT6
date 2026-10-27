# Changelog

Wszystkie znaczące zmiany w projekcie będą dokumentowane w tym pliku.

## [2026-10-27]
### Dodane
- **Silnik symulatorów aut (`app/car_sim/car_sim.c`, parser `cp_car_sim`, `CAN_PARSER_ID_CAR_SIM`):** task `car_sim` (co 10 ms): stan wejść (`io_manager` + `io_sim`), przebieg rosnący z prędkością, ramki aktywnego symulatora z harmonogramu, ramki odrzucone przez pełną kolejkę TX liczone i logowane najwyżej co 5 s. Wybór symulatora tylko w tasku (żądania z RS485 i UART): konfiguracja CAN1 z profilu auta (filtry z unikalnych ID komend), po każdym wyborze filtry `BOOT_OTA` (D9), przy błędzie powrót do poprzedniego symulatora, log `[CAR_SIM] symulator: ...`. Komendy LX z CAN1 (`car_sim_on_rx()`, kontekst `can_rx_task`, bez blokowania): przekaźnik przez `io_manager_setRelayStateAsync()` (`AWARIA_ZAMKA#1` blokuje jak dotąd), zły PIN → brak akcji i `WARN` z odebranym PIN-em (D8). Tryb `AUTO` / `RECZNY`, PIN v-box w RAM, żądanie pilota, `car_sim_get_info()`. Start kontroluje rejestr i pojemność filtrów CAN1 każdego symulatora razem z `BOOT_OTA` (przekroczenie = błąd startu). Wolna sterta FreeRTOS logowana przy starcie tasku.
- **Symulator `V_BOX` (`app/car_sim/sims/sim_v_box.c`, parser urządzenia 192, D1):** telemetria `0x01F2F3F4` bajt w bajt jak dotychczas (`data[2]` z `rpm_in` bez reguły D13, licznik BE w `data[4..7]`, zerowany przy wyborze symulatora) i komenda przekaźnika `0x01F2F3F0` (`data[2] & 0x01`, ramka krótsza niż 3 B ignorowana).
- **Rejestr symulatorów aut i symulator `BRAK` (`app/car_sim/car_sim_registry.c`, `app/car_sim/sims/sim_none.c`):** rejestr getterów profili `s_sims[]` (wzorzec `s_parsers[]`, zero `extern`), mapa „numer parsera urządzenia → symulator” (`car_sim_find_by_dev_parser()`, numer bez symulatora i 0 → `BRAK`), wyszukiwanie po kluczu bez rozróżniania wielkości liter, kontrola spójności (`car_sim_registry_check()`: poprawność profili, unikalne klucze, numer parsera urządzenia w jednym symulatorze). `BRAK` (D4): jedyna ramka `0x01F2F3FF` ext, DLC 6, `CA FE 00 BA BE 00` co 1000 ms, CAN1 Classic 500 kbps, bez komend i pilota.
- **Model stanu auta, koder sygnałów i harmonogram ramek (`app/car_sim/car_sim_types.h`, `app/car_sim/car_sim_codec.c`):** deklaratywne profile aut w czystym C (ramki i sygnały jak w pliku DBC, komendy LX z PIN-em, pilot), `vehicle_state_build()` z regułą D13 (bez stacyjki obroty 0, surowe `rpm_in` dla `V_BOX`), kodowanie `BIT` / `UINT` BE / LE z `mul` / `div` / `offset` / `shift` i nasyceniem do `max` (np. bez kodu „brak danych”), hook `encode`, dopasowanie komend z maską bajtów i ścisłą kontrolą PIN-u (D8), ramka pilota z PIN-em, harmonogram rozkładający fazy ramek co 10 ms, walidacja profilu (`car_sim_profile_check()`).
- **Test hostowy symulatorów aut (`tools/test_car_sim_host.py`, `tools/host/car_sim_cli.c`):** koder (wszystkie kodowania, D13, hook), komendy z PIN-em, pilot, harmonogram (13 ramek 100 ms: każda 10 razy na 1 s, w ticku najwyżej 2), rejestr i mapa parserów urządzenia, `BRAK`, wykrywanie błędnych profili.
- **Sygnały auta w symulacji wejść (`io_sim.c`, `io_cmd.c`):** komendy `PREDKOSC#<0..250>`, `PRZEBIEG#<0..999999>` (km, domyślnie 10000), `DROGOWE#<0|1>`, `HAMULEC_NOZNY#<0|1>`, `TEMP_SILNIKA#<-40..150>` (domyślnie 90) – tylko przy `ZRODLO=UART`; przy `ZRODLO=IO`, po przejęciu przez UART i po `IO_RESET` wartości domyślne. `io_sim_advanceOdometer()` dla przebiegu rosnącego z prędkością.

### Zmienione
- **Telemetria v-box nadawana przez symulator `V_BOX`:** `io_manager_task` nie nadaje już ramki `0x01F2F3F4` (usunięte `io_manager_canCreateFrame()`), odświeża tylko paliwo z ADC co 100 ms; log `IO_MGR` bez zmian. CAN1 konfiguruje `car_sim_init()` zamiast `can_parser_select(CAN_PARSER_ID_V_BOX)` (`cp_v_box` zostaje w rejestrze, nieużywany). Komenda przekaźnika logowana z tagiem `[CAR_SIM]` zamiast `[CP_V_BOX]`, treść bez zmian. Build `CAN_FD_TEST`: CAN1 należy do testu, symulator nadaje tylko `V_BOX`.
- **Zmiana parsera urządzenia z RS485:** numer parsera CAN1 przekazywany do symulatora aut (`car_sim_request_dev_parser()`); w trybie `AUTO` wybór pasującego symulatora, numer bez symulatora (także 0) → `BRAK` z logiem `WARN`. Utrata łącza RS485 nie zmienia symulatora. Numer CAN2 tylko logowany.
- **Limity obrotów z aktywnego symulatora auta (`io_sim_setRpmLimits()`):** zakres i krok `OBROTY#` oraz obroty SW8 (bieg jałowy) ustawiane z profilu; domyślnie wartości `V_BOX` (0..750, krok 3, SW8 = 750), więc komunikaty i ramka `0x01F2F3F4` bez zmian. Przy zmianie limitów obroty z UART są przycinane do maksimum i zaokrąglane w dół do kroku. `ZRODLO?`, `IO_HELP`, `IO_RESET` i odpowiedź `OBROTY#` pokazują sygnały auta tylko dla symulatora, który je nadaje (`io_sim_setVehicleSignalsUsed()`); dla `V_BOX` i `BRAK` teksty bez zmian.

## [2026-10-26]
### Poprawione
- **Nazewnictwo wejść:** SW1 (PC8) = Drzwi INNE, SW2 (PC7) = Drzwi KIER, SW5 (PB13) = Awaryjne, SW6 (PB14) = Maska, SW8 (PB2) = Obroty silnika – w kodzie, komendach UART (`DRZWI_INNE`/`DRZWI_KIER`, `AWARYJNE`/`MASKA` sterują teraz właściwymi bitami ramki `0x01F2F3F4`) i dokumentacji. Piny i bity bez zmian.

### Dodane
- **Łącze RS485 z urządzeniem telematycznym (`app/rs485/`, opcja CMake `RS485_LINK`, domyślnie `ON`):** task `rs485` na USART1 (57600 8N1) co 5 s odpytuje superStatus urządzenia (LX, LX_micro, Roma, Viale), dekoduje odpowiedź (`superstatus_t`: wersja fw, czas, zasilanie, GPS, GSM, ICCID, IMEI, numer parsera urządzenia CAN1/CAN2) i zgłasza zmianę parsera urządzenia przez callback (`rs485_link_set_parser_change_cb()`, na razie log `[RS485] ZMIANA parsera urządzenia CAN1: A -> B`). Zapytania tylko na cichej magistrali, nasłuch pasywny przy podłączonym programie serwisowym, wstrzymanie na czas wgrywania pliku parsera, ponawianie przy braku odpowiedzi, rozpoznanie echa i błędnego hasła. Komendy konsoli `RS485?`, `RS485#POLL`, `RS485_HELP`. Kodek ramek `AA 55` + CRC-16/MODBUS z resynchronizacją (`rs485_frame.c`) i dekoder superStatus (`superstatus.c`) w czystym C. Specyfikacja protokołu: `docs/rs485/rs485_protocol.md`, moduł i test: `docs/rs485/rs485_link.md`, test hostowy: `tools/test_rs485_host.py`, podsłuch magistrali: `tools/rs485_sniffer.py`.
- **Nadawanie w kontrakcie portu UART (`uart_port.h`):** opcjonalne `transmit()` (USART2: `NULL`, log idzie przez `safe_printf()`), implementacja `HAL_UART_Transmit()` w adapterze USART1.
- **USART1 dla RS485 (PA6 TX / PA7 RX):** konfiguracja w `v_box.ioc2` i wygenerowany kod HAL, adapter `stm32c5_uart1_adapter` z callbackami rejestrowanymi na uchwycie USART1.
- **Symulacja uszkodzonego zamka i reset ustawień (`io_sim.c`, `io_cmd.c`, `io_manager.c`):** `AWARIA_ZAMKA#<0|1>` – przekaźnik nie reaguje na ramki `0x01F2F3F0` (`io_manager_setRelayState()` nie generuje zboczy, log `ignored`), bit zamków w telemetrii zostaje bez zmian. `IO_RESET` – wszystkie wejścia 0, obroty 0, paliwo 0, zamek sprawny, źródło zostaje UART. Obie komendy tylko przy `ZRODLO=UART`. `ZRODLO#IO` i reset MCU kasują awarię zamka. `IO_HELP` wyświetla tabelę komend bez polskich znaków.
- **Symulacja wejść przez UART (`app/io/io_sim.c`, `app/io/io_cmd.c`):** wszystkie wejścia ramki telemetrii `0x01F2F3F4` ustawiane komendami przez UART mostu (`STACYJKA`, `DRZWI_KIER`, `DRZWI_INNE`, `SWIATLA`, `MASKA`, `AWARYJNE`, `HAMULEC` `#<0|1>`, `OBROTY#<0..750>`, `PALIWO#<0..100>`), odpowiedź `[IO] OK` albo `[IO] ERR: <powód>`. Globalny przełącznik źródła `ZRODLO#<UART|IO>` (po resecie zawsze IO, przy przejściu na UART snapshot stanu fizycznego), `ZRODLO?`, `IO_HELP`. SW4 (zamki, sprzężenie przekaźnika) zawsze z wejścia fizycznego. Instrukcja: `docs/io/io_uart_simulation.md`, test hostowy: `tools/test_io_cmd_host.py`.

### Zmienione
- **Ramka telemetrii `0x01F2F3F4`, `data[2]`:** obroty silnika kodowane jako RPM / 3 (odbiornik mnoży ×3). W trybie UART 0..750 RPM tylko w wielokrotnościach 3, w trybie IO bez zmian (SW8: `0xFA` albo `0`).
- **Log `IO_MGR`:** źródło wejść (`SRC`), wartości wysyłane na CAN (`BTN`, `FUEL`, `RPM`) i stan fizyczny (`PHYS`, `ADC`).

## [2026-10-22]
### Dodane
- **Moduł testu CAN FD (`app/can_bus/can_fd_test/`, `cp_fd_test`, `tools/can_fd_test.py`):** opcja CMake `CAN_FD_TEST` (osobny katalog builda) – parser `CP_FD_TEST` ze zmienną konfiguracją magistrali przełączaną przez `can_parser_select()`, protokół testowy (PING, SET_CONFIG z powrotem awaryjnym po 5 s, GET_STATS, BURST, TX_PROBE, LOOP), echo wszystkich długości DLC, opcjonalna pętla CAN1↔CAN2 (`CAN_FD_TEST_LOOP`). Narzędzie PC: matryca 13 konfiguracji + testy negatywne, log UART, raport Markdown. Instrukcja: `docs/can_bus/can_fd_test_guide.md`, wyniki: `docs/can_bus/can_fd_test_report_can2.md`.
- **Diagnostyka magistrali w porcie (`can_interface_t::get_status`, `can_manager_getStatus()`):** TEC/REC, error passive / bus-off, LEC/DLEC, TDCV, akumulowany licznik błędów protokołu (ECR.CEL) oraz błędy per typ zliczane w przerwaniu PEA/PED.
- **`can_manager_waitTxIdle()`**, licznik odrzuconych ramek TX (`tx_error_cnt`) i restartów po bus-off (`bus_off_cnt`) w `can_instance_t`, przełącznik `CAN_MANAGER_TX_LOG` (logi każdej ramki TX).
- **`open_can_bus()` (`tools/can_updater.py`):** opcjonalne parametry CAN FD (`fd`, `data_bitrate`, `sample_point`, `data_sample_point`) dla adaptera candleLight.

### Naprawione
- **Brak wyjścia z bus-off:** M_CAN po bus-off zostawał w INIT na stałe (stan HAL nadal ACTIVE), węzeł przestawał nadawać do resetu. Task TX menedżera nadzoruje teraz bus-off (co 100 ms i po błędzie TX) i restartuje kontroler.
- **Wyczerpanie sterty FreeRTOS:** po zwiększeniu `CAN_msg_t` do 72 B kolejki CAN/OTA nie mieściły się w 24 KB (`app_synctasks_init failed`). `configTOTAL_HEAP_SIZE` = 28 KB, wolna sterta logowana po starcie.

## [2026-10-21]
### Dodane
- **CAN FD – konfiguracja szyny w profilu parsera (`app/can_bus/can_parser/`, `app/port/can_port.h`):** `can_parser_profile_t` ma nowe pole `bus_config` (tryb `CAN_MODE_CLASSIC` / `CAN_MODE_FD_NO_BRS` / `CAN_MODE_FD_BRS` oraz prędkości), stosowane przez `can_parser_select()` przez nową funkcję `can_manager_setBusConfig()`. Przy błędzie przywracana jest poprzednia konfiguracja. Profil `BOOT_OTA` (`bus_config = NULL`) nie zmienia trybu szyny. Usunięto ręczne ustawianie `can_config` z `main.c`.
- **Kalkulator bit timingu FDCAN (`stm32c5_can_adapter.c`):** zamiast tablic dla 144 MHz – wyliczanie z zegara FDCAN (`HAL_FDCAN_GetClockFreq` / `CKDIV`), dokładna prędkość, sample point 87.5% (nominal) / 75% (data) z tolerancją 3%, wspólny prescaler obu faz przy BRS. Obsługiwane 1/2/4/8 Mbps w fazie danych.
- **Rekord wersji Bootloadera (`app/bootloader/boot_version_record.h/.c`, `linker/stm32c542xc_boot_flash.ld`):** 32-bajtowa struktura `boot_version_record_t` (magic `"VBVR"`, wersja z tagu `boot-vX.Y.Z`, hash commita) pod stałym adresem `0x08000200`. Aplikacja raportuje wersję zainstalowanego Bootloadera w bannerze UART i w nowej ramce Discovery `0x03` (pełne 8-bitowe wersje aplikacji i Bootloadera), a przed instalacją Bootloadera ze Staging Slot loguje wersję wgrywanego obrazu. Narzędzia PC (`can_updater.py`, GUI) wyświetlają `FW vX.Y.Z | BL vX.Y.Z`; węzły bez ramki `0x03` są nadal obsługiwane.
- **ISO-TP – konfigurowalna kontrola przepływu (`app/can_bus/isotp/`):** nowe API `isotp_set_flow_control(link, block_size, st_min)`, obsługa `BlockSize` po stronie odbiorczej (kolejna ramka `FC CTS` po każdych BS ramkach `CF`) oraz rozdzielenie buforów na `ISOTP_RX_BUF_SIZE` (1040 B) i `ISOTP_TX_BUF_SIZE` (64 B).
- **Protokół OTA (`app/ota_update/ota_protocol.h`):** stałe `OTA_MAX_BLOCK_SIZE` (1024 B) i `OTA_WRITE_PDU_OVERHEAD` (11 B) z asercją `_Static_assert` względem bufora ISO-TP.
- **Plan wdrożenia:** `docs/bootloader/ota_fast_transfer_plan.md`.

### Zmienione
- **Wersjonowanie firmware – jedno źródło prawdy: tag git (`CMakeLists.txt`, `cmake/version.cmake`):**
  - Usunięto ręcznie utrzymywane `FW_VERSION_MAJOR/MINOR/PATCH` z `CMakeLists.txt`. Wersja jest wyliczana z ostatniego tagu `vX.Y.Z` (`git describe --tags --match "v[0-9]*" --abbrev=0`), a `FW_GIT_DESCRIBE` używa tego samego dopasowania tagów.
  - `version_config.h` generowany jest przy każdym buildzie przez target `generate_version` (wcześniej tylko przy configure, co dawało nieaktualny hash/describe). Plik jest nadpisywany wyłącznie przy zmianie stanu git, więc build przyrostowy nie rekompiluje niepotrzebnie.
  - Brak repozytorium lub tagu skutkuje wersją `v0.0.0` i ostrzeżeniem CMake. Komendy git używają `-c safe.directory=*` (build w kontenerze Docker).
  - **Bootloader – osobna linia wydań z tagów `boot-vX.Y.Z`:** target `generate_boot_version` generuje `boot_version_config.h` tym samym skryptem (parametr `TAG_PREFIX`). Usunięto trzy ręcznie wpisane i niespójne wersje Bootloadera: `PACKED_BOOT_VERSION 0x12` (Discovery), tekst `v1.2` w bannerze UART oraz `v1.0.0` w odpowiedzi na `CMD_SELECT_TARGET`. Banner wyświetla dodatkowo `git describe` i czas builda.

- **Przyspieszenie transferu OTA na klasycznym CAN 500 kbps (szacunkowo ~20 s → ~3–5 s dla obrazu 63.7 KB):**
  - **Bootloader v1.2 (`app/bootloader/`):** Flow Control `BS=0, STmin=0` (wcześniej `STmin=2 ms`), bufor odroczonego zapisu wektorów `s_deferred_vector_block` oraz bufor zapisu Flash powiększone do `OTA_MAX_BLOCK_SIZE`, odrzucanie bloków > 1024 B, sprzętowy filtr FDCAN przepuszczający wyłącznie ID OTA (`0x01F2F3E0`, `0x01F2F3DF`), licznik utraconych ramek RX FIFO0 (`RF0L`) raportowany przy `CMD_VERIFY_FIRMWARE`, `PACKED_BOOT_VERSION = 0x12`.
  - **Aplikacja (`app/ota_update/`):** Flow Control `BS=16, STmin=0` chroniący kolejkę `ota_rx_queue` (64 ramki) przed przepełnieniem, bufor zapisu slotu staging powiększony do `OTA_MAX_BLOCK_SIZE`, odrzucanie bloków > 1024 B.
  - **Narzędzia PC (`tools/can_updater.py`, `tools/can_updater_gui.py`):** dekodowanie `STmin` wg ISO 15765-2 (0–127 ms oraz 100–900 µs), usunięcie sztywnego minimum 2 ms między ramkami `CF`, precyzyjne odstępy `perf_counter` (busy-wait poniżej 2 ms), ponawianie nadania przy chwilowo pełnym buforze adaptera, domyślny blok 1024 B (`--block-size` z walidacją 8–1024).
  - Wymagana jednoczesna aktualizacja Bootloadera i Aplikacji (brak zgodności wstecz z Bootloaderem v1.1 dla bloków > 512 B).
- **Dynamiczne rozszerzanie tabeli wykrytych węzłów STM32 w GUI (`tools/can_updater_gui.py`):**
  - Usunięto sztywne limity wysokości grupy i tabeli (`grp.setMaximumHeight(160)` oraz `table_devices.setMaximumHeight(105)`), które ograniczały widok listy do zaledwie ~1.5 wiersza.
  - Ustawiono współczynnik rozciągania `stretch=1` oraz politykę `Expanding` dla panelu tabeli urządzeń (`TABELA WYKRYTYCH WĘZŁÓW STM32`), dzięki czemu powiększenie lub maksymalizacja okna aplikacji w całości dynamicznie rozszerza tabelę węzłów (umożliwiając wygodny podgląd wielu urządzeń w sieci).
  - Ustabilizowano pozostałe segmenty interfejsu (panel połączenia, kafelki dashboardu KPI, panel wyboru wsadu i potoku wykonania oraz terminal logów) na stałych, czytelnych pozycjach (`stretch=0`), zapobiegając ich niepożądanemu rozsuwaniu.
  - Włączono naprzemienne kolorowanie wierszy tabeli (`setAlternatingRowColors(True)`), zablokowano przypadkową edycję komórek (`NoEditTriggers`) oraz dodano dedykowane style w ciemnym motywie inżynierskim (`DARK_ENGINEERING_THEME`) dla `QTableWidget` i `QTableWidget::item`.

### Naprawione
- **CAN FD z BRS nie działał przy 2–8 Mbps:** brak Transmitter Delay Compensation. TDC jest teraz włączane przy prescalerze danych ≤ 2 (`TDCO = presc × TSEG1`), po zapisie DBTP, który kasuje bit `TDC`.
- **Przepełnienie bufora przy nadawaniu ramek FD:** `CAN_msg_t.data` miało 54 B, a DLC 64 powodował odczyt 10 B poza buforem; przy odbiorze ramki 64 B były obcinane. `data` ma teraz 64 B (`CAN_msg_t`: 72 B).
- **Format ramki per wiadomość:** `transmit`/`receive` w `can_interface_t` przyjmują `CAN_msg_t*`. Nadawanie używa `msg->format` (odpowiedzi OTA pozostają Classic na szynie FD, ramka FD na szynie Classic jest odrzucana), a odbiór raportuje faktyczny format odebranej ramki (bit FDF) zamiast formatu szyny.
- **Brak obsługi `CAN_BAUDRATE_8000K`** i niejawne wnioskowanie BRS z `baudrate_FD > baudrate_normal` – zastąpione jawnym `can_frame_mode_t`.
- **Znacznik czasu builda (`FW_BUILD_TIMESTAMP`):** czas był generowany w strefie lokalnej, a opisywany jako `UTC`; teraz faktycznie jest w UTC.
- **Bezpieczne zamykanie magistrali `CandleBus` oraz eliminacja ostrzeżeń i wyjątków podczas zamykania GUI (`tools/can_updater.py`, `tools/can_updater_gui.py`):**
  - Wyeliminowano błąd `CandleBus was not properly shut down` oraz wyjątek `Exception ignored in: <function BusABC.__del__>` (powodowany przez nieosłonięte wywołanie `[ch.reset() for ch in self._channels.values()]` w `candle_bus.py` podczas destrukcji obiektów przez Garbage Collector Pythona).
  - Wdrożono wrapper `_wrap_candle_bus_safe_shutdown(bus)` w `tools/can_updater.py`, który zapewnia idempotentne wywołanie `bus.shutdown()`, bezpieczny reset kanałów w bloku `try...except` bez rzucania wyjątków w procedurze odśmiecania pamięci oraz wczesne ustawienie flagi `_is_shutdown = True`.
  - W wątku `CanBusMonitorWorker` wprowadzono metodę `_cleanup_bus()` oraz objęto pętlę roboczą blokiem `try ... finally`, gwarantując natychmiastowe i bezwyjątkowe zwalnianie interfejsu CAN przy zatrzymaniu wątku.
  - W procedurze `closeEvent` oraz rozłączaniu magistrali `_toggle_can_connection` zaimplementowano bezpieczne oczekiwanie na zakończenie wątku monitorującego (`wait(2000)`) i zwolnienie zasobów USB przed zamknięciem okna.

## [2026-10-20]
### Dodane
- **Bezkolizyjny protokół Automotive-Safe OTA (29-bit Extended ID `0x01F2F3xx` + Token `"VBOX"`) (`app/ota_update/ota_protocol.h`, `cp_boot_ota.c`, `ota_manager.c`, `openbl_core.c`):**
  - Przeniesienie komunikacji Bootloadera i OTA ze standardowych 11-bitowych identyfikatorów OBD-II (`0x7DF`, `0x7E0`, `0x7E8`, `0x7E9`) do dedykowanej 29-bitowej przestrzeni Extended ID projektu V-Box (`0x01F2F3DF`, `0x01F2F3E0`, `0x01F2F3E8`, `0x01F2F3E9`), eliminujące ryzyko kolizji z fabrycznymi sterownikami silnika/skrzyni biegów (ECM/TCM) oraz skanerami diagnostycznymi w pojeździe.
  - Wprowadzenie 4-bajtowego klucza autoryzacyjnego ASCII `"VBOX"` (`0x56, 0x42, 0x4F, 0x58`) w ramce rozgłoszeniowej `CMD_DISCOVER_NODES` (`0x01F2F3DF`). Ramki bez poprawnego podpisu są natychmiast odrzucane.
- **Wsparcie Dual-Bus (`CAN1` + `CAN2`) oraz funkcja `can_parser_attach_boot_ota()` dla płytek bez transceivera `CAN2` (`can_parser.h`, `can_parser.c`, `can_manager.h`, `can_manager.c`, `main.c`, `boot_shared.h`, `boot_shared.c`):**
  - Dodano funkcję kompozytową `can_parser_attach_boot_ota(&can1_inst)` umożliwiającą dołączenie filtrów `CAN_PARSER_ID_BOOT_OTA` równolegle do aktywnego profilu `CAN_PARSER_ID_V_BOX` na głównej magistrali `CAN1`.
  - Dodano śledzenie aktywnej instancji odbiorczej `can_manager_get_active_rx_instance()` w `can_manager.c`. Menedżer `ota_manager` automatycznie odpowiada wyłącznie na tym interfejsie (`CAN1` lub `CAN2`), na którym odebrał zapytanie, zapobiegając zapychaniu bufora nadawczego na nieobsadzonym porcie `CAN2`.
  - Rozszerzono strukturę `boot_shared_ram_t` w `SHARED_RAM` (`0x2000FFC0`) o pole `active_can_bus` (`BOOT_CAN_BUS_AUTO`, `BOOT_CAN_BUS_FDCAN1`, `BOOT_CAN_BUS_FDCAN2`) przekazywane podczas przejścia z aplikacji do Bootloadera (`CMD_ENTER_BOOTLOADER`), a w Bootloaderze (`openbl_core.c`) uruchomiono jednoczesny nasłuch na `FDCAN1` oraz `FDCAN2`.

### Zmienione
- **Cichy Tryb Ratunkowy (Silent Recovery Mode – Zero Unsolicited Traffic) (`app/bootloader/boot_main.c`, `app/bootloader/openbl_core.c`):**
  - Usunięto spontaniczne, cykliczne nadawanie ramki alarmowej `0xEE` co 1000 ms w `boot_main.c`, gwarantując 100% ciszy na magistrali CAN pojazdu nawet w stanie awarii aplikacji lub pustej pamięci Flash.
  - Ramka diagnostyczna `0xEE` (`CRASH_RECOVERY_BEACON` z przyczyną awarii, licznikiem błędów i adresem `PC`) oraz status `OTA_MODE_RECOVERY (0x02)` są teraz wysyłane **wyłącznie na żądanie (on-demand)** w odpowiedzi na autoryzowaną ramkę Discovery (`0x01F2F3DF` z tokenem `"VBOX"`).
- **Aktualizacja narzędzi PC, wsparcie adapterów `candleLight` (WinUSB / `gs_usb`) oraz pakietu testów Fault-Injection (`tools/can_updater.py`, `tools/can_updater_gui.py`, `tools/test_ota_suite.py`, `tools/requirements_ota.txt`):**
  - Dodano pełną obsługę adapterów CANable 2.0 z firmwarem **`candleLight`** (natywny sterownik WinUSB przez `candle` / `candle_api` oraz automatyczny fallback `gs_usb` przez `libusb_package`) obok dotychczasowego trybu `slcan` (wirtualny port COM) oraz `socketcan`.
  - Wprowadzono funkcję `list_candle_devices()` oraz `open_can_bus()` z automatycznym wykrywaniem podłączonego adaptera `candleLight` (`candle:0:0`) przy starcie GUI, CLI oraz testów automatycznych, wraz z filtrowaniem sprzętowych ramek echa TX (`msg.is_rx == False`).
  - Dostosowano stos ISO-TP, skaner węzłów oraz monitor GUI do 29-bitowych ramek Extended ID (`is_extended_id=True`), tokenu `"VBOX"` oraz odbioru ramek `0xEE` na żądanie.
  - Rozbudowano pakiet testowy `tools/test_ota_suite.py`: dodano weryfikację odporności na kolizje w pojeździe (ignorowanie 11-bitowego `0x7DF` oraz nieautoryzowanych ramek `0x01F2F3DF` w Test 1) oraz weryfikację ciszy na magistrali (1.5 s pasywnego nasłuchu = 0 ramek) i odpowiedzi `0xEE` on-demand w Test 2.

### Naprawione
- **Wielowęzłowy mechanizm bezkolizyjnego Discovery na wspólnej magistrali CAN (eliminacja Race Condition i Bit Error) (`ota_protocol.h`, `ota_manager.c`, `openbl_core.c`, `tools/can_updater.py`, `tools/can_updater_gui.py`, `tools/test_ota_suite.py`):**
  - **Unikalne identyfikatory arbitrażu CAN dla odpowiedzi węzłów:** Wprowadzono kodowanie 8-bitowego skrótu UID węzła (`uid_hash`) w bajcie 1 29-bitowego identyfikatora odpowiedzi Discovery: `CAN_ID_OTA_DISCOVERY_RESP_NODE(hash) = 0x01F200E9 | (hash << 8)`. Dzięki temu w przypadku jednoczesnej transmisji dwóch lub więcej węzłów sprzętowy kontroler CAN bezkolizyjnie rozstrzyga arbitraż w polu identyfikatora (Arbitration Field), całkowicie eliminując błędy monitorowania bitu w fazie danych (CAN Bit Error / Stuff Error) niszczące ramki na magistrali.
  - **Antykolizyjny algorytm slotów czasowych (Prime-Modulo Slot Delay):** Zastąpiono 5-bitowy algorytm `(uid_crc & 0x1F) * 3 ms` deterministyczną formułą opartą na liczbie pierwszej: `slot_delay_ms = 10 + ((uid_crc ^ (uid_hash << 16)) % 47) * 7 ms` (zakres 10..332 ms). Gwarantuje to szerokie rozproszenie czasowe odpowiedzi (np. 66 ms dla węzła `0x3D` vs 248 ms dla węzła `0x9F` – aż 182 ms różnicy), całkowicie eliminując kolizje pakietów na magistrali. Wdrożono identyczną formułę opóźnienia również w Bootloaderze (`openbl_core.c`).
  - **Wielo-węzłowe składanie pakietów w Pythonie (`tools/can_updater.py`, `tools/can_updater_gui.py`):** Zastąpiono pojedynczą zmienną `pending_f1` słownikiem `pending_f1: Dict[int, bytes]` indeksowanym identyfikatorem CAN węzła, co umożliwia bezbłędne składanie ramek Frame 1 i Frame 2 przy przeplatanej transmisji od wielu urządzeń. Dopasowanie odpowiedzi oparto na masce `(rx.arbitration_id & 0x1FFF00FF) == 0x01F200E9`, zachowując pełną kompatybilność wsteczną ze starszymi wersjami firmware (`0x01F2F3E9`).
- **Dedykowany mechanizm blokady sesji magistrali i eliminacja duplikacji ramek w trybie Dual-Bus (`CAN1` + `CAN2`) (`ota_manager.c`, `app/bootloader/openbl_core.c`, `main.c`, `tools/can_updater.py`):**
  - **Pełne zachowanie wsparcia dla dołączania `BOOT_OTA` do `CAN1`:** Zgodnie z założeniem architektonicznym profil `BOOT_OTA` jest dołączony do głównej magistrali `CAN1` przez `can_parser_attach_boot_ota(&can1_inst)` (umożliwiając diagnostykę i OTA na płytkach 1-kanałowych), jak i do `CAN2` (`can_parser_select(&can2_inst, CAN_PARSER_ID_BOOT_OTA)`).
  - **Inteligentne filtrowanie duplikatów w `ota_manager_feed_can_frame()`:** Rozwiązano problem dublowania ramek na stanowiskach testowych, gdzie linie CAN1 i CAN2 są fizycznie spięte w jedną magistralę. Gdy nadchodzi ramka inicjująca transmisję (Single Frame `0x00` lub First Frame `0x10`), menedżer blokuje aktywny kanał `s_active_can_inst`. Zduplikowane ramki (np. kopia `CF #1` z drugiego kontrolera) docierające z nieaktywnej instancji są natychmiast odrzucane, co zapobiega rozjeżdżaniu się licznika sekwencji ISO-TP (`sn != link->rx_sn`) i gwarantuje płynne przesyłanie wszystkich 38 ramek w blokach 256 B.
  - **Dynamiczne blokowanie magistrali w Bootloaderze (`openbl_core.c`):** W procedurze `poll_fdcan_instance()` wprowadzono dynamiczne ryglowanie aktywnego kontrolera `s_active_hfdcan` na podstawie przekazanego parametru `active_can_bus` z `SHARED_RAM` (lub pierwszej odebranej ramki). Ramki z drugiego kontrolera są bezstratnie opróżniane z bufora FIFO bez przekazywania do stosu ISO-TP, co eliminuje dublowanie również w trybie Bootloadera.
  - **Poprawka kodowania paska postępu w CLI (`tools/can_updater.py`):** Zastąpiono znak Unicode `█` standardowym znakiem `=` w `print_progress_bar()`, eliminując wyjątek `UnicodeEncodeError` na konsolach Windows ze stroną kodową CP-1250.
- **Eliminacja błędu Timeoutu w Bloku #0 podczas aktualizacji OTA (przepełnienie kolejki i blokujący UART) (`can_manager.c`, `ota_manager.c`, `isotp.c`, `openbl_core.c`, `can_updater.py`):**
  - **Usunięcie blokującego `safe_printf` z `can_rx_task`:** Usunięto synchroniczne wypisywanie UART dla każdej odebranej ramki CAN w `can_manager.c` (linia 148), które blokowało zadanie odbiorcze na 3.125 ms na ramkę, powodując natychmiastowe przepełnienie 3-elementowego sprzętowego bufora FIFO FDCAN (`RF0L` - Message Lost) i utratę kolejnych ramek Consecutive Frame (CF).
  - **Zwiększenie kolejki ramek `OTA_QUEUE_LENGTH` z 16 do 64:** Dla pojedynczego 256-bajtowego bloku ISO-TP przesyłanych jest 39 ramek CAN (1 First Frame + 38 Consecutive Frames). Przy dotychczasowym rozmiarze kolejki 16, ramki 17-39 były natychmiast odrzucane (`errQUEUE_FULL`), co uniemożliwiało odebranie i potwierdzenie bloku. Zwiększenie do 64 gwarantuje bezstratny bufor całego bloku.
  - **Separacja ramek `STmin = 2ms` oraz precyzyjny zegar Windows (`timeBeginPeriod`):** W `isotp.c` zaktualizowano ramkę Flow Control, żądając odstępu `STmin = 2ms`. W skrypcie `can_updater.py` aktywowano precyzyjny timer Windows (`timeBeginPeriod(1)`), eliminując domyślne 15.6-milisekundowe ziarno `time.sleep` i zapobiegając nadmiernemu opóźnianiu transmisji bloków.
  - **Dostosowanie timeoutów kasowania pamięci Flash i zapisu bloków:** Zwiększono timeout oczekiwania na potwierdzenie kasowania Flash (`CMD_ERASE_APP`) z 6.0 s do 12.0 s, aby zapobiec przedwczesnemu timeoutowi po stronie PC (sprzętowe kasowanie 31 stron po 2 KB przez bibliotekę HAL ma limit 10.0 s). Timeout dla zapisu pojedynczego bloku zwiększono z 1.0 s do 2.5 s.
  - **Pętla opróżniania FIFO w Bootloaderze (`openbl_core.c`):** W procedurze `poll_fdcan_instance` zaimplementowano pełne opróżnianie bufora RX FIFO 0 w pętli `while(1)` aż do osiągnięcia `fill_level == 0`, zapobiegając utracie ramek przy szybkich seriach pakietów.
- **Wybór środowiska kompilacji oraz eliminacja błędu ścieżek kontenera (`tools/can_updater_gui.py`):**
  - Rozwiązano błąd `CreateProcess failed: The system cannot find the file specified`, który występował, gdy program na systemie Windows próbował uruchomić polecenie `ninja -C build/docker` zawierające wewnętrzne linuksowe ścieżki kontenera (`/usr/local/bin/cmake`).
  - Dodano listę wyboru silnika kompilacji `combo_build_engine` umożliwiającą użytkownikowi przełączanie między **`🔨 Windows (Ninja)`** (`build/debug_GCC_NUCLEO-C542RC`) a **`🐳 Docker (vbox-builder)`** (`.devcontainer\docker_build.bat`), z dynamicznym wykrywaniem dostępnych środowisk.
  - Zapewniono, że natywna kompilacja na Windows zawsze kieruje proces do katalogu `build/debug_GCC_NUCLEO-C542RC`, natomiast kompilacja Dockerowa jest wywoływana przez dedykowany skrypt wsadowy.
- **Odporność filtrów odbiorczych na sterowniki USB (candleLight / WinUSB / gs_usb) (`tools/can_updater.py`, `tools/can_updater_gui.py`, `tools/test_ota_suite.py`):**
  - Usunięto nadmiarowe sprawdzanie flagi `is_rx` dla dedykowanych identyfikatorów odpowiedzi węzłów (`0x01F2F3E9` Discovery Response oraz `0x01F2F3E8` ISO-TP Response). Ramki te są generowane wyłącznie przez mikrokontrolery STM32 i nigdy przez komputer PC, co zapobiega błędnemu odrzucaniu odpowiedzi węzłów przez specyficzne backendy USB/WinUSB.
  - Zwiększono domyślny timeout skanowania węzłów w wątku monitorującym GUI z 0.35 s do 0.5 s, gwarantując niezawodne wykrywanie urządzeń przy maksymalnym opóźnieniu slotu antykolizyjnego (do 93 ms) i cyklicznym ruchu telemetrii I/O (100 ms).
  - Wprowadzono bezpieczny limit (do 50 ramek) w pętli wstępnego opróżniania bufora RX (`bus.recv`) przed wysłaniem zapytania broadcast, eliminując ryzyko zapętlenia przy intensywnym ruchu na magistrali.

## [2026-10-19]
### Naprawione
- **Wyrównanie numeracji kroków potoku wykonania podczas wgrywania (`tools/can_updater.py`, `tools/can_updater_gui.py`):**
  - Rozwiązano problem podświetlania kroku 3 („3. Kasowanie”) zamiast kroku 4 („4. Zapis Bloków”) w widżecie potoku `PipelineStepperWidget` podczas strumieniowania bloków pamięci.
  - Wyemitowano brakujący krok 2 (`step_cb(2, 5, "Connecting / Resetting to Bootloader")`) podczas nawiązywania sesji z bootloaderem, krok 3 (`step_cb(3, 5, ...)`) dla kasowania Flash, krok 4 (`step_cb(4, 5, ...)`) dla aktywnego przesyłania bloków pamięci oraz krok 5 (`step_cb(5, 5, ...)`) dla weryfikacji CRC32 i restartu, co doprowadziło do 100% spójności CLI i GUI.
- **Eliminacja zawieszonego statusu SOS po poprawnym wgraniu i uruchomieniu aplikacji (`tools/can_updater.py`, `tools/can_updater_gui.py`):**
  - Rozwiązano problem utrzymywania się statusu `🔴 SOS (AWARIA APP)` oraz `1 W TRYBIE SOS` po pomyślnym wgraniu `v_box.bin` i rozruchu aplikacji (`APPLICATION`).
  - Zaimplementowano mechanizm czyszczenia i wygaszania (TTL = 3.5s) ramek ratunkowych `0xEE` w `CanBusMonitorWorker`: urządzenia raportujące tryb `APPLICATION` natychmiast usuwają powiązane beacony awaryjne z pamięci podręcznej.
  - W `_refresh_device_table()` oraz `_update_kpi_cards()` nadano nadrzędny priorytet trybowi `dev.get("mode") == "APPLICATION"`, blokując przypisywanie archiwalnych sygnałów awarii do poprawnie działającej aplikacji i aktualizując kafelki KPI do `1 Gotowe do Pracy (1 App / 0 Boot) | Brak awarii (OK)`.
  - W `_on_flash_finished()` po udanej aktualizacji wprowadzono automatyczne opróżnianie buforów magistrali CAN oraz wyczyszczenie pamięci podręcznej beaconów (`clear_all_beacons()`), odblokowując natychmiast przyciski diagnostyczne i przywracając zielony status węzła `🟢 Zdrowy (App)`.
- **Eliminacja obcinania ramek i zaokrągleń paneli oraz optymalizacja potoku wykonania (`tools/can_updater_gui.py`):**
  - Zlikwidowano pionowy `mid_splitter` rozcinający panel tabeli i panel konfiguracji/potoku, który powodował obcinanie dolnych i bocznych krawędzi `QGroupBox`, ukrywanie narożników `border-radius` i zniekształcenia przy zmianie wysokości.
  - Wprowadzono stabilny układ pionowy `QVBoxLayout` ze zbalansowanymi wysokościami sekcji (tabela 135-160 px, wiersz konfiguracji i potoku 200-225 px, terminal ze współczynnikiem rozciągania stretch=1 wypełniający dół okna).
  - Poprawiono style QSS dla stanu `:disabled` w `QPushButton#btn_danger`, `btn_success`, `btn_primary`, eliminując mylące, jaskrawe świecenie przycisku „Przerwij” na czerwono przed rozpoczęciem aktualizacji.
  - Przeprojektowano widżet potoku `PipelineStepperWidget`: wyśrodkowano linie łączące kroki (dokładnie w osi kółek 26 px), dodano dynamiczne zaznaczanie aktywnych i ukończonych linii na zielono (`#10B981`) oraz zaimplementowano głębokie czyszczenie layoutu przy przełączaniu kroków App (5) / Bootloader (6).
  - Przeniesiono przyciski szybkiej diagnostyki (`[📥 Do Bootloadera]`, `[▶️ Skocz do App]`, `[🔎 Dekoduj PC]`) z panelu potoku do paska akcji tabeli wykrytych węzłów obok `[🔍 Skanuj]`, uwalniając przestrzeń w potoku wykonania i grupując akcje na urządzeniu w jednym logicznym miejscu.
  - Zaimplementowano automatyczne uzupełnianie docelowego UID (`txt_target_uid`) przy wykryciu pojedynczego urządzenia oraz przy zmianie zaznaczenia wiersza tabeli (`itemSelectionChanged`), zabezpieczając sesję przed brakiem wskazanego celu.
- **Scalenie wierszy urządzenia i eliminacja duplikacji w trybie awaryjnym (`tools/can_updater.py`, `tools/can_updater_gui.py`):**
  - Rozwiązano problem powstawania dwóch osobnych wierszy dla tego samego urządzenia STM32 w tabeli węzłów, gdy mikrokontroler nadawał ramkę ratunkową `0xEE` (`CRASH_RECOVERY_BEACON`):
    - W firmware C (`app/bootloader/openbl_core.c`) skrót `uid_hash` jest liczony jako suma XOR wszystkich 12 bajtów UID (`s_mcu_uid[i]`), podczas gdy narzędzie Python dotychczas liczyło `zlib.crc32(uid_bytes) & 0xFF`, co powodowało rozbieżność skrótów (np. `0x0C` vs `0x55`) i brak dopasowania do odebranego beaconu.
    - Dodano funkcję `compute_mcu_uid_hash(uid_bytes)` w `tools/can_updater.py` wyliczającą poprawny skrót XOR identyczny z mikrokontrolerem oraz dostosowano `scan_nodes()` do zwracania zarówno `uid_hash` (XOR), jak i pomocniczego `uid_crc_hash`.
    - W `tools/can_updater_gui.py` wprowadzono scalanie danych wiersza: jeśli urządzenie zgłasza błąd aplikacji, tabela prezentuje dokładnie jeden wiersz ze statusem `🔴 SOS (AWARIA APP)`, trybem `BOOTLOADER [RECOVERY]` oraz pełnym opisem diagnostycznym: `Awaria App: {reason} ({fail_cnt}/3) @ {decoded_pc} | Bootloader stabilny (gotowy do wgrania)`.
- **Blokada niebezpiecznego skoku do uszkodzonej aplikacji (`tools/can_updater_gui.py`):**
  - Wprowadzono automatyczne blokowanie (`setEnabled(False)`) szybkiej akcji `[▶️ Skocz do App]` (`btn_quick_app`), gdy wskazany węzeł docelowy znajduje się w trybie ratunkowym (`BOOT_MODE_RECOVERY`).
  - Dodano prewencyjne ostrzeżenie modalne (`QMessageBox.warning`) oraz dymek podpowiedzi wyjaśniający, że skok do uszkodzonej lub brakującej aplikacji spowoduje ponowną pętlę restartów i natychmiastowy błąd MCU (wymagane jest uprzednie wgranie poprawnego wsadu).

### Zmienione
- **Ergonomia skanowania i usunięcie zduplikowanego przycisku (`tools/can_updater_gui.py`):**
  - Usunięto nadmiarowy przycisk `Skanuj` z górnego panelu połączenia (`POŁĄCZENIE MAGISTRALI CAN`), pozostawiając jedno, logiczne i wyeksponowane miejsce skanowania: przycisk `[🔍 Skanuj Magistralę CAN (0x7DF)]` w nagłówku tabeli wykrytych węzłów.
- **Dynamiczna weryfikacja gotowości środowiska budowania (`tools/can_updater_gui.py`):**
  - Dodano funkcję `check_build_environment(repo_root)` weryfikującą obecność i sprawność środowiska (narzędzia natywne: `ninja` + `arm-none-eabi-gcc` lub kontener Docker: `docker` + obraz `vbox-builder`).
  - Przycisk `[🔨 Zbuduj Projekt]` automatycznie adaptuje etykietę i stan aktywności (`Zbuduj Projekt (Docker)`, `Zbuduj Projekt (Ninja)` lub zablokowany `Zbuduj Projekt (Niedostępny)`).
  - Obok przycisku dodano przycisk pomocy `[ℹ️]`, otwierający czytelne okno z instrukcją krok po kroku konfiguracji środowiska Docker Desktop oraz natywnego Ninja/GCC, przypominający również, że wgrywanie gotowych plików `.bin` z dysku nie wymaga żadnego środowiska kompilacji.

### Dodane
- **Kompletna instrukcja obsługi i dokumentacja funkcjonalna aplikacji GUI (`docs/bootloader/can_updater_gui_manual.md`, `README.md`):**
  - Utworzono podręcznik użytkownika opisujący wszystkie 6 stref interfejsu `V-BOX CAN OTA Updater & Diagnostic Studio`, zasady automatycznej klasyfikacji portów USB (CANable 2.0 vs ST-Link VCP), kafelki filtrujące KPI, obsługę trybu awaryjnego SOS z dekodowaniem `PC`, działanie strażnika binarnego *Pre-Flight Binary Guard*, potoki 5/6-krokowe oraz scenariusze serwisowe krok po kroku wraz ze zrzutem ekranu (`docs/bootloader/can_updater_gui_screen.png`).
  - Zaktualizowano główny plik `README.md` oraz `docs/bootloader/can_bootloader_and_ota_guide.md` o bezpośrednie odnośniki do nowej instrukcji.
- **Dymki podpowiedzi (Tooltips) dla kluczowych kontrolek diagnostycznych i konfiguracyjnych (`tools/can_updater_gui.py`):**
  - Wyjaśnienie opcji `Użyj artefaktu z build/ (Auto-Path)` – automatyczne śledzenie `v_box.bin` / `v_box_boot.bin` w `build/debug_GCC_NUCLEO-C542RC/`.
  - Wyjaśnienie flagi `Verbose ISO-TP` – podgląd pojedynczych surowych ramek CAN vs raportowanie postępu na poziomie bloków pamięci.
  - Wyjaśnienie przycisku `[🔍 Dekoduj PC błędu]` – tłumaczenie adresu instrukcji awarii mikrokontrolera na plik źródłowy C, funkcję i numer linii przez `arm-none-eabi-addr2line` oraz plik ELF.
  - Wyjaśnienie przycisków szybkiej diagnostyki: `[⚡ Wejdź w Bootloader]` oraz `[▶️ Skocz do App]`.
- **Trwała blokada minimalnego rozmiaru okna i ochrona przed zniekształcaniem sekcji GUI (`tools/can_updater_gui.py`):**
  - Ustalono stały minimalny rozmiar okna na poziomie `1180 x 840 px` (`self.setMinimumSize(1180, 840)`), uniemożliwiając użytkownikowi zwężenie okna do rozmiarów zniekształcających widżety.
  - Wyłączono zwijanie widżetów poniżej limitów w suwaku podziału (`mid_splitter.setChildrenCollapsible(False)`) oraz ustawiono gwarantowane wysokości minimalne: tabela (130 px), panel konfiguracji/potoku (270 px) oraz terminal (160 px), dzięki czemu ręczne przesuwanie suwaka nigdy nie utnie przycisków akcji ani strażnika Pre-Flight.
  - Przeprojektowano i skompaktowano panel połączenia CAN (`POŁĄCZENIE MAGISTRALI CAN`): dopasowano stałe szerokości kontrolek, skrócono etykietę przycisku do zwięzłego `🔍 Skanuj`, eliminując nachodzenie na siebie elementów przy zmianie szerokości.
  - Skrócono opisy radiowe w wierszu wyboru wsadu (`Aplikacja (App)`, `Bootloader`, `Test RAM (Dummy)`), przenosząc pełne adresy pamięci Flash i specyfikację do dymków podpowiedzi (tooltip), co zabezpieczyło kontrolki przed obcinaniem tekstu.
- **Eliminacja ucinania sekcji „KONFIGURACJA AKTUALIZACJI I WYBÓR WSADU” (`tools/can_updater_gui.py`):**
  - Skompaktowano układ pionowy panelu konfiguracji (marginesy 10x8 px, odstępy 5 px, stała wysokość kontrolek 26 px, umieszczenie selektorów typu wsadu `rad_app`/`rad_boot`/`rad_dummy` w jednym wierszu z etykietą `Wsad:`).
  - Nadano gwarantowaną wysokość minimalną dla kontenera `lower_widget` (265 px) oraz modułu strażnika binarnego `frame_preflight` (48 px), usuwając niepożądane rozpychanie `addStretch(1)`.
  - Zoptymalizowano podział wertykalny w `mid_splitter` (współczynnik rozciągania 1:4 na korzyść panelu konfiguracji/potoku, domyślne rozmiary 130x290 px), co gwarantuje pełną i czystą widoczność weryfikacji Pre-Flight (rozmiar, wektory MSP/Reset, suma CRC32) bez konieczności powiększania okna aplikacji.
- **Całkowite wyeliminowanie zamrażania i lagowania GUI po wykryciu trybu ratunkowego 0xEE (`tools/can_updater_gui.py`):**
  - Zidentyfikowano przyczynę blokowania głównego wątku GUI (`QApplication` event loop): funkcja `decode_pc_address()` przy braku lokalnego `arm-none-eabi-addr2line` wykonywała synchroniczne wywołanie `docker run` (~600 ms) w wątku interfejsu przy każdej odebranej ramce `0xEE` oraz podwójnie przy odrysowaniu tabeli (ponad 1,2 s blokady co sekundę).
  - Wprowadzono pamięć podręczną `_PC_DECODE_CACHE`: zredukowano czas sprawdzania znanego adresu PC do 0,001 ms.
  - Asynchroniczne dekodowanie Docker w tle: translacja adresu PC w kontenerze `vbox-builder` odbywa się w dedykowanym wątku w tle i aktualizuje tabelę przez sygnał `sig_pc_decoded`, gwarantując natychmiastową responsywność okna i płynne przesuwanie (60 FPS).
  - Dławienie ramek i strażnik `is_changed`: wstrzymano niepotrzebne, ciągłe przebudowywanie widżetów tabeli i powtarzające się wpisy do terminala przy identycznych, cyklicznych ramkach ratunkowych.

### Zmienione
- **Domyślne wyłączenie opcji `Auto-Scan (3s)` na rzecz cichej magistrali CAN (`tools/can_updater_gui.py`):**
  - Ustawiono domyślny stan pola wyboru `Auto-Scan (3s)` na odznaczony (`False`) w interfejsie użytkownika oraz w wątku monitora `CanBusMonitorWorker` (`self.auto_scan_enabled = False`).
  - Zapewniono cichą pracę magistrali CAN bez wysyłania cyklicznych zapytań discovery co 3 sekundy, pozostawiając natychmiastowe pojedyncze skanowanie po kliknięciu „Połącz” oraz manualne wyzwalanie przyciskiem `[🔍 Skanuj Magistralę CAN (0x7DF)]`.
  - Zweryfikowano architekturę wyłączności magistrali CAN: potwierdzono, że mechanizm `pause_for_flashing()` bezpiecznie wstrzymuje monitorowanie magistrali w tle na czas wgrywania oprogramowania przez `OtaFlashWorker`.

### Dodane
- **Zabezpieczenie i asystent środowiska Docker Desktop (`tools/can_updater_gui.py`):**
  - Wprowadzono walidację dostępności narzędzia `docker` oraz responsywności demona `docker info` przed uruchomieniem kompilacji projektu przyciskiem `[🔨 Zbuduj Projekt (Docker/Ninja)]`.
  - W przypadku braku Dockera w systemie lub wyłączonej usługi Docker Desktop wyświetlane jest czytelne okno dialogowe z instrukcją odblokowania funkcji oraz wyjaśnieniem, że wgrywanie oprogramowania i diagnostyka CAN działają w 100% natywnie w Pythonie (wystarczy wybrać plik przyciskiem `📂 Przeglądaj...`).

### Zmienione
- **Usunięcie efektu zagnieżdżonych obwódek („pętli”) w kafelkach i stonowanie alarmu SOS (`tools/can_updater_gui.py`):**
  - Wyeliminowano kaskadowe dziedziczenie stylów `QFrame` przez widżety potomne `QLabel` wewnątrz kafelków `DeviceSummaryCard` oraz modułu `frame_preflight` (precyzyjne selektory ID `#summaryCard`, `#framePreflight` oraz reguła `border: none; background: transparent` dla `QLabel`), likwidując niepożądany efekt wielokrotnych obwódek/pętli.
  - Usunięto inwazyjny, pełnoszerokościowy baner ratunkowy `EmergencyBanner` oraz migający stroboskop (`pulse_timer`).
  - Kafelek `Wzywa Pomocy (Recovery)` w stanie spoczynku pozostaje stonowany i ciemny (`0 błędów / Brak awarii`), a w momencie nadejścia ramki ratunkowej `0xEE` elegancko i stabilnie „zapala się” w karminowym odcieniu (`#250C0E` / `#DC2626`) obok kafelka `Gotowe do Pracy`.
  - Kliknięcie w kafelek pomocy lub wiersz węzła awaryjnego w tabeli natychmiast filtruje widok i podpowiada docelowy UID.
- **Optymalizacja ergonomii i widoczności interfejsu CAN OTA Updater (`tools/can_updater_gui.py`):**
  - Wyczyszczono formatowanie listy portów szeregowych w panelu połączenia – usunięto nadmiarowe etykiety `[Zalecany dla CAN]`, zapobiegając kolizji z przyciskiem odświeżania portów, oraz naprawiono klasyfikację konwerterów (precyzyjna detekcja CANable po VID `0x16D0` / PID `0x117E` lub nazwach SLCAN/CANable).
  - Skompaktowano kafelki KPI floty (`DeviceSummaryCard`) do wysokości 66 px ze zoptymalizowanymi rozmiarami czcionek, co znacząco zredukowało zajmowaną przestrzeń wertykalną.
  - Dodano wyeksponowany, dedykowany przycisk `[🔍 Skanuj Magistralę CAN (0x7DF)]` bezpośrednio w nagłówku tabeli urządzeń.
  - Powiększony panel tabeli urządzeń (minimalna wysokość 140 px, wysokość wierszy 28 px) z implementacją kontrastowego stylu pasków przewijania QSS (`QScrollBar`), zapewniający czytelną prezentację i wygodne przewijanie wykrytych węzłów STM32.
  - Pełna synchronizacja stanu aktywności (`setEnabled`) obu przycisków skanowania w cyklu życia połączenia magistrali CAN i sesji programowania Flash.

### Dodane
- **Zaawansowana Aplikacja Okienkowa GUI do aktualizacji i diagnostyki CAN OTA (`tools/can_updater_gui.py`):**
  - Implementacja kompletnej aplikacji graficznej opartej na PyQt6 (z automatycznym fallbackiem do PyQt5) i nowoczesnym motywie przemysłowym Dark Engineering Theme.
  - Smart Connection Panel z inteligentnym rozróżnieniem adaptera CANable 2.0 SLCAN (VID `0x16D0`, PID `0x117E`) od portu ST-Link Virtual COM Port (VID `0x0483`), dynamicznym odświeżaniem portów i odpornością na odłączenie magistrali USB (`safe_open_or_reconnect_bus`).
  - Pasywny i aktywny monitor magistrali w tle (`CanBusMonitorWorker` w `QThread`) nasłuchujący ramek `0x7E9` i automatycznie wykrywający ramki ratunkowe `0xEE` (`CRASH_RECOVERY_BEACON`) z natychmiastowym dopytaniem o pełne 96-bitowe UID (`0xA0` na `0x7DF`).
  - Interaktywny pulpit floty z 3 klikalnymi kafelkami KPI i pulsującym alarmem wizualnym SOS (`DeviceSummaryCard`, `EmergencyBanner`) oraz przyciskiem natychmiastowego ratowania („Wybierz i Ratuj”).
  - Dekoder adresu instrukcji awarii PC (`decode_pc_address`) wykorzystujący lokalny `arm-none-eabi-addr2line` z automatycznym fallbackiem do kontenera Docker `vbox-builder` i pliku `v_box.elf`.
  - Świadomość repozytorium (`Repo-Aware`): automatyczne mapowanie artefaktów `build/debug_GCC_NUCLEO-C542RC/`, wykrywanie nieaktualnej kompilacji (`is_build_stale`) na podstawie znaczników czasu źródeł w `app/`, `generated/`, `linker/` oraz wbudowany kompilator tła (`BuildProjectWorker`).
  - Strażnik Pre-Flight Binary Guard (`inspect_firmware_binary`): weryfikacja rozmiaru, sumy CRC32 oraz tablicy wektorów Cortex-M33 (`Initial_MSP` w `0x20000000..0x20010000` i `Reset_Handler` z bitem Thumb), uniemożliwiająca przypadkowe wgranie obrazu Bootloadera jako Aplikacji lub odwrotnie.
  - 5-etapowy potok postępu (`PipelineStepperWidget`) z pomiarem KB/s, ETA, bloków i ponowień oraz przyciskiem bezpiecznego przerwania transmisji (`cancel_cb`).
  - Podwójny terminal (`DualTerminalPanel`): log transmisji CAN/ISO-TP z opcją Verbose i zapisem do pliku oraz wbudowany monitor portu ST-Link VCP UART2 (115200 bps) w osobnym wątku `SerialUartWorker`.
- **Rozszerzenie silnika backendu CLI (`tools/can_updater.py`):**
  - Dodano opcjonalne funkcje zwrotne (`progress_cb`, `step_cb`, `log_cb`, `cancel_cb`) do funkcji `scan_nodes()`, `run_update_session()` oraz `run_bootloader_update_session()`.
  - Obliczanie skrótu `uid_hash = zlib.crc32(uid_bytes) & 0xFF` oraz rejestrowanie alarmów `0xEE` do listy `beacons_out`.
  - Obsługa bezpiecznego przerwania transmisji bloków w pętli.
- **Konfiguracja środowiska i zadania VS Code (`tools/requirements_ota.txt`, `.vscode/tasks.json`):**
  - Dodano zadania `[GUI] Launch CAN OTA Updater`, `[OTA] Scan CAN Bus (CLI)` oraz `[OTA] Run Fault-Injection Test Suite`.
  - Zaktualizowano zależności o `PyQt6` dla środowiska Windows.
- **Plan implementacyjny aplikacji okienkowej PyQt CAN OTA Updater GUI (`docs/bootloader/can_updater_gui_implementation_plan.md`):**
  - Szczegółowa specyfikacja architektury i UI/UX aplikacji okienkowej opartej na PyQt6/PyQt5 oraz silniku `tools/can_updater.py`.
  - Projekt interaktywnych kafelków floty (KPI Blocks), pasywnego detektora ramek ratunkowych `SOS 0xEE` (`BOOT_MODE_RECOVERY`) z pulsującym alarmem i dekodowaniem adresu awarii `PC` (`addr2line`), inteligentnej integracji z repozytorium i systemem budowania CMake/Ninja oraz inspektora nagłówków tablicy wektorów Cortex-M33 (Pre-Flight Check).
- **Kompleksowa dokumentacja techniczna i instrukcja obsługi podsystemu CAN Bootloader & OTA (`docs/bootloader/can_bootloader_and_ota_guide.md`, `README.md`, `docs/project_documentation.md`):**
  - Szczegółowy opis architektury Dual-Target (`v_box_boot` + `v_box`), mapy pamięci Flash/SRAM, warstwy ISO-TP, protokołu OpenBootloader oraz mechanizmów Anti-Brick (Deferred Vector Table Commit, reset RCC przy skoku, Staging Slot).
  - Pełne diagramy przepływu programu (`Program Flow` w Mermaid) z podziałem na Bootloader (Fast-Boot < 2 ms, Stay-in-Boot, Recovery Mode `0xEE`) oraz Aplikację Główną (asynchroniczny `ota_worker_task`, 3s Healthy confirmation, `fault_recovery`).
  - Instrukcja podłączenia sprzętowego dla konwertera **CANable 2.0** i transceivera 3.3V do kontrolera **FDCAN2** (`PB5` CN10 pin 6 / D4, `PB6` CN10 pin 3 / D10) oraz podręcznik CLI dla `tools/can_updater.py` i `tools/test_ota_suite.py` z autentycznymi logami z konsoli PC oraz terminala UART2.
  - Aktualizacja głównego pliku `README.md` o opis architektury Dual-Target, instrukcje aktualizacji po CAN, nowe zadania VS Code (`[Flash] Full Merged Image`, `[Flash] Bootloader Only`, `[Flash] MainApp Only`) oraz linki do nowej dokumentacji.
- **Interaktywny i zautomatyzowany pakiet testowy awarii i mechanizmów odporności OTA (`tools/test_ota_suite.py`):**
  - Implementacja dedykowanego narzędzia testowego weryfikującego odporność Bootloadera i Aplikacji Głównej na wstrzykiwane błędy i awarie zasilania.
  - **Test 1 (Application - Corrupted CRC32):** Weryfikacja odrzucenia uszkodzonej sumy kontrolnej weryfikacji aplikacji (`OTA_RESP_NACK, OTA_ERR_CRC`) i braku skoku do uszkodzonego kodu.
  - **Test 2 (Application - Power-Loss Recovery):** Symulacja nagłego odcięcia zasilania podczas zapisu pamięci Flash aplikacji, interaktywna instrukcja odłączenia i podłączenia zasilania (`TERAZ ODŁĄCZ ZASILANIE PŁYTKI...`), detekcja wejścia Bootloadera w tryb ratunkowy `BOOT_MODE_RECOVERY` (emisja ramki alarmowej `0xEE`) oraz automatyczne przeprowadzenie procedury ratunkowej (Rescue Flash) z przywróceniem działania aplikacji.
  - **Test 3 (Bootloader - Invalid Vector Table):** Próba wgrania Bootloadera ze zmanipulowanym wskaźnikiem MSP (`0xDEADBEEF`) i weryfikacja sprzętowego strażnika tablicy wektorów Cortex-M33 odrzucającego commit do sektora `0x08000000`.
  - **Test 4 (Bootloader - Interrupted Staging):** Symulacja przerwania wgrywania do slotu stagingowego przed rozkazem instalacji, weryfikacja nienaruszalności aktywnego Bootloadera i poprawnego startu aplikacji po resecie.
  - **Test 5 & 6 (Golden Path):** Pełna weryfikacja wzorcowego procesu aktualizacji Aplikacji (`v_box.bin`) oraz Bootloadera (`v_box_boot.bin`) ze 100% potwierdzeniem transferu, sum kontrolnych i poprawnego restartu.
  - **Końcowy raport:** Prezentacja czytelnej, sformatowanej tabeli podsumowującej wszystkie scenariusze testowe, statusy `[PASS]/[FAIL]`, czas wykonania i diagnozę.
### Naprawione
- **Ochrona przed przedwczesnym rozruchem niekompletnej Aplikacji po zaniku zasilania (`app/bootloader/openbl_core.c`):**
  - Wdrożenie mechanizmu opóźnionego zapisu tablicy wektorów (Deferred Vector Table Commit): blok 0 (`0x08008000`) jest buforowany w pamięci RAM i programowany do pamięci Flash dopiero po pomyślnej weryfikacji sumy kontrolnej całego obrazu `CMD_VERIFY_FIRMWARE`.
  - Zapobiega to sytuacji, w której przerwanie zasilania w trakcie zapisu pozostawiało poprawny nagłówek MSP/Reset_Handler przy braku reszty kodu, co powodowało natychmiastowe zawieszenie rdzenia w Cortex-M Lockup po restarcie.
  - W przypadku przerwania zapisu sektor `0x08008000` pozostaje w stanie skasowanym (`0xFFFFFFFF`), dzięki czemu Bootloader przy pierwszym rozruchu wykrywa brak aplikacji, wchodzi w `BOOT_MODE_RECOVERY` i czeka na flash ratunkowy.
- **Brakujące stałe trybów pracy OTA (`tools/can_updater.py`):**
  - Zdefiniowano stałe `OTA_MODE_BOOTLOADER`, `OTA_MODE_APPLICATION`, `OTA_MODE_RECOVERY` importowane przez narzędzia testowe.
- **Odporność pętli uruchomieniowej testów (`tools/test_ota_suite.py`):**
  - Zwiększono limity czasu oczekiwania na re-enumerację portu USB po odłączeniu zasilania do 12 sekund oraz zabezpieczono każdy scenariusz testowy blokiem `try-except`, gwarantując wygenerowanie kompletnego raportu końcowego nawet w przypadku awarii pojedynczego testu.

## [2026-10-16]
### Dodane
- **Zdalna aktualizacja samego Bootloadera z poziomu Aplikacji Głównej przez Flash Staging Slot (Faza 6):**
  - `linker/stm32c542xc_app_flash.ld`: Rezerwacja dedykowanego regionu pamięci `STAGING (r)` pod adresem `0x08038000` (ostatnie 16 stron Flash = 32 KB) na bufor stagingowy nowego wsadu Bootloadera. Pamięć Aplikacji Głównej `ROM` ograniczona do 192 KB (`0x08008000`–`0x08037FFF`, z czego aplikacja zajmuje ~61.3 KB, pozostawiając ponad 130 KB wolnego miejsca).
  - `app/ota_update/bootloader_updater.h`, `bootloader_updater.c`: Wdrożenie modułu instalatora Bootloadera operującego w kontekście `MainApp`. Obsługa kasowania i strumieniowego zapisu do bufora stagingowego (`0x08038000`), pełna walidacja sumy kontrolnej CRC32 weryfikacja nagłówka tablicy wektorów Cortex-M33 (sprawdzenie, czy `MSP` wskazuje na poprawny obszar SRAM `0x20000000`–`0x20010000`, a `Reset_Handler` na przestrzeń Bootloadera `0x08000001`–`0x08008000` z ustawionym bitem Thumb). Atomowe skasowanie i zaprogramowanie sektora `0x08000000` w sekcji krytycznej `taskENTER_CRITICAL()` z weryfikacją bajt po bajcie (`memcmp`).
  - `app/ota_update/ota_protocol.h`, `ota_manager.c`: Rejestracja nowej komendy protokołu `CMD_UPDATE_BOOTLOADER (0xA4)`. Dyspozytor rozpoznaje adres docelowy `0x08038000` w komendach `CMD_ERASE_APP` oraz `CMD_WRITE_MEMORY`, a po pomyślnym zatwierdzeniu instalacji `CMD_UPDATE_BOOTLOADER` wykonuje reset procesora `NVIC_SystemReset()` na rozkaz `CMD_GO_EXECUTE`.
  - `tools/can_updater.py`: Dodanie opcji wiersza poleceń `--update-bootloader <BOOTLOADER_BIN>`. Skrypt automatycznie upewnia się, że urządzenie pracuje w trybie Aplikacji (w razie potrzeby wywołuje skok do App), wgrywa wsad do slotu stagingowego, weryfikuje sumę CRC32, wydaje polecenie commitu i restartuje układ do nowego Bootloadera.
- **Pełny proces aktualizacji Firmware OTA po magistrali CAN z zapisem do Flash (Faza 5):**
  - `openbl_flash_c5.c`: Dedykowany, statyczny bufor 512-bajtowy `s_aligned_flash_buf` w sekcji BSS gwarantujący ścisłe 32-bitowe wyrównanie wskaźnika danych dla sprzętowego kontrolera pamięci Flash (`HAL_FLASH_ProgramByAddrAdapt`) bez obciążania stosu mikrokontrolera. Bezpieczne kasowanie stron Flash z walidacją przestrzeni aplikacji (`0x08008000`–`0x08040000`) oraz weryfikacja zapisu bajt po bajcie (`memcmp`).
  - `openbl_core.c`: Dodano weryfikację aktywnej autoryzacji sesji docelowej (`s_target_selected`) przed wykonaniem procedury skoku do Aplikacji w ramach komendy `CMD_GO_EXECUTE (0x21)`.
  - `tools/can_updater.py`: Wdrożenie automatycznego wykrywania trybu urządzenia i żądania przełączenia `App -> Bootloader` (Step 1b: wysłanie `CMD_ENTER_BOOTLOADER`, odczekanie na restart i re-selekcja węzła w Bootloaderze przed wywołaniem kasowania i programowania). Zwiększono limit czasu oczekiwania na skasowanie pamięci Flash do 6,0 s oraz dodano automatyczne dopełnianie plików `.bin` bajtami `0xFF` do granicy 16 bajtów.
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
- **Konfiguracje uruchomieniowe debugera ST-Link GDB Server (`.vscode/launch.json`, `docs/build_and_development_guide.md`):**
  - Dodano 4 dedykowane profile debugowania w VS Code:
    1. `STM32Cube: Debug Full System (Bootloader + App)` – wgrywanie obu wsadów i ładowanie podwójnej tablicy symboli dla Bootloadera i Aplikacji z możliwością płynnego śledzenia tranzycji skoku przez pułapki w obu przestrzeniach.
    2. `STM32Cube: Debug Application (v_box)` – ukierunkowane debugowanie wyłącznie Aplikacji Głównej (`0x08008000`) ze stopem na `main()`.
    3. `STM32Cube: Debug Bootloader (v_box_boot)` – ukierunkowane debugowanie wyłącznie Bootloadera (`0x08000000`).
    4. `STM32Cube: Attach to Target (Hotplug / No Reset)` – bezinwazyjne dołączenie sesji GDB do działającego procesora bez wywoływania resetu mikrokontrolera.
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
- **Błyskawiczny rozruch aplikacji Fast-Boot oraz automatyczny tryb ratunkowy (`app/bootloader/boot_main.c`, `app/bootloader/boot_shared.h`, `tools/can_updater.py`):**
  - Wyeliminowano sztuczne 300-milisekundowe okno nasłuchu CAN na starcie Bootloadera (`[NORMAL] Listening...`) opóźniające start systemu.
  - Wprowadzono bezopóźnieniowy rozruch (Fast Boot): Bootloader natychmiast po resecie sprawdza integralność Aplikacji pod `0x08008000` (poprawność wskaźników MSP w SRAM i Reset_Handler we Flash). Jeśli Aplikacja istnieje, wykonuje skok bezzwłocznie (czas wykonania Bootloadera < 2 ms).
  - Bootloader przechodzi w tryb oczekiwania i nasłuchu magistrali CAN2 wyłącznie w trzech uzasadnionych scenariuszach:
    1. Aplikacja zgłosiła wielokrotne awarie (HardFault / Watchdog / Assert $\ge$ 3 razy) – tryb `BOOT_MODE_RECOVERY`,
    2. Działająca Aplikacja zażądała wejścia do Bootloadera na żądanie PC (`CMD_ENTER_BOOTLOADER`) – tryb `BOOT_MODE_STAY_IN_BOOT`,
    3. Pamięć Flash jest czysta lub Aplikacja uszkodzona – wejście w tryb ratunkowy z nowym kodem błędu `BOOT_FAULT_NO_APP` (kod `0x04`).
  - W trybie awaryjnym/ratunkowym Bootloader natychmiast i cyklicznie (co 1 s) rozgłasza na ID `0x7E9` ramkę alarmową `0xEE` (`CRASH_RECOVERY_BEACON`) z kodem przyczyny, licznikiem awarii i adresem PC, a skrypt `tools/can_updater.py --scan` automatycznie dekoduje i wyświetla stan ratunkowy.
- **Optymalizacja i czytelność raportowania pamięci oraz scalania wsadów (`tools/print_memory.py`, `tools/merge_binaries.py`, `CMakeLists.txt`, `.devcontainer/docker_build.bat`):**
  - Wprowadzono skonsolidowany pulpit podsumowania Dual-Target (`print_dual_dashboard`), prezentujący w jednym, czytelnym zestawieniu (14 linii) wykorzystanie pamięci Flash, RAM, stosu i sterty FreeRTOS dla Bootloadera i Aplikacji oraz parametry scalonego obrazu.
  - Wyeliminowano wielokrotne, nakładające się tabele pamięci podczas równoległego budowania w Ninja – poszczególne targety kompilacji raportują postęp w zwięzłym formacie jednolinijkowym (`[MEM] ...`), a pełny raport wyświetlany jest jednokrotnie na koniec procesu łączenia wsadów.
  - Usunięto mylący pasek 100% zajętości pamięci dla bufora nieresetowalnego `SHARED_RAM` (oznaczono jako stały bufor IPC mailbox `[Fixed]`).
  - Dodano wyraźne rozróżnienie ról targetów w nagłówkach raportów szczegółowych (`CAN BOOTLOADER` vs `MAIN APPLICATION`).
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
- **Procedura skoku z Bootloadera do Aplikacji Głównej (`app/bootloader/boot_jump.c`, `main.c`):**
  - Wyeliminowano zawieszenie mikrokontrolera w pętli `while(1)` po skoku do Aplikacji (brak startu schedulera FreeRTOS i brak pracy diody LD1 heartbeat).
  - Przyczyną problemu było pozostawienie aktywnego generatora zegara PSI @ 144 MHz po stronie Bootloadera – funkcja `HAL_RCC_PSI_SetConfig()` w `mx_rcc_init()` Aplikacji sprawdza, czy oscylator jest wyłączony (`is_psis_ready == 0U`), w wyniku czego zwracała krytyczny status `HAL_ERROR` (`SYSTEM_CLOCK_ERROR`), powodując wyjście z funkcji `main()` z kodem `-1`.
  - W `boot_jump_to_application()` zaimplementowano pełne przywrócenie stanu sprzętowego: deinitializację peryferiów (`mx_fdcan1_deinit`, `mx_fdcan2_deinit`, `mx_usart2_uart_deinit`, `mx_tim6_deinit`), zatrzymanie i deinitializację pamięci podręcznej ICACHE (`HAL_ICACHE_DeInit`), reset szyn HAL (`HAL_DeInit()`) oraz reset drzewa zegarowego RCC (`mx_rcc_deinit() -> HAL_RCC_Reset()` do domyślnego HSIDIV3 @ 48 MHz).
  - Zastąpiono wywołanie skoku przez C-wskaźnik funkcji atomicznym skokiem w asemblerze Cortex-M33 (`cpsie i`, `msr msp, %0`, `bx %1`), zapewniając nienaruszony stan rejestrów stosu i przerwań jak po resecie sprzętowym.
  - W `main.c` dodano sygnalizację awarii zegarów/sprzętu (szybkie miganie LD1) oraz diagnostyczne logi UART w przypadku niepowodzenia inicjalizacji menedżerów CAN, OTA, I/O i zadań FreeRTOS.
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
  - Przeniesienie wejść krańcówek ze względu na konflikt z FDCAN2: Drzwi INNE na pin **PC8** (CN10 pin 31), Drzwi KIER na pin **PC7** (CN10 pin 19), a dodatkowego 9. wejścia na pin **PB2** (CN10 pin 22).
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

