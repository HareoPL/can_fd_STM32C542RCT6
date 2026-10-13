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
- **Konfiguracja zadań (`app/tasks_config.h`):** Dodano definicje priorytetu `TASK_PRIO_IO_MANAGER` oraz rozmiaru stosu `TASK_STACK_IO_MANAGER` (512 słów).

### Zmienione
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

