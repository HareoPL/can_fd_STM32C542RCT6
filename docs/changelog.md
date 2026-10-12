# Changelog

Wszystkie znaczące zmiany w projekcie będą dokumentowane w tym pliku.

## [2026-10-12]
### Dodane
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

