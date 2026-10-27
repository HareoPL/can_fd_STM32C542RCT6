# Most UART → CAN (`can_uart_bridge`)

Moduł nadaje ramki CAN / CAN FD na **CAN1 lub CAN2** na podstawie jednej linii tekstu wysłanej przez
UART (USART2, ST-LINK VCP, 115200 8N1). Składnia jest identyczna z Serial Commanderem
(`can_support.parse_can_frame()`), więc ten sam przycisk działa przez adapter CAN i przez UART.

```text
> 321##1#11 22 33 44 55 66 77 88 AA BB CC DD EE FF 00 11
[CAN_UART] Polecenie: 321##1#11 22 33 44 55 66 77 88 AA BB CC DD EE FF 00 11
[CAN_UART] Ramka: 321 [16] [FD+BRS] 11 22 33 44 55 66 77 88 AA BB CC DD EE FF 00 11
[CAN_UART] Rozmiar: 16B (FD: True, BRS: True) -> CAN1 OK
```

## Spis treści
1. [Szybki start](#1-szybki-start)
2. [Składnia ramek](#2-składnia-ramek)
3. [Polecenia sterujące](#3-polecenia-sterujące)
4. [Odpowiedzi i błędy](#4-odpowiedzi-i-błędy)
5. [Ramka a tryb magistrali](#5-ramka-a-tryb-magistrali)
6. [Ograniczenia](#6-ograniczenia)
7. [Architektura](#7-architektura)
8. [Build i testy](#8-build-i-testy)
9. [Szybkie komendy](#9-szybkie-komendy)

---

## 1. Szybki start

1. Wgraj firmware (most jest domyślnie włączony, opcja CMake `CAN_UART_BRIDGE=ON`).
   W logu startowym pojawi się `[CAN_UART] bridge ready (CAN1, CAN2), wpisz HELP`.
2. Otwórz port VCP (np. `COM22`, 115200 8N1) w terminalu albo w Serial Commanderze.
3. Ustaw tryb magistrali (po starcie profile parserów ustawiają Classic 500 kbps):
   ```text
   CFG#1#FD_BRS#500k#2M
   ```
4. Wyślij ramkę:
   ```text
   321##1#01 02 03
   ```

**Serial Commander:** transport *Serial (UART)*, port VCP, 115200, koniec linii **CRLF**, format ASCII.
Treścią przycisku jest jedna linia w składni z sekcji 2. Węzeł nie odsyła echa wpisywanych znaków.

Każda linia kończy się `\r`, `\n` albo `\r\n` (puste linie są ignorowane). Maksymalna długość to
255 znaków. Dozwolone są tylko znaki ASCII 0x20–0x7E.

## 2. Składnia ramek

Opcjonalny prefiks magistrali `CAN1:` / `CAN2:` (wielkość liter dowolna). Bez prefiksu ramka trafia na
magistralę domyślną (`BUS#n`, po starcie CAN1).

| Polecenie | Ramka |
|---|---|
| `187#01 00 00 00 40 7D 00 00` | Classic, 8 B |
| `3D0#` | Classic, DLC 0 |
| `321#01 02 … (9–64 B)` | automatycznie **FD + BRS**, gdy dane > 8 B |
| `321##1#01 02 03` | FD z BRS |
| `321##0#01 02 03` | FD bez BRS |
| `321##1010203` | FD z BRS bez drugiego `#` (pierwszy znak po `##` to flaga) |
| `65A#R`, `65A#R4` | ramka zdalna (RTR, tylko Classic) z DLC 0–8 |
| `TLV#321#0xA5#Hello` | TLV `ot_app_msg_tlv`: typ (u8), długość (u16 LE), wartość, dopełnienie zerami do 64 B, FD + BRS |
| `TLV#321#165#hex:01 02 03` | TLV z wartością binarną, typ dziesiętny albo `0x..` |
| `CAN2:18DAF110##0#01 02 03` | ramka na CAN2 |

Reguły (jak w `can_support.py`):

- **ID:** opcjonalne `0x`, maks. 8 cyfr hex. Rozszerzone (29-bit), gdy ma dokładnie 8 cyfr albo wartość
  > `0x7FF`. Wartość > `0x1FFFFFFF` to błąd.
- **Dane:** separatory `spacja . : _ -` są pomijane, liczba cyfr hex musi być parzysta, maks. 64 B
  (Classic 8 B).
- **Długość FD** nie jest kwantowana w poleceniu: 10 B wychodzi na magistralę z DLC 12 (dopełnienie zerami),
  odpowiedź raportuje 10 B.
- **TLV:** wartość maks. 61 B (3 B nagłówka + 61 B = 64 B).

## 3. Polecenia sterujące

| Polecenie | Działanie |
|---|---|
| `CFG#1#FD_BRS#500k#2M` | CAN1: FD z BRS, nominal 500 kbps, dane 2 Mbps |
| `CFG#2#FD#500k` | CAN2: FD bez BRS 500/500 kbps (`<data>` pomijane) |
| `CFG#1#CLASSIC#250k` | CAN1: Classic 250 kbps |
| `CFG?` | Konfiguracja obu magistral, TEC/REC, `tx_error_cnt`, `bus_off_cnt`, magistrala domyślna, `MODE`, liczniki UART |
| `BUS#2` / `BUS?` | Magistrala domyślna dla ramek bez prefiksu |
| `MODE#fd_no_brs` / `MODE?` | Wymuszony standard ramki: `auto`, `classic`, `fd_brs`, `fd_no_brs`, `tlv` (jak *can_mode* w Serial Commanderze) |
| `HELP` | Skrócona składnia |
| `IO_HELP`, `ZRODLO#UART`, `OBROTY#750`, ... | Symulacja wejść ramki telemetrii `0x01F2F3F4` (tag `[IO]`): [io_uart_simulation.md](../io/io_uart_simulation.md) |
| `RS485?`, `RS485#POLL`, `RS485_HELP` | Łącze RS485 z urządzeniem telematycznym: superStatus i parser urządzenia (tag `[RS485]`, opcja CMake `RS485_LINK`): [rs485_link.md](../rs485/rs485_link.md) |

Prędkości: bit/s (`500000`), z sufiksem `k` (`500k`) albo `M` (`2M`), dozwolony ułamek (`83.3k`), o ile
daje całkowitą liczbę bit/s. Tryb: `CLASSIC`, `FD`, `FD_BRS` (wielkość liter dowolna).

`CFG` czeka na opróżnienie kolejki TX (maks. 200 ms) i przełącza magistralę przez
`can_manager_setBusConfig()`. Konfiguracja nieosiągalna przy zegarze FDCAN 144 MHz (np. dane 5 Mbps)
zwraca `ERR` i **zostawia poprzednią konfigurację**. Ustawienie z UART obowiązuje do następnego wyboru
profilu parsera (`can_parser_select()`) albo do resetu. Filtry profilu się nie zmieniają.

Obsługiwane tryby i prędkości: [README – CAN / CAN FD Support](../../README.md#can--can-fd-support).

## 4. Odpowiedzi i błędy

Nadanie (3 linie, ramka w formacie `CanFrame.to_display()` z sufiksem TLV):

```text
[CAN_UART] Polecenie: CAN2:TLV#321#0xA5#Hello
[CAN_UART] Ramka: 321 [64] [FD+BRS] A5 05 00 48 65 6C 6C 6F 00 … 00 | TLV[0xA5, len=5]: "Hello"
[CAN_UART] Rozmiar: 64B (FD: True, BRS: True) -> CAN2 OK
```

`OK` oznacza **przyjęcie ramki do kolejki TX**. Późniejszy błąd sprzętowy (np. brak ACK, bus-off) widać
w `CFG?` (`tx_error_cnt`, `bus_off_cnt`, TEC/REC).

Błędy (jedna linia, treść błędów składni identyczna z `CanFrameError`):

```text
[CAN_UART] ERR: nieparzysta liczba cyfr hex w danych ('112', długość: 3). Każdy bajt w CAN to 2 cyfry hex (np. '1120' lub '0112').
[CAN_UART] ERR: CAN1 pracuje w trybie Classic - ramka FD odrzucona (CFG#1#FD_BRS#500k#2M)
[CAN_UART] ERR: kolejka TX CAN1 pełna
[CAN_UART] ERR: ID zarezerwowane dla OTA
[CAN_UART] ERR: konfiguracja nieosiągalna, pozostaje FD+BRS 500k/2M
[CAN_UART] ERR: CAN2 niedostępny
[CAN_UART] ERR: linia za długa (maks. 255 znaków)
```

## 5. Ramka a tryb magistrali

| Ramka \ magistrala | Classic | FD | FD + BRS |
|---|---|---|---|
| Classic, RTR | wysłana | wysłana | wysłana |
| FD bez BRS (`##0#`) | `ERR` | wysłana | wysłana bez BRS |
| FD z BRS (`##1#`, > 8 B) | `ERR` | `WARN`, wysłana bez BRS | wysłana z BRS |

BRS jest wybierany dla każdej ramki osobno (`CAN_MSG_FLAG_BRS_SET` w `CAN_msg_t.flags`), więc na magistrali
FD + BRS można wysłać także ramkę FD bez przełączania prędkości.

## 6. Ograniczenia

- **Ten sam UART co logi.** Odpowiedzi są wysyłane w jednym kawałku (`safe_write()`), ale mogą się przeplatać
  z cyklicznymi logami innych modułów.
- **Tempo poleceń.** Odpowiedź jest dłuższa od polecenia, więc przy ciągłym strumieniu bez czekania bufor RX
  (512 B) w końcu się przepełni (`WARN: przepełnienie bufora RX UART`, licznik `rx_overflow` w `CFG?`).
  Skrypty powinny czekać na odpowiedź po każdej linii.
- **ID zarezerwowane dla OTA** (`0x01F2F3DF`, `0x01F2F3E0`, `0x01F2F3E9`) są odrzucane.
- **Odbiór ramek** (podgląd magistrali) nie jest jeszcze obsługiwany.
- **Sufiks TLV:** znaki spoza ASCII 32–126 są pokazywane jako `?` (Serial Commander: U+FFFD albo surowy znak).

## 7. Architektura

```text
USART2 RX IRQ ─► stm32c5_uart_adapter ─(bajt, ISR)─► bufor kołowy 512 B ─► task can_uart
                                                                             │ składanie linii
                                          ┌──────────────────────────────────┼────────────────────┐
                                          ▼                                  ▼                    ▼
                                 can_frame_text_parse()            CFG / BUS / MODE     can_frame_text_format()
                                          │                                  │                    │
                                          ▼                                  ▼                    ▼
                                 can_manager_send()           can_manager_setBusConfig()     safe_write()
```

| Warstwa | Pliki | Zależności |
|---|---|---|
| Kontrakt UART | `app/port/uart_port.h` | `stdint`, `stdbool` |
| Adapter UART | `app/port/stm32c5/stm32c5_uart_adapter.c/.h` | HAL UART, NVIC (priorytet 5) |
| Parser / formatter | `app/can_bus/can_uart_bridge/can_frame_text.c/.h` | tylko `can_msg_types.h` (bez HAL, bez FreeRTOS) |
| Moduł | `app/can_bus/can_uart_bridge/can_uart_bridge.c/.h` | `can_manager`, `uart_port`, FreeRTOS, `safe_printf` |

Kontrakt `uart_port.h` obejmuje tylko odbiór. Nadawanie zostaje w `safe_printf` / `safe_write`:

```c
typedef void (*uart_rx_byte_fn)(uint8_t byte, void *context);   /* wywoływane z ISR */
typedef struct {
    bool (*start_rx)(void);                                        /* uzbraja odbiór na przerwaniu */
    void (*attach_rx_callback)(uart_rx_byte_fn cb, void *context);
    uint32_t (*get_rx_errors)(void);                               /* FE/NE/PE/ORE od startu (opcjonalne) */
} uart_interface_t;
```

HAL STM32C5 trzyma osobne stany `tx_state` i `rx_state`, więc blokujące nadawanie logów i odbiór
na przerwaniu na tym samym uchwycie USART2 nie blokują się nawzajem. Callback ISR tylko odkłada bajt
do bufora i budzi task (powiadomienie FreeRTOS). Odbiór jest uzbrajany dopiero z tasku, po starcie schedulera.

## 8. Build i testy

| Opcja CMake | Domyślnie | Znaczenie |
|---|---|---|
| `CAN_UART_BRIDGE` | `ON` | Moduł, adapter UART i task `can_uart` (stos 2 KB, priorytet `tskIDLE_PRIORITY + 2`) |

W buildzie `CAN_FD_TEST` bez `CAN_FD_TEST_LOOP` CAN2 nie jest uruchamiany, więc most obsługuje tylko CAN1.

**Test zgodności parsera z Serial Commanderem** (host, bez sprzętu): kompiluje `can_frame_text.c`
lokalnym `gcc` albo w obrazie Dockera `vbox-builder` i porównuje 168 wektorów z
`can_support.parse_can_frame()` (ID, format, BRS, RTR, dane, tekst odpowiedzi, treść błędów):

```bash
python tools/test_can_frame_text_host.py --serial-commander C:/WORK/01_tools/serial_commander
```

Plan implementacji i decyzje projektowe: [can_uart_bridge_tasks.md](can_uart_bridge_tasks.md).

---

## 9. Szybkie komendy

Każda komenda w osobnym bloku, do skopiowania i wysłania jedną linią (CRLF). Ramka FD wymaga magistrali
w trybie `FD` albo `FD_BRS` – najpierw ustaw konfigurację z sekcji 9.2.

### 9.1 Diagnostyka

Pomoc

```text
HELP
```

Konfiguracja i liczniki obu magistral

```text
CFG?
```

Magistrala domyślna

```text
BUS?
```

Wymuszony standard ramki

```text
MODE?
```

### 9.2 Konfiguracja magistrali

Wszystkie tryby z matrycy testowej. Po zmianie ustaw tę samą konfigurację w adapterze PC.

#### CAN1

Classic 125 kbps:

```text
CFG#1#CLASSIC#125k
```

Classic 250 kbps:

```text
CFG#1#CLASSIC#250k
```

Classic 500 kbps (domyślna po starcie):

```text
CFG#1#CLASSIC#500k
```

Classic 1 Mbps:

```text
CFG#1#CLASSIC#1M
```

CAN FD bez BRS 500/500 kbps:

```text
CFG#1#FD#500k
```

CAN FD bez BRS 1/1 Mbps:

```text
CFG#1#FD#1M
```

CAN FD + BRS 500 kbps / 1 Mbps:

```text
CFG#1#FD_BRS#500k#1M
```

CAN FD + BRS 500 kbps / 2 Mbps:

```text
CFG#1#FD_BRS#500k#2M
```

CAN FD + BRS 500 kbps / 4 Mbps:

```text
CFG#1#FD_BRS#500k#4M
```

CAN FD + BRS 500 kbps / 8 Mbps:

```text
CFG#1#FD_BRS#500k#8M
```

CAN FD + BRS 1 Mbps / 2 Mbps:

```text
CFG#1#FD_BRS#1M#2M
```

CAN FD + BRS 1 Mbps / 4 Mbps:

```text
CFG#1#FD_BRS#1M#4M
```

CAN FD + BRS 1 Mbps / 8 Mbps:

```text
CFG#1#FD_BRS#1M#8M
```

#### CAN2

Classic 125 kbps:

```text
CFG#2#CLASSIC#125k
```

Classic 250 kbps:

```text
CFG#2#CLASSIC#250k
```

Classic 500 kbps (domyślna po starcie):

```text
CFG#2#CLASSIC#500k
```

Classic 1 Mbps:

```text
CFG#2#CLASSIC#1M
```

CAN FD bez BRS 500/500 kbps:

```text
CFG#2#FD#500k
```

CAN FD bez BRS 1/1 Mbps:

```text
CFG#2#FD#1M
```

CAN FD + BRS 500 kbps / 1 Mbps:

```text
CFG#2#FD_BRS#500k#1M
```

CAN FD + BRS 500 kbps / 2 Mbps:

```text
CFG#2#FD_BRS#500k#2M
```

CAN FD + BRS 500 kbps / 4 Mbps:

```text
CFG#2#FD_BRS#500k#4M
```

CAN FD + BRS 500 kbps / 8 Mbps:

```text
CFG#2#FD_BRS#500k#8M
```

CAN FD + BRS 1 Mbps / 2 Mbps:

```text
CFG#2#FD_BRS#1M#2M
```

CAN FD + BRS 1 Mbps / 4 Mbps:

```text
CFG#2#FD_BRS#1M#4M
```

CAN FD + BRS 1 Mbps / 8 Mbps:

```text
CFG#2#FD_BRS#1M#8M
```

### 9.3 Magistrala domyślna i standard ramki

Ramki bez prefiksu `CAN1:` / `CAN2:` idą na magistralę domyślną. `MODE` wymusza standard każdej ramki
(jak *can_mode* w Serial Commanderze).

Ramki bez prefiksu na CAN1 (domyślnie):

```text
BUS#1
```

Ramki bez prefiksu na CAN2:

```text
BUS#2
```

Standard ze składni i długości (domyślnie):

```text
MODE#auto
```

Zawsze Classic (błąd przy `##` i > 8 B):

```text
MODE#classic
```

Zawsze CAN FD z BRS:

```text
MODE#fd_brs
```

Zawsze CAN FD bez BRS:

```text
MODE#fd_no_brs
```

Dane pakowane jako TLV typu 0xA5 (64 B, FD + BRS):

```text
MODE#tlv
```

### 9.4 Ramki Classic

ID 11-bit, 8 B:

```text
187#01 00 00 00 40 7D 00 00
```

ID 11-bit, DLC 0:

```text
3D0#
```

ID 29-bit (8 cyfr), 3 B:

```text
18DAF110#02 10 03
```

ID 29-bit z prefiksem 0x:

```text
0x18DAF110#02 10 03
```

Separatory w danych (`.` `:` `_` `-`):

```text
123#01.02:03_04-05
```

Ramka zdalna RTR, DLC 0:

```text
65A#R
```

Ramka zdalna RTR, DLC 8:

```text
65A#R8
```

### 9.5 Ramki CAN FD

FD z BRS, 3 B:

```text
321##1#01 02 03
```

FD bez BRS, 3 B:

```text
321##0#01 02 03
```

FD z BRS, 16 B:

```text
321##1#11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00
```

FD bez BRS, 16 B, ID 29-bit:

```text
18DAF110##0#11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00
```

Automatycznie FD + BRS (dane > 8 B):

```text
321#01 02 03 04 05 06 07 08 09 0A 0B 0C
```

FD z BRS, 64 B:

```text
321##1#00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F
```

FD bez BRS, 64 B:

```text
321##0#00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F
```

FD 10 B (na magistrali DLC 12, dopełnienie zerami):

```text
321##1#01 02 03 04 05 06 07 08 09 0A
```

### 9.6 Ramki TLV

Tekst, typ hex:

```text
TLV#321#0xA5#Hello
```

Tekst, typ dziesiętny, ID 29-bit:

```text
TLV#18DAF110#7#Hello world
```

Wartość binarna:

```text
TLV#321#165#hex:01 02 03
```

### 9.7 Wybór magistrali w ramce

Classic na CAN2:

```text
CAN2:187#01 00 00 00 40 7D 00 00
```

FD bez BRS na CAN2:

```text
CAN2:321##0#01 02 03
```

FD z BRS na CAN2:

```text
CAN2:321##1#11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00
```

RTR na CAN2:

```text
CAN2:65A#R4
```

TLV na CAN2:

```text
CAN2:TLV#321#0xA5#Hello
```

Classic na CAN1 (niezależnie od `BUS#`):

```text
CAN1:187#01 02 03
```

### 9.8 Przykładowa sesja: CAN FD 500k / 2M na CAN1

Kolejne komendy po jednej linii:

```text
CFG#1#FD_BRS#500k#2M
```

```text
321##1#11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00
```

```text
321##0#01 02 03
```

```text
187#01 02 03
```

```text
CFG?
```

```text
CFG#1#CLASSIC#500k
```

Ostatnia komenda przywraca Classic 500 kbps (konfigurację profilu po starcie).
