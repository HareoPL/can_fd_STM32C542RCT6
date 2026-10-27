# Łącze RS485 z urządzeniem telematycznym (`app/rs485/`)

v-box odpytuje przez RS485 (USART1) **superStatus** urządzenia telematycznego (LX, LX_micro, Roma, Viale)
i zgłasza, gdy zmieni się **parser urządzenia**, czyli plik parsera CAN `NNN.CAN` wgrany do urządzenia.
Protokół: [rs485_protocol.md](rs485_protocol.md).

> **Nazewnictwo:** „parser urządzenia” (`dev_parser_no`, `can1_dev_parser_no`) to parser wgrany do
> zewnętrznego urządzenia. Nie jest to profil parsera CAN v-box (`can_parser_*`, `cp_*`). Kodek ramek
> RS485 nazywa się `rs485_frame` (dekoder), a nie „parser”.

## Spis treści
1. [Szybki start](#1-szybki-start)
2. [Architektura](#2-architektura)
3. [Dostęp do magistrali](#3-dostęp-do-magistrali)
4. [Konfiguracja](#4-konfiguracja)
5. [Logi i komendy konsoli](#5-logi-i-komendy-konsoli)
6. [Testy](#6-testy)
7. [Otwarte kwestie](#7-otwarte-kwestie)

---

## 1. Szybki start

1. Podłącz transceiver RS485 (z automatycznym przełączaniem DE/RE): **PA6 (USART1_TX, CN10 pin 13) → DI**,
   **PA7 (USART1_RX, CN10 pin 15) ← RO**, linie A/B do urządzenia (złącze v-box pin 1 / 2).
2. Wgraj firmware. Moduł jest domyślnie włączony (opcja CMake `RS485_LINK=ON`). W logu (USART2) pojawi się:
   ```text
   [RS485] link ready (superStatus co 5000 ms, src 0x90), wpisz RS485_HELP
   ```
3. Po pierwszej odpowiedzi urządzenia:
   ```text
   [RS485] komunikacja z urządzeniem OK (fw 9550, IMEI 353322082664526)
   [RS485] parser urządzenia CAN1: 260
   ```
4. Po wgraniu innego parsera programem serwisowym:
   ```text
   [RS485] inny master na magistrali (program serwisowy) - nasłuch pasywny
   [RS485] wgrywanie pliku do urządzenia: 195.CAN (7552 B) - odpytywanie wstrzymane
   [RS485] wgrywanie pliku zakończone - superStatus za 3000 ms
   [RS485] ZMIANA parsera urządzenia CAN1: 260 -> 195
   ```

## 2. Architektura

```text
USART1 RX ISR ──► ring buffer 1024 B ──► vTaskNotifyGiveFromISR ──► rs485_link_task
 (stm32c5_uart1_adapter)                                              │
                                                                      ├─► rs485_frame decoder (AA 55, CRC, resync)
                                                                      ├─► superstatus_decode() ──► ostatni superStatus
                                                                      │                          └─► callback zmiany parsera
                                                                      └─► superstatus_build_request() ──► uart->transmit()
```

| Plik | Zawartość |
|---|---|
| `app/rs485/rs485_frame.c/.h` | Budowanie ramek, CRC-16/MODBUS, dekoder strumieniowy: szuka `AA 55`, przy błędnym CRC odrzuca tylko pierwszy bajt (prawdziwa ramka „połknięta” przez fałszywy sync zostaje odzyskana), po ciszy na magistrali (`rs485_frame_decoder_idle()`) porzuca niepełną ramkę. Czysty C. |
| `app/rs485/superstatus.c/.h` | Zapytanie superStatus, dekoder odpowiedzi do `superstatus_t`, rozpoznanie „błędnego hasła”, formatowanie czasu urządzenia (sekundy od 2000). Czysty C. |
| `app/rs485/rs485_link.c/.h` | Task `rs485` (priorytet `tskIDLE_PRIORITY + 2`, stos 512 słów), odpytywanie, reguły dostępu do magistrali, callback zmiany parsera, komendy konsoli. |
| `app/rs485/rs485_link_config.h` | Stałe konfiguracyjne (sekcja 4). |
| `app/port/uart_port.h` | Kontrakt portu z opcjonalnym `transmit()` (USART2: `NULL`, USART1: `HAL_UART_Transmit()`). |

**Inicjalizacja (`main.c`):**

```c
rs485_link_set_parser_change_cb(on_dev_parser_change);   /* na razie tylko log */
rs485_link_init(stm32c5_uart1_interfaceGet());           /* attach_rx_callback(), start_rx() w tasku */
```

Callback `on_dev_parser_change(can_bus, old_no, new_no)` wywoływany jest z tasku RS485 (nie może blokować).
Przy pierwszym odczycie `old_no = RS485_DEV_PARSER_NO_UNKNOWN`. CAN2 bez parsera (0) przy pierwszym
odczycie nie generuje powiadomienia. W kolejnym etapie callback wybierze odpowiadający profil parsera CAN
v-box.

## 3. Dostęp do magistrali

Magistrala jest współdzielona z urządzeniem i z programem serwisowym, który używa tego samego adresu
nadawcy `0x90`. Reguły:

| Sytuacja | Zachowanie |
|---|---|
| Normalna praca | superStatus co `RS485_LINK_POLL_PERIOD_MS` (5 s), pierwsze zapytanie 1 s po starcie |
| Przed nadaniem | wymagana cisza na magistrali `RS485_LINK_BUS_IDLE_MS` (20 ms) |
| Brak odpowiedzi | timeout 300 ms, do 3 powtórzeń; po 3 kolejnych nieudanych cyklach log „utrata komunikacji” |
| Ramka innego mastera (`xx 90`) | nasłuch pasywny przez 15 s od ostatniej takiej ramki; odpowiedzi superStatus wywołane przez program serwisowy są dekodowane |
| Echo własnego zapytania (≤ 50 ms) | ignorowane, nie przełącza w tryb pasywny |
| Wgrywanie pliku (`24 90` / `26 90`) | zapytania wstrzymane do ACK końca (`90 26 .. 04 02`), potem superStatus po 3 s |
| Każda odpowiedź superStatus | restart okresu odpytywania (przy podłączonym programie serwisowym v-box nie nadaje) |
| Błędne hasło | log `ERROR`, kolejna próba po okresie odpytywania |

## 4. Konfiguracja

`app/rs485/rs485_link_config.h`. Każdą wartość można nadpisać z CMake (`target_compile_definitions`).

| Stała | Domyślnie | Opis |
|---|---|---|
| `RS485_LINK_SRC_ADDR` | `0x90` | Adres nadawcy zapytań (program serwisowy) |
| `RS485_LINK_TOOL_VERSION` | `0x0F45` | Wersja narzędzia w zapytaniu |
| `RS485_LINK_PASSWORD` | `"9999"` | Hasło urządzenia (maks. 9 znaków) |
| `RS485_LINK_POLL_PERIOD_MS` | 5000 | Okres superStatus |
| `RS485_LINK_RESP_TIMEOUT_MS` | 300 | Timeout odpowiedzi |
| `RS485_LINK_RETRIES` | 3 | Powtórzenia po timeoucie |
| `RS485_LINK_LOST_POLLS` | 3 | Nieudane cykle do zgłoszenia utraty komunikacji |
| `RS485_LINK_BUS_IDLE_MS` | 20 | Cisza przed nadaniem |
| `RS485_LINK_FRAME_GAP_MS` | 50 | Cisza kończąca niepełną ramkę |
| `RS485_LINK_ECHO_WINDOW_MS` | 50 | Okno rozpoznania echa własnego zapytania |
| `RS485_LINK_PASSIVE_HOLD_MS` | 15000 | Nasłuch pasywny po ramce innego mastera |
| `RS485_LINK_TRANSFER_HOLD_MS` | 10000 | Wstrzymanie po ramce wgrywania pliku |
| `RS485_LINK_AFTER_TRANSFER_MS` | 3000 | superStatus po zakończeniu wgrywania |

## 5. Logi i komendy konsoli

Komendy wpisuje się w konsoli USART2 (ST-LINK VCP, 115200, most UART, `CAN_UART_BRIDGE=ON`):

| Komenda | Odpowiedź |
|---|---|
| `RS485?` | stan łącza (`OK` / `BRAK ODPOWIEDZI` / `BŁĘDNE HASŁO` / `BRAK DANYCH`), tryb (aktywny / pasywny / wstrzymany), liczniki, ostatni superStatus i numery parserów urządzenia |
| `RS485#POLL` | natychmiastowe zapytanie superStatus (także w trybie pasywnym, nie w trakcie wgrywania pliku) |
| `RS485_HELP` | lista komend |

Przykład `RS485?`:

```text
[RS485] link: OK, tryb: aktywny
[RS485] zapytania=12 odpowiedzi=12 timeouty=0 echo=0 obce=0 | ramki=24 CRC=0 pominięte=0 UART=0 overflow=0
[RS485] superStatus sprzed 1830 ms: fw=9550 czas=2026-10-27 08:15:02 UTC
[RS485] zasilanie=1290 (~12.90 V) temp=39C wej=0x00 wyj=0x00 GPS sat=11 hdop=80 GSM=0x45 op=26002
[RS485] IMEI=353322082664526 SIM=8948022221120361975 modem=1.4.3
[RS485] parser urządzenia: CAN1=260 CAN2=0
```

## 6. Testy

### 6.1. Test hostowy (bez sprzętu)

```bash
python tools/test_rs485_host.py -v
```

Kompiluje `rs485_frame.c` i `superstatus.c` (lokalny gcc albo Docker `vbox-builder`) i sprawdza je na
rzeczywistych ramkach z podsłuchu LX: zapytanie bajt w bajt jak program serwisowy, odpowiedzi superStatus
(parser 194 i 260), błędne hasło, ramki wgrywania, kolizje, log tekstowy, ramka w kawałkach, fałszywy sync,
błąd CRC, daty urządzenia.

### 6.2. Test na sprzęcie (konwerter USB-RS485 na PC)

`tools/rs485_sniffer.py` podsłuchuje magistralę, dekoduje superStatus i wgrywanie oraz zgłasza zmianę
parsera urządzenia. Opcja `--poll` pozwala wysyłać superStatus z PC bez v-box.

| # | Krok | Oczekiwany wynik |
|---|---|---|
| 1 | Sam konwerter PC + urządzenie: `python tools/rs485_sniffer.py --port COM20 --poll 5 --hide-cycle` | `<- superStatus ... parser urządzenia CAN1=...`, czyli urządzenie odpowiada na zapytanie z `src 0x90` / `0x0F45` |
| 2 | v-box + konwerter PC, bez urządzenia: sniffer bez `--poll` | co 5 s seria 4 × `-> superStatus (zapytanie)` co ok. 300 ms (zapytanie + 3 powtórzenia, poprawne CRC), w konsoli v-box `RS485?`: rosnące `zapytania` i `timeouty` |
| 3 | v-box + urządzenie (+ sniffer) | log `komunikacja z urządzeniem OK`, `parser urządzenia CAN1: N`, `RS485?` pokazuje superStatus |
| 4 | Program serwisowy podłączony | log `nasłuch pasywny`, v-box nie nadaje, `RS485?` dalej pokazuje świeży superStatus |
| 5 | Wgranie innego parsera programem serwisowym | `wgrywanie pliku do urządzenia: NNN.CAN`, `zakończone`, `ZMIANA parsera urządzenia CAN1: A -> B` |
| 6 | Odłączenie urządzenia | po ok. 15 s `brak odpowiedzi urządzenia ... utrata komunikacji` |

Jeśli w kroku 2 sniffer pokazuje `TX (echo)` albo v-box widzi swoje zapytania (`echo` > 0 w `RS485?`),
transceiver odbiera własne nadawanie. Echo jest ignorowane, ale warto to odnotować.

## 7. Otwarte kwestie

* **Adres nadawcy:** v-box podszywa się pod program serwisowy (`0x90`, `0x0F45`). Do sprawdzenia, czy
  urządzenie odpowiada też na inny adres / wersję (wtedy v-box i program serwisowy nie będą się mylić).
* **Jednostka `main_power`:** najpewniej 10 mV, do potwierdzenia przy innym napięciu zasilania.
* **Parser skonfigurowany a wgrany:** `can1_dev_parser_no` to numer z konfiguracji urządzenia (potwierdzone
  testem online). Obecnie v-box zgłasza zmianę już po zmianie konfiguracji, nawet gdy plik nie jest jeszcze
  wgrany. Do zrobienia: śledzić parę (numer, `can_mod_revision` ≠ 0) i podawać callbackowi stan „wgrany”
  ([rs485_protocol.md](rs485_protocol.md), sekcje 4.4 i 5.1).
* **Aktualizacja online (GSM)** nie wysyła na RS485 ramek wgrywania, więc pauza na czas wgrywania działa tylko
  przy wgrywaniu przez RS485.
* **Kolejny etap:** tabela „parser urządzenia → profil parsera CAN v-box” i automatyczne
  `can_parser_select()` w callbacku.
