# Changelog

Wszystkie znaczące zmiany w projekcie będą dokumentowane w tym pliku.

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
- **Konfiguracja zadań (`app/tasks_config.h`):** Dodano definicje priorytetu `TASK_PRIO_IO_MANAGER` oraz rozmiaru stosu `TASK_STACK_IO_MANAGER` (512 słów).
- **Wytyczne architektoniczne:** Dodano regułę 12 definiującą standard tworzenia i rejestracji parserów CAN oraz wzorzec profilu z getterem bez `extern`.

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

