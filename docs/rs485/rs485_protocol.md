# Protokół RS485 urządzeń telematycznych (LX, LX_micro, Roma, Viale)

Specyfikacja binarnego protokołu na magistrali RS485 urządzenia telematycznego. Opracowana na podstawie
podsłuchu magistrali LX (2026-10-26, konwerter USB-RS485 na COM20) i starej dokumentacji superStatus
w wariancie UDP. Implementacja w v-box: [rs485_link.md](rs485_link.md).

## Spis treści
1. [Warstwa fizyczna](#1-warstwa-fizyczna)
2. [Ramka](#2-ramka)
3. [Adresy](#3-adresy)
4. [superStatus](#4-superstatus)
5. [Wgrywanie parsera urządzenia](#5-wgrywanie-parsera-urządzenia)
6. [Ruch na magistrali z programem serwisowym](#6-ruch-na-magistrali-z-programem-serwisowym)
7. [Wariant UDP](#7-wariant-udp)
8. [Wektory testowe](#8-wektory-testowe)

---

## 1. Warstwa fizyczna

| Parametr | Wartość |
|---|---|
| Interfejs | RS485 half-duplex, 2 przewody (A/B) |
| Prędkość | **57600 baud, 8N1** |
| Kolejność bajtów pól wielobajtowych | little-endian |

Po tej samej magistrali urządzenie wysyła także **tekstowy log diagnostyczny bez ramki** (ASCII z `\r\n`),
np. po starcie CAN:

```text
CanBus init start
Starting CANBus init
Searching for 195.CAN
CanBus file exist
!! [ENABLED CAN_NORMAL]
CAN1 INIT OK [BTR_500K]
```

Na magistrali zdarzają się **kolizje**: w zrzutach są ramki bez `AA 55`, bez samego `AA` albo zlepione z
tekstem logu. Odbiornik musi szukać `AA 55`, sprawdzać CRC i odrzucać resztę.

## 2. Ramka

```text
 0    1    2       3       4     5 ... 5+LEN-1
+----+----+-------+-------+-----+---------------------+
| AA | 55 | CRC L | CRC H | LEN | payload[LEN]        |
+----+----+-------+-------+-----+---------------------+
                          \______ zakres CRC ________/
payload[0] = odbiorca, payload[1] = nadawca
```

* `LEN`: liczba bajtów payloadu (2..255).
* **CRC-16/MODBUS**: wielomian `0xA001` (odwrócony `0x8005`), wartość początkowa `0xFFFF`, liczone po
  `LEN` + payload, zapisane little-endian.

```c
uint16_t crc = 0xFFFF;
for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
}
```

Przykład (heartbeat urządzenia): `AA 55 3E DC 05 A1 D5 4E 25 01` → LEN = 5, CRC(`05 A1 D5 4E 25 01`) = `0xDC3E`.

## 3. Adresy

Zapytanie ma postać `[odbiorca][nadawca][wersja u16][typ][...]`, a odpowiedź
`[nadawca zapytania][odbiorca zapytania][wersja fw u16][typ][...]`.

| Adres | Znaczenie |
|---|---|
| `0x90` | **Program serwisowy (PC)**: nadawca wszystkich zapytań, odbiorca odpowiedzi. v-box używa tego samego adresu. |
| `0x50` | superStatus |
| `0x24` | Zapis bloku pliku (wgrywanie parsera urządzenia) |
| `0x26` | Sterowanie wgrywaniem pliku (start, koniec, potwierdzenia) |
| `0x16` | Log tekstowy urządzenia w ramce (`90 16 "CAN INIT COMPLETE." 00`) |
| `0xD5` | Ramki systemowe urządzenia: heartbeat `A1 D5 4E 25 01` co ok. 5 s, odpowiedź „błędne hasło” |
| `0x1A`, `0xD6`, `0x11`, `0x28`, `0x37` (odp. `0x3B`), `0x1E` | Inne zapytania cyklu programu serwisowego, nierozpoznane |

* Wersja w zapytaniu programu serwisowego: `45 0F` = `0x0F45` (3909). Stara dokumentacja UDP podaje `0x0F42`.
* Wersja w odpowiedziach: wersja firmware urządzenia, np. `4E 25` = `0x254E` (9550). **To nie jest ID urządzenia.**

## 4. superStatus

### 4.1. Zapytanie (19 B)

| Offset ramki | Pole | Wartość |
|---|---|---|
| 0–1 | sync | `AA 55` |
| 2–3 | CRC16 | `62 1D` (dla hasła `9999`) |
| 4 | LEN | `0E` (14) |
| 5 | odbiorca | `50` |
| 6 | nadawca | `90` |
| 7–8 | wersja narzędzia | `45 0F` |
| 9 | typ | `01` |
| 10–18 | hasło, 9 B dopełnione zerami | `39 39 39 39 00 00 00 00 00` (`"9999"`) |

```text
AA 55 62 1D 0E 50 90 45 0F 01 39 39 39 39 00 00 00 00 00
```

### 4.2. Odpowiedź (LEN = 152)

Ramka `90 50`, przychodzi 80–140 ms po zapytaniu. Mapa pól pochodzi ze starej dokumentacji UDP: offset w
ramce UDP − 5 = indeks w payloadzie RS485. Zweryfikowano ją na rzeczywistych odpowiedziach LX.

| Payload | UDP | Typ | Pole | Przykład (LX, 2026-10-26) |
|---|---|---|---|---|
| 0 | 5 | u8 | odbiorca | `0x90` |
| 1 | 6 | u8 | nadawca | `0x50` |
| 2 | 7 | u16 | wersja firmware | 9550 |
| 4 | 9 | u8 | typ ramki | 1 |
| 5 | 10 | u32 | `time_date`: **sekundy od 2000-01-01 UTC** | 2026-10-26 12:33:39 |
| 9 | 14 | u8 | wejścia binarne (maska) | `0x00` |
| 10 | 15 | u8 | wyjścia binarne (maska) | `0x00` |
| 11 | 16 | i16 | napięcie zasilania, **prawdopodobnie 10 mV** | 1290 (12.90 V) |
| 13 | 18 | u8 | naładowanie baterii [%] | 0 |
| 14 | 19 | i8 | temperatura baterii [°C] | 39 |
| 15 | 20 | u8 | status zasilania | 3 |
| 16 | 21 | u32 | flow | 0 |
| 20 | 25 | u8 | liczba satelitów | 11 |
| 21 | 26 | u8 | status GPS | `0x8D` |
| 22 | 27 | u16 | HDOP | 80 |
| 24 | 29 | u8 | status GSM | `0x45` |
| 25 | 30 | 6 B | operator (MCC+MNC, ASCII) | `26002` |
| 31 | 36 | 20 B | SIM ID (ICCID, ASCII) | `8948022221120361975` |
| 51 / 53 | 56 / 58 | u16 / u16 | activity lo / hi | |
| 55 | 60 | 8 B | chip ID | |
| 63 | 68 | u8 | minor soft version | 1 |
| 64 | 69 | u8 | CAN car ID | 255 |
| **65** | **70** | **u16** | **rewizja modułu CAN**: 0 = plik parsera urządzenia nie jest wgrany (sekcja 4.4) | 1 |
| 67..73 | 72..78 | u16 ×4 | wersje zones / expander / temp_rh / exp_out | `0xFFFF` |
| 75..78 | 80..83 | u8 ×4 | ABS / ESP / TC / bateria podtrzymująca | |
| 79 | 84 | u32 | `time_date_rtc` (sekundy od 2000) | |
| 83..89 | 88..94 | u16 ×4 | device tester ok / err (lo, hi) | |
| 91..93 | 96..98 | u8 ×3 | wersja modemu GSM (major, minor, ext) | 1.4.3 |
| 94 | 99 | 16 B | IMEI (ASCII) | `353322082664526` |
| 110 | 115 | u16 | RTC counter | |
| 112 | 117 | u16 | wersja CAN locker | |
| 114 | 119 | u16 | status systemu | |
| 116 | 121 | u16 | pliki do wysłania | |
| **118** | **123** | **u16** | **parser urządzenia CAN1** (`NNN.CAN`) | **260** |
| **120** | **125** | **u16** | **parser urządzenia CAN2** | 0 |
| 122 | 127 | 8 B | wersja modemu (ASCII) | |
| 130 | 135 | 16 B | wersja aplikacji (ASCII) | |
| 146 | 151 | u16 | licznik dokumentów expandera | |
| 148 | 153 | u8 | tacho % | |
| 149 | 154 | u16 | status procesu tacho | |
| 151 | 156 | u8 | GPS sys / GSM RAT / SS | |

**Rozbieżności starej dokumentacji z danymi:**
* Czas to sekundy od **2000-01-01 UTC**, a nie czas Unix (dokumentacja podaje Unix time).
* `main_power_val` = 1290 przy zasilaniu ok. 12.9 V, więc jednostką jest raczej **10 mV**, a nie mV.
* Wersja narzędzia w zapytaniu: dokumentacja `0x0F42`, program serwisowy `0x0F45`.

### 4.3. Błędne hasło

Urządzenie odpowiada stałą ramką (sygnatura z dokumentacji, CRC poprawne):

```text
AA 55 BA 3D 05 90 D5 0C 06 02
```

### 4.4. Numer parsera urządzenia

`payload[118..119]` = numer pliku parsera dla CAN1 (`04 01` = 260 → `260.CAN`), `payload[120..121]` = CAN2.
Pole to **numer parsera z konfiguracji urządzenia**, a nie dowód, że plik jest wgrany. Test online
(2026-10-27) potwierdził to wprost: po zmianie konfiguracji 252 → 267 superStatus od razu podawał 267, choć
pliku `267.CAN` nie było jeszcze w urządzeniu.

To, czy plik parsera jest wgrany, pokazuje **`can_mod_revision` (`payload[65..66]`)**:

| Stan | `can1_dev_parser_no` | `can_mod_revision` | Log tekstowy urządzenia |
|---|---|---|---|
| Wgrany i działający `252.CAN` | 252 | 1 | |
| Konfiguracja zmieniona na 267, brak pliku | 267 | **0** | `Searching for 267.CAN` (bez `CanBus file exist`) |
| Wgrany `267.CAN` | 267 | 1 | `Searching for 267.CAN`, `CanBus file exist`, `CAN1 INIT OK [BTR_500K]` |

Wartość 1 to prawdopodobnie rewizja z nagłówka pliku parsera (`09 00 01 00 ...`). Na razie jest tylko jedna
obserwacja przejścia 1 → 0 → 1.

## 5. Wgrywanie parsera urządzenia

Program serwisowy wgrywa plik `NNN.CAN` (natywny kod parsera, ładowany pod `0x0803C000` w urządzeniu):

| Krok | Kierunek | Payload | Uwagi |
|---|---|---|---|
| Start | PC → urz. | `26 90 00 00 20 "9999" 00×5 ... size u32 @17, nazwa @21` | np. `195.CAN`, 7552 B |
| ACK startu | urz. → PC | `90 26 <fw16> 04 01 00 ...` | |
| Blok | PC → urz. | `24 90 <adres u32> <128 B>` | adresy od `0x0803C000` co `0x80` |
| ACK bloku | urz. → PC | `90 26 <fw16> 04 08 00 00 <adres u32>` | |
| Koniec | PC → urz. | `26 90 45 0F 02 "9999" ... <adres u32 @14> <rozmiar u32 @18> <suma u16 @22>` | |
| ACK końca | urz. → PC | `90 26 <fw16> 04 02 00 ...` | |

Po ok. 2 s urządzenie restartuje CAN i wypisuje w logu tekstowym `Searching for NNN.CAN`,
`CanBus file exist` (przy nadpisaniu istniejącego pliku dodatkowo `File [NNN.CAN] deleted`) oraz
`CAN1 INIT OK [BTR_500K]`.

Bajt 4 nagłówka obrazu parsera **nie jest** numerem parsera (dla 260 ma wartość `0xFF`).

### 5.1. Zmiana konfiguracji i wgrywanie online (przez GSM)

Gdy program serwisowy łączy się z urządzeniem online, a nie przez RS485, ani zmiana konfiguracji, ani
wgrywanie pliku `NNN.CAN` **nie wysyła na RS485 żadnej ramki** (brak `26 90` / `24 90` i brak ramki
powiadomienia). Zmianę widać tylko:
* w superStatus: `can1_dev_parser_no` zmienia się od razu po zmianie konfiguracji, a `can_mod_revision`
  przechodzi na 0 (brak pliku) i z powrotem na wartość niezerową po wgraniu pliku (sekcja 4.4),
* w logu tekstowym urządzenia po restarcie CAN (`Searching for NNN.CAN`, `CanBus file exist`, `CAN1 INIT OK`).

## 6. Ruch na magistrali z programem serwisowym

Podłączony program serwisowy co ok. 5 s odpytuje urządzenie w stałym cyklu:

```text
1A 90 -> (90 1A)   1E 90 -> (90 1E)   D6 90   11 90 -> 90 11   28 90 -> 90 28
A1 D5 (heartbeat urządzenia)   37 90 -> 90 3B   50 90 -> 90 50 (superStatus)
```

Przycisk superStatus w programie serwisowym wysyła dodatkowe zapytanie `50 90` poza cyklem (identyczne
bajty). Zdarza się, że urządzenie nie odpowiada, więc zapytanie trzeba ponawiać.

## 7. Wariant UDP

Ten sam protokół działa po UDP (port 8887). Zamiast `AA 55` na początku ramki jest Device ID (u16 LE), a
reszta ramki (CRC, LEN, payload) jest identyczna.

## 8. Wektory testowe

Rzeczywiste ramki z podsłuchu (superStatus z parserem 194 i 260, błędne hasło, ramki wgrywania, kolizje,
log tekstowy) są w `tools/test_rs485_host.py`. Test uruchamia kodek z `app/rs485/` na PC:

```bash
python tools/test_rs485_host.py -v
```
