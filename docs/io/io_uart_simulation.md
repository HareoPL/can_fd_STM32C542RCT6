# Symulacja wejść przez UART (`io_cmd`, `io_sim`)

Wszystkie wejścia ramki telemetrii `0x01F2F3F4` (przełączniki, obroty silnika, poziom paliwa) można
ustawiać komendami tekstowymi przez UART, zamiast fizycznymi przyciskami i potencjometrem. Komendy
przyjmuje ten sam UART i task co [most UART → CAN](../can_bus/can_uart_bridge.md) (USART2, ST-LINK VCP,
115200 8N1, linia zakończona CR, LF albo CRLF).

```text
> ZRODLO#UART
[IO] OK ZRODLO=UART
> DRZWI_KIER#1
[IO] OK DRZWI_KIER=1
> OBROTY#750
[IO] OK OBROTY=750 RPM (data[2]=0xFA)
> OBROTY#100
[IO] ERR: OBROTY musi być wielokrotnością 3 (np. 99 lub 102)
```

## Spis treści
1. [Źródło wejść](#1-źródło-wejść)
2. [Komendy](#2-komendy)
3. [Obroty silnika](#3-obroty-silnika)
4. [Odpowiedzi i błędy](#4-odpowiedzi-i-błędy)
5. [Zamki (SW4) i przekaźnik](#5-zamki-sw4-i-przekaźnik)
6. [Architektura](#6-architektura)
7. [Build i testy](#7-build-i-testy)

---

## 1. Źródło wejść

Źródło wybiera globalny przełącznik: **IO** (fizyczne wejścia) albo **UART** (wartości z komend).

| Zdarzenie | Źródło |
|---|---|
| Reset MCU / zasilanie | **IO**, zawsze (przełącznik jest tylko w RAM) |
| `ZRODLO#UART` | UART: ramka CAN budowana z wartości ustawionych komendami |
| `ZRODLO#IO` | IO: natychmiastowy powrót do fizycznych przycisków i potencjometru |

- **Przy przejściu IO → UART** wartości symulowane startują od bieżącego stanu fizycznego, więc ramka
  CAN nie zmienia się w chwili przełączenia. Komendy zmieniają potem tylko wskazane wejścia.
- Ponowne `ZRODLO#UART` w trybie UART **nie** nadpisuje ustawionych wartości.
- W trybie UART fizyczne przyciski i potencjometr **nie wpływają** na ramkę CAN (z wyjątkiem SW4, zob.
  [rozdział 5](#5-zamki-sw4-i-przekaźnik)). Nadal są próbkowane i pokazywane w logu (`PHYS`, `ADC`).
- Symulacja nie ma timeoutu, a tryb UART trwa do `ZRODLO#IO` albo do resetu.

## 2. Komendy

Format: `NAZWA#wartość`. Wielkość liter nie ma znaczenia, a spacje wokół `#` są pomijane.

| Komenda | Wejście | Wartości | Ramka `0x01F2F3F4` |
|---|---|---|---|
| `ZRODLO#UART` / `ZRODLO#IO` | źródło wejść | – | – |
| `ZRODLO?` | stan wszystkich wejść (wartości wysyłane na CAN) | – | – |
| `IO_HELP` | pomoc | – | – |
| `STACYJKA#<0\|1>` | ACC stacyjka (SW0, PB4) | 1 = ACC wł. | `data[0]` bit 0 |
| `DRZWI_INNE#<0\|1>` | Drzwi inne (SW1, PC8) | 1 = otwarte | `data[0]` bit 1 |
| `DRZWI_KIER#<0\|1>` | Drzwi kierowcy (SW2, PC7) | 1 = otwarte | `data[0]` bit 2 |
| `SWIATLA#<0\|1>` | Światła (SW3, PB7) | 1 = włączone | `data[0]` bit 3 |
| `AWARYJNE#<0\|1>` | Awaryjne (SW5, PB13) | 1 = włączone | `data[0]` bit 5 |
| `MASKA#<0\|1>` | Maska (SW6, PB14) | 1 = otwarta | `data[0]` bit 6 |
| `HAMULEC#<0\|1>` | Hamulec ręczny (SW7, PB15) | 1 = zaciągnięty | `data[0]` bit 7 |
| `OBROTY#<0..750>` | Obroty silnika (SW8, PB2) | RPM, **wielokrotność 3** | `data[2]` = RPM / 3 |
| `PALIWO#<0..100>` | Poziom paliwa (potencjometr, PA0) | % | `data[1]` |

Komendy ustawiające działają tylko przy `ZRODLO=UART`. `ZRODLO?`, `ZRODLO#...` i `IO_HELP` działają
w obu trybach. Zmiana trafia do najbliższej ramki telemetrii (okres 100 ms).

Przy wyborze nazw unikano znaków hex: `ACC#01` nadal jest ramką CAN o ID `0xACC`, dlatego stacyjka ma
komendę `STACYJKA`. Linie, które nie są komendami IO, trafiają bez zmian do parsera ramek mostu.

## 3. Obroty silnika

Bajt `data[2]` przenosi obroty ze skalą **3 RPM/bit**: firmware wysyła `RPM / 3`, a parser odbiornika
mnoży wartość przez 3.

| RPM | `data[2]` |
|---|---|
| 0 | `0x00` (silnik zatrzymany) |
| 3 | `0x01` |
| 99 | `0x21` |
| 750 | `0xFA` (maksimum) |

- Komenda przyjmuje **tylko wielokrotności 3**, a wartość nie jest zaokrąglana. Dla innej wartości
  odpowiedź podaje najbliższe poprawne wartości, np. `OBROTY#100` daje
  `ERR: OBROTY musi być wielokrotnością 3 (np. 99 lub 102)`.
- W trybie IO przełącznik SW8 daje 750 RPM (`0xFA`) albo 0 RPM, czyli tak samo jak przed wprowadzeniem
  symulacji.
- W trybie UART bit SW8 w logu i w `ZRODLO?` jest ustawiony, gdy obroty są większe od 0.

## 4. Odpowiedzi i błędy

Każda komenda dostaje **jedną** odpowiedź z tagiem `[IO]`, wysyłaną od razu po przyjęciu komendy.

| Odpowiedź | Przyczyna |
|---|---|
| `OK DRZWI_KIER=1`, `OK PALIWO=42%`, `OK OBROTY=99 RPM (data[2]=0x21)`, `OK ZRODLO=UART` | Komenda przyjęta |
| `ZRODLO=UART STACYJKA=1 ... ZAMKI=1(fiz) ... OBROTY=750 PALIWO=42` | Odpowiedź na `ZRODLO?` |
| `ERR: źródło IO, najpierw ZRODLO#UART` | Komenda ustawiająca przy `ZRODLO=IO` |
| `ERR: OBROTY musi być wielokrotnością 3 (np. 99 lub 102)` | RPM niepodzielne przez 3 |
| `ERR: OBROTY poza zakresem 0..750`, `ERR: PALIWO poza zakresem 0..100` | Wartość poza zakresem |
| `ERR: MASKA: wartość musi być 0 lub 1` | Przełącznik z wartością inną niż 0 / 1 |
| `ERR: składnia: PALIWO#<0..100>`, `ERR: składnia: ZRODLO#<UART\|IO>` | Brak wartości, znaki inne niż cyfry, nieznane źródło |
| `ERR: ZAMKI = sprzężenie przekaźnika, tylko odczyt` | Próba ustawienia SW4 |

Zakres jest sprawdzany przed źródłem, więc `OBROTY#751` przy `ZRODLO=IO` zwraca błąd zakresu.

## 5. Zamki (SW4) i przekaźnik

SW4 (PB12, „Zamki ZAMKNIĘTE”) to sprzężenie zwrotne przekaźnika bistabilnego, którym steruje ramka CAN
`0x01F2F3F0`. Dlatego:
- bit 4 w ramce telemetrii **zawsze** pochodzi z wejścia fizycznego, także w trybie UART,
- `ZAMKI#...` zwraca błąd, a `ZRODLO?` pokazuje stan jako `ZAMKI=x(fiz)`,
- sterowanie przekaźnikiem (`io_manager_setRelayState()`, worker, weryfikacja feedbacku) nie zależy od
  źródła wejść.

## 6. Architektura

```text
UART ISR -> can_uart (task mostu) -> process_line()
                                      |-- HELP / CFG / BUS / MODE
                                      |-- io_cmd_process()  -> io_sim_set*()     (sekcja krytyczna)
                                      `-- ramka CAN
io_manager task (10 ms) -> debounce + ADC -> io_sim_resolve() -> io_manager_canCreateFrame() -> CAN1
```

| Plik | Rola |
|---|---|
| `app/io/io_sim.c/.h` | Przełącznik źródła, wartości symulowane, walidacja (zakresy, krok RPM, SW4), wartości wypadkowe. Bez zależności od HAL, a od FreeRTOS tylko przez sekcję krytyczną (`IO_SIM_HOST` dla testu hostowego) |
| `app/io/io_cmd.c/.h` | Parser komend i teksty odpowiedzi. Zwraca `false` dla linii, które nie są komendami IO, i zostawia je bez zmian |
| `app/io/io_manager.c` | Odczyt fizyczny (debounce, ADC co 100 ms), `io_manager_getPhysInputs()`, budowa ramki z wartości wypadkowych, log statusu |
| `app/can_bus/can_uart_bridge/can_uart_bridge.c` | Wywołanie `io_cmd_process()` przed parserem ramek, linia `IO_HELP` w `HELP` |

Log `IO_MGR` co 500 ms (i przy zmianie):
`SRC: UART | BTN: 0x1BB [...] | PHYS: 0x010 | PA8: HIGH | ADC: 1720 ( 42%) | FUEL:  42% | RPM: 750`.
Pola `BTN`, `FUEL` i `RPM` to wartości wysyłane na CAN, a `PHYS` i `ADC` to wejścia fizyczne.

## 7. Build i testy

Komendy wymagają mostu UART (`CAN_UART_BRIDGE=ON`, domyślnie), bo `io_cmd.c` jest budowany razem z nim.
`io_sim.c` jest zawsze w buildzie. Bez mostu źródło pozostaje IO.

**Test hostowy** (bez sprzętu) kompiluje `io_cmd.c` + `io_sim.c` lokalnym `gcc` albo w obrazie
Dockera `vbox-builder`. Przechodzi scenariusz 58 kroków i sprawdza każdą odpowiedź oraz bajty
`data[0..2]` ramki: snapshot przy przełączeniu, SW4 z wejścia fizycznego, wszystkie przełączniki,
zakresy, krok RPM, błędy składni i linie przekazywane do parsera ramek (`ACC#01`, `CAN1:...`).

```bash
python tools/test_io_cmd_host.py [-v]
```

**Test na płytce:**
1. Po resecie `ZRODLO?` pokazuje `ZRODLO=IO`, a ramka zmienia się z przyciskami.
2. `ZRODLO#UART` nie zmienia ramki, a przyciski fizyczne przestają na nią wpływać (poza zamkami).
3. Każda komenda zmienia właściwy bit lub bajt: `OBROTY#750` daje `data[2]=0xFA`, `OBROTY#99` daje
   `0x21`, a `OBROTY#100` zwraca `ERR`.
4. Przekaźnik działa z ramki `0x01F2F3F0`, a bit 4 ramki telemetrii pokazuje jego stan.
5. `ZRODLO#IO` przywraca wejścia fizyczne, a reset przywraca `ZRODLO=IO`.
