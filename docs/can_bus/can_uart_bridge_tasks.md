# Most UART → CAN (`can_uart_bridge`) – zadania implementacyjne

Dokument rozpisuje implementację modułu, który przyjmuje przez UART (USART2 / ST-LINK VCP) polecenia
w składni **cansend / CAN FD / TLV** Serial Commandera (`C:\WORK\01_tools\serial_commander\can_support.py`)
i nadaje odpowiadające im ramki na **CAN1 lub CAN2** z pełną kontrolą nad konfiguracją magistrali.

## Spis treści
1. [Cel i zakres](#1-cel-i-zakres)
2. [Decyzje projektowe](#2-decyzje-projektowe)
3. [Specyfikacja protokołu](#3-specyfikacja-protokołu)
4. [Architektura](#4-architektura)
5. [Zadania](#5-zadania)
6. [Kolejność i zależności](#6-kolejność-i-zależności)
7. [Definition of Done](#7-definition-of-done)

---

## 1. Cel i zakres

**Cel:** wysłać dowolną ramkę CAN / CAN FD z węzła jedną linią tekstu z terminala lub z przycisku
Serial Commandera (transport *Serial (UART)*), np.:

```text
Polecenie: 321##1#11 22 33 44 55 66 77 88 AA BB CC DD EE FF 00 11
Ramka: 321 [16] [FD+BRS] 11 22 33 44 55 66 77 88 AA BB CC DD EE FF 00 11
Rozmiar: 16B (FD: True, BRS: True)
```

**W zakresie:**
- ID standardowe (11-bit) i rozszerzone (29-bit), dane 0–64 B, Classic / FD / FD+BRS per ramka,
  RTR, TLV (`ot_app_msg_tlv`),
- wybór magistrali CAN1 / CAN2,
- konfiguracja magistrali z UART (tryb, prędkość nominalna, prędkość danych),
- walidacja i komunikaty błędów zgodne z `can_support.CanFrameError`,
- automatyczny test zgodności z `can_support.parse_can_frame()`.

**Poza zakresem (etap 2):** podgląd ramek odbieranych (`MON`), filtrowanie logów, protokół binarny.

---

## 2. Decyzje projektowe

Decyzje oznaczone **[DO POTWIERDZENIA]** mają przyjętą wartość domyślną, z którą zadania są rozpisane.
Ich zmiana dotyczy tylko wskazanych zadań.

| # | Decyzja | Przyjęte | Wpływ na zadania |
|---|---|---|---|
| D1 | Format protokołu | **Tekst liniowy ASCII** zgodny z `can_support.py` (nie binarny) **[DO POTWIERDZENIA]** | T4, T5 |
| D2 | Zakończenie linii | `\r`, `\n` lub `\r\n` (Serial Commander domyślnie wysyła CRLF) | T5 |
| D3 | Komenda konfiguracji | `CFG#<bus>#<tryb>#<nominal>[#<data>]` **[DO POTWIERDZENIA]** | T4, T6 |
| D4 | Konfiguracja z UART a profil parsera | `CFG` **nadpisuje** konfigurację magistrali (`can_manager_setBusConfig()`) do następnego `can_parser_select()` i wypisuje ostrzeżenie **[DO POTWIERDZENIA]** | T6 |
| D5 | BRS i RTR per ramka | Nowe pole `flags` w `CAN_msg_t` (BRS, RTR), rozmiar struktury bez zmian (72 B) | T2, T3 |
| D6 | Wybór magistrali | Opcjonalny prefiks `CAN1:` / `CAN2:`. Bez prefiksu: magistrala domyślna (`BUS#n`, start = CAN1) | T4, T6 |
| D7 | Włączenie modułu | Opcja CMake `CAN_UART_BRIDGE`, domyślnie `ON` | T7 |
| D8 | Kanał odpowiedzi | `safe_printf()` (ten sam UART co logi, odpowiedzi z tagiem `[CAN_UART]`) | T6 |

---

## 3. Specyfikacja protokołu

### 3.1 Linia wejściowa

- Maksymalnie **255 znaków** bez terminatora. Dłuższa linia zostaje odrzucona (`ERR: linia za długa (maks. 255 znaków)`),
  a bufor czeka na kolejny terminator.
- Białe znaki na początku i końcu są obcinane, pusta linia jest ignorowana.
- Prefiks magistrali (bez rozróżniania wielkości liter): `CAN1:` lub `CAN2:`. Reszta linii to polecenie.
- Polecenia sterujące (`CFG`, `BUS`, `MODE`, `HELP`) są rozpoznawane **przed** parserem ramek.
  Każda inna linia to ramka.

### 3.2 Składnia ramek – musi odpowiadać `parse_can_frame()` (tryb `auto`)

| Wejście | Wynik |
|---|---|
| `187#01 00 00 00 40 7D 00 00` | Classic, 8 B |
| `321#01 02 … (9–64 B)` | **automatycznie FD + BRS**, gdy dane > 8 B |
| `321##1#01 02 03` | FD, BRS = 1 |
| `321##0#01 02 03` | FD, BRS = 0 |
| `321##1010203` | FD z flagą, bez drugiego `#` (pierwszy znak `0`/`1` to flaga) |
| `321##01 02` | FD; pierwszy znak `0` to flaga BRS = 0, dane `1 02` → **błąd nieparzystej liczby cyfr**, jak w Pythonie |
| `65A#R`, `65A#R4` | RTR (Classic), DLC 0–8 |
| `3D0#` | ramka danych z DLC = 0 |
| `TLV#321#0xA5#Hello` | TLV: `<BH` = typ (u8), długość (u16 **LE**), wartość, dopełnienie zerami do 64 B, FD + BRS |
| `TLV#321#165#hex:01 02 03` | TLV z wartością binarną (prefiks `hex:`); typ dziesiętny lub `0x..` |

Reguły szczegółowe (przepisane z `can_support.py`):

1. **ID** (`_parse_can_id`): opcjonalny prefiks `0x`, wyłącznie cyfry hex, maks. 8 cyfr.
   **Extended, gdy ID ma dokładnie 8 cyfr albo wartość > 0x7FF.** Wartość > 0x1FFFFFFF to błąd.
2. **Dane:** usuwane są separatory `[\s.:_\-]`. Pozostałe znaki muszą być hex, a liczba cyfr parzysta.
3. **Limity:** Classic do 8 B, FD do 64 B. Długość FD nie jest kwantowana w parserze. Np. 10 B
   sprzęt wyśle z DLC 12 (dopełnienie zerami w adapterze), ale odpowiedź raportuje 10 B.
4. **RTR** tylko dla Classic. `R` w trybie FD lub ze składnią `##` daje błąd.
5. **Tryb wymuszony** (`MODE#…`, odpowiednik `can_mode`):
   `classic` (błąd przy `##` i > 8 B), `fd_brs`, `fd_no_brs`, `tlv` (dane pakowane jako TLV typu `0xA5`).

### 3.3 Polecenia sterujące

| Polecenie | Działanie | Odpowiedź |
|---|---|---|
| `CFG#1#FD_BRS#500k#2M` | CAN1: FD z BRS, nominal 500 kbps, dane 2 Mbps | `[CAN_UART] CAN1: FD+BRS 500k/2M OK` |
| `CFG#2#FD#1M` | CAN2: FD bez BRS 1 Mbps (`<data>` pomijane) | `[CAN_UART] CAN2: FD 1M OK` |
| `CFG#1#CLASSIC#250k` | CAN1: Classic 250 kbps | `[CAN_UART] CAN1: Classic 250k OK` |
| `CFG?` | Konfiguracja obu magistral, magistrala domyślna, tryb ramek | wiele linii |
| `BUS#2` | Magistrala domyślna = CAN2 | `[CAN_UART] BUS: CAN2` |
| `MODE#fd_brs` | Wymuszenie standardu ramki (`auto`, `classic`, `fd_brs`, `fd_no_brs`, `tlv`) | `[CAN_UART] MODE: fd_brs` |
| `HELP` | Skrócona składnia | wiele linii |

Wartości prędkości: liczba w bit/s (`500000`), z sufiksem `k` / `K` (`500k`), `M` (`2M`),
dozwolony ułamek dziesiętny (`83.3k`). Tryb (bez rozróżniania wielkości liter): `CLASSIC`, `FD`, `FD_BRS`.

### 3.4 Odpowiedzi

Sukces nadania (3 linie, format jak `CanFrame.to_display()`):

```text
[CAN_UART] Polecenie: CAN2:18DAF110##0#01 02 03
[CAN_UART] Ramka: 18DAF110 [3] [FD] 01 02 03
[CAN_UART] Rozmiar: 3B (FD: True, BRS: False) -> CAN2 OK
```

- ID: 3 cyfry hex (standard) albo 8 cyfr (extended), wielkie litery.
- Flagi: ` [FD+BRS]`, ` [FD]` albo brak (Classic). RTR: `65A [4] RTR`.
- Dla danych ≥ 3 B, które są poprawną strukturą TLV z niepustą wartością, dopisywany jest sufiks
  ` | TLV[0xA5, len=5]: "Hello"`, jak w `to_display(show_tlv=True)`.

Błędy (jedna linia, treść jak `CanFrameError` w `can_support.py`):

```text
[CAN_UART] ERR: nieparzysta liczba cyfr hex w danych ('112', długość: 3). Każdy bajt w CAN to 2 cyfry hex (np. '1120' lub '0112').
[CAN_UART] ERR: CAN1 pracuje w trybie Classic - ramka FD odrzucona (CFG#1#FD_BRS#500k#2M)
[CAN_UART] ERR: kolejka TX CAN1 pełna
```

Ostrzeżenie (ramka wysłana, ale inaczej niż w poleceniu):

```text
[CAN_UART] WARN: CAN1 pracuje w trybie FD bez BRS - ramka wysłana bez BRS
```

---

## 4. Architektura

```text
 USART2 RX IRQ ──► stm32c5_uart_adapter ──(bajt, ISR)──► can_uart_bridge: stream buffer
                     (port UART)                                   │
                                                       task can_uart: składanie linii
                                                                   │
                                     ┌─────────────────────────────┼──────────────────────┐
                                     ▼                             ▼                      ▼
                           can_frame_text_parse()        CFG / BUS / MODE        can_frame_text_format()
                           (czyste C, bez HAL)                    │                      │
                                     │                             ▼                      ▼
                                     └──► can_manager_send()   can_manager_setBusConfig()   safe_printf()
                                                 │
                                         port CAN (adapter FDCAN: flags BRS/RTR)
```

| Warstwa | Pliki | Zależności |
|---|---|---|
| Port UART | `app/port/uart_port.h`, `app/port/stm32c5/stm32c5_uart_adapter.c/.h` | HAL UART, NVIC (bez FreeRTOS) |
| Parser / formatter | `app/can_bus/can_uart_bridge/can_frame_text.c/.h` | tylko `can_msg_types.h`, `<string.h>` (bez HAL, bez FreeRTOS) |
| Moduł | `app/can_bus/can_uart_bridge/can_uart_bridge.c/.h` | `can_manager`, `uart_port`, FreeRTOS, `safe_printf` |
| Typ ramki | `app/can_bus/can_msg_types.h` | – |

Zasady z `gemini.md`: brak HAL poza `app/port`, callback z ISR wyłącznie przekazuje bajt dalej,
priorytet przerwania ≥ `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` (5), bez blokowania tasków CAN RX/TX.

---

## 5. Zadania

### T1 – Port UART z odbiorem na przerwaniu

**Pliki:** `app/port/uart_port.h` (nowy), `app/port/stm32c5/stm32c5_uart_adapter.c/.h` (nowe), `CMakeLists.txt`

1. Kontrakt w `uart_port.h` (wzorowany na `can_port.h`):
   ```c
   typedef void (*uart_rx_byte_fn)(uint8_t byte, void *context);   /* wywoływane z ISR */
   typedef struct {
       bool (*start_rx)(void);                                     /* uzbraja odbiór */
       void (*attach_rx_callback)(uart_rx_byte_fn cb, void *context);
   } uart_interface_t;
   ```
   Nadawanie zostaje w `safe_printf` (już współdzieli `mx_usart2_uart_gethandle()`), więc kontrakt nie ma `transmit`.
2. Adapter `stm32c5_uart2_interfaceGet()`:
   - `HAL_UART_Receive_IT(huart, &s_rx_byte, 1)` i ponowne uzbrojenie w `HAL_UART_RxCpltCallback`,
   - obsługa błędów ramki/overrun w `HAL_UART_ErrorCallback`: wyczyszczenie flag i ponowne uzbrojenie, licznik błędów,
   - `HAL_CORTEX_NVIC_SetPriority(USART2_IRQn, PREEMP_PRIORITY_5, 0)`, `EnableIRQ`,
   - `USART2_IRQHandler()` → `HAL_UART_IRQHandler(mx_usart2_uart_gethandle())`
     (wcześniej sprawdzić, że handler nie jest już zdefiniowany w `generated/` ani w pliku startup jako silny symbol).
3. Weryfikacja współbieżności z `safe_printf`: blokujące `HAL_UART_Transmit` i odbiór na przerwaniu
   przy tym samym uchwycie. Sprawdzić w HAL C5 rozdzielenie stanów TX/RX (`global_state` / `rx_state`).
   Jeśli HAL blokuje TX w trakcie aktywnego RX_IT, przejść na `HAL_UART_ReceiveToIdle_IT` z buforem kołowym
   albo na bezpośrednią obsługę RXNE w adapterze. Wynik analizy zapisać w komentarzu adaptera.

**Kryteria akceptacji:**
- echo testowe (tymczasowe) bajtów z ISR do licznika: 10 000 bajtów z PC przy 115200 bez utraty
  w trakcie intensywnego logowania,
- logi `safe_printf` działają bez zmian.

---

### T2 – Flagi BRS / RTR w `CAN_msg_t`

**Pliki:** `app/can_bus/can_msg_types.h`

1. Pole `uint8_t flags` w dzisiejszym miejscu wypełnienia (po `len`), rozmiar 72 B bez zmian
   (`_Static_assert(sizeof(CAN_msg_t) == 72U, ...)`):
   ```c
   #define CAN_MSG_FLAG_BRS  0x01U  /**< CAN FD: przełączanie prędkości w fazie danych */
   #define CAN_MSG_FLAG_RTR  0x02U  /**< Classic: ramka zdalna */
   ```
2. **Zgodność wsteczna:** istniejący kod zeruje ramkę (`memset` lub `= {0}`) i ustawia tylko `format`,
   więc `flags == 0` musi oznaczać dotychczasowe zachowanie (BRS według trybu magistrali). Dlatego
   dochodzi trzecia flaga:
   ```c
   #define CAN_MSG_FLAG_BRS_SET 0x04U /**< BRS wybrane jawnie dla tej ramki (wartość w CAN_MSG_FLAG_BRS) */
   ```
   Bez `BRS_SET` obowiązuje tryb magistrali. Moduł UART zawsze ustawia `BRS_SET`, żeby `##0#` na
   magistrali FD+BRS dało ramkę bez BRS.
3. Przegląd użyć `CAN_msg_t` (`grep -rn CAN_msg_t app`) i potwierdzenie, że żadne miejsce nie kopiuje
   struktury polami.

**Kryteria akceptacji:** build produkcyjny i testowy bez ostrzeżeń, OTA i `io_manager` działają bez zmian.

---

### T3 – Adapter FDCAN: BRS i RTR per ramka

**Pliki:** `app/port/stm32c5/stm32c5_can_adapter.c`, `app/port/can_port.h` (dokumentacja kontraktu)

1. `stm32c5_can_transmit_generic()`:
   - BRS = `CAN_MODE_FD_BRS` **i** (brak `BRS_SET` **lub** ustawione `BRS`),
   - RTR: `tx_header.b.frame_type = HAL_FDCAN_FRAME_REMOTE`, długość z `msg->len`, dane pomijane.
     RTR z `format == FD` → `false`.
2. `stm32c5_can_receive_generic()`: ustawia `CAN_MSG_FLAG_BRS` z `rx_header.b.bit_rate_switch`
   i `CAN_MSG_FLAG_RTR` z `rx_header.b.frame_type`.
3. Filtr globalny: obecnie `acceptance_remote_* = REJECT`. Odbiór RTR nie jest wymagany, ustawienie zostaje,
   ale trzeba to opisać w kontrakcie.
4. Aktualizacja opisu `transmit` / `receive` w `can_port.h` oraz w `docs/can_bus/can_porting_guide.md`.

**Kryteria akceptacji:** na adapterze PC (candleLight) widać:
FD z BRS na magistrali FD+BRS dla `BRS`, FD bez BRS dla `BRS_SET` bez `BRS`, ramkę RTR z poprawnym DLC.

---

### T4 – Parser i formatter tekstu ramki (`can_frame_text`)

**Pliki:** `app/can_bus/can_uart_bridge/can_frame_text.c/.h` (nowe)

1. API (czyste C, bez alokacji, bez HAL):
   ```c
   typedef enum { CFT_MODE_AUTO, CFT_MODE_CLASSIC, CFT_MODE_FD_BRS, CFT_MODE_FD_NO_BRS, CFT_MODE_TLV } cft_mode_t;

   typedef struct {
       uint8_t bus;          /**< 0 = domyślna, 1 = CAN1, 2 = CAN2 (z prefiksu) */
       CAN_msg_t msg;        /**< ident, format, len, flags, data */
   } cft_frame_t;

   bool can_frame_text_parse(const char *line, cft_mode_t mode, cft_frame_t *out,
                             char *err, size_t err_size);
   size_t can_frame_text_format(const CAN_msg_t *msg, char *buf, size_t size);   /* to_display() */
   ```
2. Implementacja reguł z [3.2](#32-składnia-ramek--musi-odpowiadać-parse_can_frame-tryb-auto), w kolejności z `parse_can_frame()`:
   prefiks magistrali → `TLV#` → brak `#` → `##` + flaga → ID → RTR → dane → tryb wymuszony → `auto`.
3. Komunikaty błędów skopiowane **dosłownie** z `can_support.py` (UTF-8, polskie znaki), m.in.:
   - `brak separatora '#'. Format: ID#DANE, np. 187#01 02 03 lub TLV#321#0xA5#Dane`
   - `ID '<id>' nie jest liczbą szesnastkową`, `ID '<id>' jest za długie (maks. 8 cyfr hex)`,
     `ID 0x<id> poza zakresem 29-bit (maks. 0x1FFFFFFF)`
   - `dane '<dane>' zawierają znaki spoza hex`, `nieparzysta liczba cyfr hex w danych (...)`
   - `za dużo danych: <n> B (maksimum CAN FD: 64 bajty)`, `ramki zdalne (RTR) nie są obsługiwane w standardzie CAN FD`
   - komunikaty trybu `classic` i TLV.
4. TLV: nagłówek `<BH` = `type`, `len & 0xFF`, `len >> 8`, wartość, dopełnienie zerami do 64 B.
   Wartość dłuższa niż 61 B to błąd: `za dużo danych TLV: <n> B (maks. 61 B)`.
   Tego przypadku w Pythonie nie ma (tam powstaje ramka > 64 B, odrzucana później), więc komunikat jest nowy.
5. Formatter zgodny z `CanFrame.to_display(show_tlv=True)`, łącznie z sufiksem TLV
   (znaki spoza ASCII 32–126 zastępowane `?` zamiast `\ufffd`; różnica opisana w dokumentacji).

**Kryteria akceptacji:** wszystkie wektory z T8 dają identyczny wynik (ID, extended, FD, BRS, RTR,
długość, dane albo treść błędu) jak `parse_can_frame()`.

---

### T5 – Odbiór linii (ISR → task)

**Pliki:** `app/can_bus/can_uart_bridge/can_uart_bridge.c`

1. Callback z ISR: `xStreamBufferSendFromISR(s_rx_stream, &byte, 1, &woken)` + `portYIELD_FROM_ISR`.
   Rozmiar bufora 512 B. Przepełnienie zwiększa licznik `rx_overflow` i jest raportowane przy następnej linii.
2. Task: `xStreamBufferReceive()` → składanie linii do bufora 256 B:
   - `\r` i `\n` kończą linię, a pusta linia (np. drugi znak CRLF) jest pomijana,
   - przekroczenie 255 znaków: tryb odrzucania do najbliższego terminatora i jeden komunikat `ERR`,
   - znaki < 0x20 (poza terminatorami) i ≥ 0x7F są odrzucane z błędem `niedozwolony znak 0x..`.
3. Brak echa wpisywanych znaków (Serial Commander nie oczekuje echa).

**Kryteria akceptacji:** 1000 linii wysłanych bez przerw (skrypt) → 1000 odpowiedzi, brak sklejonych lub uciętych linii.

---

### T6 – Wykonanie poleceń: ramki i konfiguracja

**Pliki:** `app/can_bus/can_uart_bridge/can_uart_bridge.c/.h`

1. API:
   ```c
   bool can_uart_bridge_init(const uart_interface_t *uart, can_instance_t *can1, can_instance_t *can2);
   ```
   `can2 == NULL` dopuszczalne (płytka bez CAN2): polecenia dla CAN2 zwracają `ERR: CAN2 niedostępny`.
2. **Ramka:**
   - parsowanie (T4) z bieżącym `MODE`, wybór magistrali: prefiks albo domyślna,
   - walidacja względem `inst->can_config.mode`:
     FD na Classic → `ERR`, BRS na FD bez BRS → `WARN` i wysyłka bez BRS, RTR na FD → `ERR`,
   - `can_manager_send()`. Pełna kolejka → `ERR: kolejka TX CANx pełna`,
   - odpowiedź 3-liniowa z [3.4](#34-odpowiedzi). Potwierdzenie oznacza **przyjęcie do kolejki**;
     ewentualny późniejszy błąd sprzętowy widać w `tx_error_cnt` (dopisany do `CFG?`).
3. **`CFG`:**
   - parsowanie: bus `1|2`, tryb, prędkości (`k`, `M`, ułamki), `<data>` wymagane tylko dla `FD_BRS`,
   - `can_manager_waitTxIdle(inst, 200)`, potem `can_manager_setBusConfig(inst, &cfg)`,
   - błąd (np. 5M przy 144 MHz) → `ERR: konfiguracja nieosiągalna, pozostaje <stara>`
     (manager przywraca poprzednią konfigurację),
   - zgodnie z D4: `WARN: konfiguracja nadpisana z UART (profil parsera: <id>)`.
4. **`CFG?`:** dla każdej magistrali tryb, prędkości, TEC/REC, `tx_error_cnt`, `bus_off_cnt`
   (`can_manager_getStatus()`), a także magistrala domyślna, `MODE` i licznik `rx_overflow`.
5. **`BUS#n`**, **`MODE#…`**, **`HELP`**.
6. Bezpieczeństwo: moduł nie zmienia filtrów ani profili parsera i nie wysyła na zarezerwowanych ID OTA
   (`0x01F2F3DF`, `0x01F2F3E0`, `0x01F2F3E9`): `ERR: ID zarezerwowane dla OTA`. **[DO POTWIERDZENIA]**

**Kryteria akceptacji:** scenariusze z T8, ręcznie z Serial Commandera (transport UART, EOL CRLF).

---

### T7 – Integracja

**Pliki:** `main.c`, `app/tasks_config.h`, `CMakeLists.txt`, `generated/middleware/FreeRTOSConfig.h` (jeśli brak sterty)

1. `CMakeLists.txt`: opcja `CAN_UART_BRIDGE` (domyślnie `ON`): źródła, include, `CAN_UART_BRIDGE=1`.
2. `tasks_config.h`: `TASK_PRIO_CAN_UART (tskIDLE_PRIORITY + 2U)`, `TASK_STACK_CAN_UART 512U`.
3. `main.c`: po inicjalizacji CAN1, CAN2 i OTA wywołać
   `can_uart_bridge_init(stm32c5_uart2_interfaceGet(), &can1_inst, &can2_inst)`.
   W buildzie `CAN_FD_TEST` przekazywać `&can2_inst` tylko z `CAN_FD_TEST_LOOP`, w przeciwnym razie `NULL`.
4. Kontrola sterty: dzisiaj wolne ok. 6 KB (prod). Task 2 KB + stream buffer 512 B + bufory, więc
   zapas trzeba sprawdzić w logu `FreeRTOS heap free` i w razie potrzeby podnieść `configTOTAL_HEAP_SIZE`.
5. Log startowy: `[CAN_UART] bridge ready (CAN1, CAN2), wpisz HELP`.

**Kryteria akceptacji:** oba buildy (prod, `CAN_FD_TEST`) bez ostrzeżeń, wolna sterta ≥ 2 KB.

---

### T8 – Test zgodności z Serial Commanderem

**Pliki:** `tools/test_can_uart_bridge.py` (nowy), `tools/can_uart_vectors.json` (generowany)

1. Import `can_support` z `C:\WORK\01_tools\serial_commander` (ścieżka z parametru `--serial-commander`).
2. Wektory (min. 80), wynik oczekiwany liczony przez `can_support.parse_can_frame()`:
   - ID: `0`, `7FF`, `800` (ext), `00000123` (ext, 8 cyfr), `1FFFFFFF`, `20000000` (błąd), `0x1A`, `G1` (błąd), 9 cyfr (błąd),
   - dane: 0–8 B, 9 B (auto FD+BRS), 12/16/20/24/32/48/64 B, 65 B (błąd), nieparzyste cyfry, znaki spoza hex,
     wszystkie separatory,
   - `##0#`, `##1#`, `##1` bez danych, `##` bez flagi, `##01 02` (pułapka flagi),
   - RTR: `#R`, `#R0`, `#R8`, `#R9` (błąd), `##1#R` (błąd),
   - TLV: tekst, `hex:`, typ `165` i `0xA5`, typ `256` (błąd), wartość 61 i 62 B,
   - prefiksy `CAN1:`, `can2:`, wszystkie tryby `MODE#…`.
3. Dla każdego wektora: linia przez COM22 → odpowiedź węzła (`Ramka:` lub `ERR:`) → ramka odebrana
   adapterem candleLight (ID, ext, FD, BRS, RTR, dane).
   Porównanie z oczekiwaniem: odpowiedź tekstowa == `CanFrame.to_display()`, błąd == treść `CanFrameError`.
4. Scenariusze `CFG`: wszystkie tryby i prędkości z matrycy `can_fd_test` (adapter przełączany przez
   `can_fd_test.FdTester.open()`), `CFG` z 5M → `ERR`, `CFG?`, `BUS#2` z `can2 == NULL` → `ERR`.
5. Raport: `docs/can_bus/can_uart_bridge_test_report.md` (PASS/FAIL na wektor, różnice tekstowe jako diff).

**Kryteria akceptacji:** 100% wektorów zgodnych. Odstępstwa celowe (np. `?` zamiast `\ufffd`) są wymienione w raporcie.

---

### T9 – Dokumentacja

**Pliki:** `docs/can_bus/can_uart_bridge.md` (nowy), `docs/changelog.md`,
`docs/can_bus/can_porting_guide.md` (flagi BRS/RTR, port UART), `gemini.md` (sekcja modułów)

1. Instrukcja użytkownika: składnia z sekcji 3, przykłady, konfiguracja przycisków Serial Commandera
   (transport UART, EOL CRLF, format ASCII), ograniczenia (logi na tym samym UART, potwierdzenie = kolejka).
2. Opis architektury i kontraktu `uart_port.h`.
3. Wpis w changelogu.

---

### T10 – Etap 2 (osobne zadania, po akceptacji etapu 1)

| Zadanie | Opis |
|---|---|
| T10.1 `MON#<bus>#ON/OFF` | Wypisywanie ramek odebranych (`to_display`), ramki nadawane przez moduł oznaczone `TX` |
| T10.2 `LOG#OFF/ON` | Wyciszanie cyklicznych logów (`IO_MGR`, `MAIN`), żeby odpowiedzi nie mieszały się z logami |
| T10.3 Powtarzanie | `REP#<n>#<ms>#<ramka>` – seria ramek (testy obciążeniowe, diagnostyka adaptera PC) |
| T10.4 Licznik `{CNT}` | Odpowiednik `CNT_PATTERN` z Serial Commandera |

---

## 6. Kolejność i zależności

```text
T1 (port UART) ──────────────┐
T2 (flags) ──► T3 (adapter) ─┼──► T5 (linie) ──► T6 (polecenia) ──► T7 (integracja) ──► T8 (test) ──► T9 (docs)
T4 (parser, niezależny) ─────┘
```

| Zadanie | Zależy od | Szacunek |
|---|---|---|
| T1 Port UART | – | 0.5 dnia |
| T2 Flagi `CAN_msg_t` | – | 0.25 dnia |
| T3 Adapter BRS/RTR | T2 | 0.5 dnia |
| T4 Parser / formatter | T2 | 1 dzień |
| T5 Odbiór linii | T1 | 0.5 dnia |
| T6 Polecenia | T3, T4, T5 | 1 dzień |
| T7 Integracja | T6 | 0.25 dnia |
| T8 Test zgodności | T7 | 1 dzień |
| T9 Dokumentacja | T8 | 0.5 dnia |

Proponowane commity (Conventional Commits, każdy kompilowalny):
1. `feat(port): add UART port with interrupt driven reception` (T1)
2. `feat(can): add per-frame BRS and RTR flags to CAN_msg_t and the FDCAN adapter` (T2, T3)
3. `feat(can): add cansend text frame parser and formatter` (T4)
4. `feat(can): add UART to CAN bridge module` (T5, T6, T7)
5. `test(tools): add UART to CAN bridge compatibility test` (T8)
6. `docs(can): document UART to CAN bridge` (T9)

---

## 7. Definition of Done

- [ ] Decyzje D1, D3, D4 i zarezerwowane ID OTA potwierdzone.
- [ ] Każda linia z przycisku Serial Commandera (składnia CAN) wysłana przez UART daje na magistrali ramkę
      identyczną z tą, którą Serial Commander nadałby przez adapter CAN (T8: 100% zgodności).
- [ ] `CFG` działa dla CAN1 i CAN2 we wszystkich trybach z matrycy `can_fd_test`. Nieosiągalna konfiguracja
      zostawia poprzednią.
- [ ] Brak HAL poza `app/port`, callbacki ISR nie blokują, priorytet przerwania USART2 = 5.
- [ ] Build produkcyjny i `CAN_FD_TEST` bez ostrzeżeń, wolna sterta FreeRTOS ≥ 2 KB.
- [ ] OTA (discovery + aktualizacja) i logi `safe_printf` działają bez zmian.
- [ ] Dokumentacja i changelog zaktualizowane.
