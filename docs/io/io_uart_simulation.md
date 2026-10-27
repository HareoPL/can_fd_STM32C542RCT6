# Symulacja wejść przez UART (`io_cmd`, `io_sim`)

Wszystkie wejścia symulowanego auta (przełączniki, obroty silnika, poziom paliwa) można ustawiać
komendami tekstowymi przez UART, zamiast fizycznymi przyciskami i potencjometrem. Dla symulatorów aut,
które je nadają (np. Arkana), dochodzą sygnały bez fizycznego wejścia: prędkość, przebieg, światła drogowe,
pedał hamulca i temperatura silnika. Można też zasymulować uszkodzony zamek, czyli przekaźnik, który nie
reaguje na komendy CAN. Wejścia koduje do ramek CAN1 aktywny
[symulator auta](../car_sim/car_sim.md) (po resecie `V_BOX`: ramka telemetrii `0x01F2F3F4`). Komendy
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
| `ZRODLO#IO` | IO: natychmiastowy powrót do fizycznych przycisków i potencjometru, kasuje `AWARIA_ZAMKA` |
| `IO_RESET` | UART bez zmian: wszystkie wejścia 0, `OBROTY=0`, `PALIWO=0`, `AWARIA_ZAMKA=0` |

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
| `IO_HELP` | pomoc: tabela komend (tylko znaki ASCII) | – | – |
| `IO_RESET` | reset ustawień symulacji: wejścia 0, obroty 0, paliwo 0, zamek sprawny (źródło zostaje UART) | – | wszystkie bajty wejść = 0 (poza SW4) |
| `STACYJKA#<0\|1>` | ACC stacyjka (SW0, PB4) | 1 = ACC wł. | `data[0]` bit 0 |
| `DRZWI_INNE#<0\|1>` | Drzwi inne (SW1, PC8) | 1 = otwarte | `data[0]` bit 1 |
| `DRZWI_KIER#<0\|1>` | Drzwi kierowcy (SW2, PC7) | 1 = otwarte | `data[0]` bit 2 |
| `SWIATLA#<0\|1>` | Światła (SW3, PB7) | 1 = włączone | `data[0]` bit 3 |
| `AWARYJNE#<0\|1>` | Awaryjne (SW5, PB13) | 1 = włączone | `data[0]` bit 5 |
| `MASKA#<0\|1>` | Maska (SW6, PB14) | 1 = otwarta | `data[0]` bit 6 |
| `HAMULEC#<0\|1>` | Hamulec ręczny (SW7, PB15) | 1 = zaciągnięty | `data[0]` bit 7 |
| `OBROTY#<0..750>` | Obroty silnika (SW8, PB2) | RPM, **wielokrotność 3** | `data[2]` = RPM / 3 |
| `PALIWO#<0..100>` | Poziom paliwa (potencjometr, PA0) | % | `data[1]` |
| `AWARIA_ZAMKA#<0\|1>` | Uszkodzony zamek | 1 = przekaźnik nie reaguje na ramki CAN | – (bit 4 dalej z feedbacku) |
| `PREDKOSC#<0..250>` | Prędkość auta | km/h | – (np. Arkana `0x41C`) |
| `PRZEBIEG#<0..999999>` | Przebieg | km, domyślnie 10000; rośnie z prędkością | – (np. Arkana `0x438`) |
| `DROGOWE#<0\|1>` | Światła drogowe | 1 = włączone | – (np. Arkana `0x46A`) |
| `HAMULEC_NOZNY#<0\|1>` | Pedał hamulca | 1 = wciśnięty | – (np. Arkana `0x206`) |
| `TEMP_SILNIKA#<-40..150>` | Temperatura silnika | °C, domyślnie 90 | – (np. Arkana `0x417`) |

Kolumna „Ramka” podaje bajty symulatora `V_BOX`. Inne symulatory kodują te same wejścia w ramkach auta,
np. SW3 (`SWIATLA`) to światła, które czyta parser urządzenia (Arkana: mijania), tabela sygnałów:
[car_sim.md](../car_sim/car_sim.md#22-arkana-renault-arkana-parser-urządzenia-196). Sygnały auta (`PREDKOSC` ...
`TEMP_SILNIKA`) są przyjmowane zawsze w trybie UART, ale `ZRODLO?`, `IO_HELP` i `IO_RESET` pokazują je tylko
przy symulatorze, który je nadaje; dla `V_BOX` i `BRAK` teksty są takie jak przed wprowadzeniem symulatorów.
W trybie IO i po `IO_RESET` mają wartości domyślne (prędkość 0, przebieg 10000 km, drogowe i pedał wyłączone,
90 °C).

Komendy ustawiające (także `AWARIA_ZAMKA` i `IO_RESET`) działają tylko przy `ZRODLO=UART`. `ZRODLO?`, `ZRODLO#...` i `IO_HELP` działają
w obu trybach. Zmiana trafia do najbliższej ramki telemetrii (okres 100 ms).

Przy wyborze nazw unikano znaków hex: `ACC#01` nadal jest ramką CAN o ID `0xACC`, dlatego stacyjka ma
komendę `STACYJKA`. Linie, które nie są komendami IO, trafiają bez zmian do parsera ramek mostu.

## 3. Obroty silnika

Zakres i krok `OBROTY#` oraz obroty przełącznika SW8 (bieg jałowy) ustawia aktywny symulator auta:

| Symulator | `OBROTY#` | Krok | SW8 |
|---|---|---|---|
| `V_BOX`, `BRAK` | 0..750 | 3 | 750 |
| `ARKANA` | 0..8000 | 1 | 800 |

Przy zmianie symulatora obroty z UART są przycinane do maksimum i zaokrąglane w dół do kroku. Symulatory aut
nadają 0 obr/min przy wyłączonej stacyjce (D13); `V_BOX` nadaje surowe obroty z wejść, jak dotychczas.

Dla `V_BOX` bajt `data[2]` przenosi obroty ze skalą **3 RPM/bit**: firmware wysyła `RPM / 3`, a parser
odbiornika mnoży wartość przez 3. Przy symulatorze, który nadaje sygnały auta, odpowiedź `OBROTY#` nie podaje
`data[2]` (np. `[IO] OK OBROTY=2500 RPM`).

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
| `OK AWARIA_ZAMKA=1 (przekaźnik nie reaguje na ramki CAN)`, `OK AWARIA_ZAMKA=0` | Uszkodzenie zamka włączone / wyłączone |
| `OK IO_RESET: wejścia=0, OBROTY=0, PALIWO=0, AWARIA_ZAMKA=0` | Ustawienia symulacji wyzerowane |
| `ZRODLO=UART STACYJKA=1 ... ZAMKI=1(fiz) ... OBROTY=750 PALIWO=42 AWARIA_ZAMKA=0` | Odpowiedź na `ZRODLO?` |
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

### Symulacja uszkodzonego zamka (`AWARIA_ZAMKA`)

`AWARIA_ZAMKA#1` symuluje zamek, który nie reaguje na polecenia:
- ramka `0x01F2F3F0` jest odbierana i logowana (`RELAY CMD ... QUEUED`), ale
  `io_manager_setRelayState()` nie generuje zboczy na PA8 i zwraca `false`; w logu `IO_MGR` pojawia się
  `Relay command <n> ignored: simulated lock fault (AWARIA_ZAMKA)`,
- przekaźnik zostaje w dotychczasowym położeniu, więc bit 4 (ZAMKI) w ramce telemetrii pokazuje stan
  sprzed polecenia, czyli odbiornik widzi brak reakcji zamka,
- log statusu `IO_MGR` ma dopisek `| AWARIA ZAMKA`, a `ZRODLO?` pokazuje `AWARIA_ZAMKA=1`.

Uszkodzenie wyłącza `AWARIA_ZAMKA#0`, `IO_RESET`, `ZRODLO#IO` albo reset MCU. W trybie IO zamek zawsze
działa normalnie.

## 6. Architektura

```text
UART ISR -> can_uart (task mostu) -> process_line()
                                      |-- HELP / CFG / BUS / MODE
                                      |-- io_cmd_process()  -> io_sim_set*()     (sekcja krytyczna)
                                      `-- ramka CAN
io_manager task (10 ms) -> debounce + ADC (100 ms) -> io_manager_getPhysInputs()
car_sim task (10 ms)    -> io_sim_resolve() -> vehicle_state_build() -> ramki aktywnego symulatora -> CAN1
```

| Plik | Rola |
|---|---|
| `app/io/io_sim.c/.h` | Przełącznik źródła, wartości symulowane (także sygnały auta), flaga uszkodzonego zamka, reset ustawień, walidacja (zakresy, limity RPM z symulatora `io_sim_setRpmLimits()`, SW4), wartości wypadkowe. Bez zależności od HAL, a od FreeRTOS tylko przez sekcję krytyczną (`IO_SIM_HOST` dla testu hostowego) |
| `app/io/io_cmd.c/.h` | Parser komend i teksty odpowiedzi. Zwraca `false` dla linii, które nie są komendami IO, i zostawia je bez zmian |
| `app/io/io_manager.c` | Odczyt fizyczny (debounce, ADC co 100 ms), `io_manager_getPhysInputs()`, log statusu, blokada przekaźnika przy `AWARIA_ZAMKA` |
| `app/car_sim/` | Symulator auta: kodowanie wartości wypadkowych do ramek CAN1 ([car_sim.md](../car_sim/car_sim.md)) |
| `app/can_bus/can_uart_bridge/can_uart_bridge.c` | Wywołanie `io_cmd_process()` przed parserem ramek, linia `IO_HELP` w `HELP` |

Log `IO_MGR` co 500 ms (i przy zmianie):
`SRC: UART | BTN: 0x1BB [...] | PHYS: 0x010 | PA8: HIGH | ADC: 1720 ( 42%) | FUEL:  42% | RPM: 750`.
Pola `BTN`, `FUEL` i `RPM` to wartości wypadkowe (kodowane przez symulator auta), a `PHYS` i `ADC` to wejścia
fizyczne.

## 7. Build i testy

Komendy wymagają mostu UART (`CAN_UART_BRIDGE=ON`, domyślnie), bo `io_cmd.c` jest budowany razem z nim.
`io_sim.c` jest zawsze w buildzie. Bez mostu źródło pozostaje IO.

**Test hostowy** (bez sprzętu) kompiluje `io_cmd.c` + `io_sim.c` lokalnym `gcc` albo w obrazie
Dockera `vbox-builder`. Przechodzi scenariusz 142 kroków i sprawdza każdą odpowiedź oraz bajty
`data[0..2]` ramki: snapshot przy przełączeniu, SW4 z wejścia fizycznego, wszystkie przełączniki,
zakresy, krok RPM, `AWARIA_ZAMKA` (włączenie, kasowanie przez `ZRODLO#IO`), `IO_RESET`, błędy składni
tabelę `IO_HELP` (tylko ASCII, równa szerokość wierszy, wszystkie komendy) i linie przekazywane do
parsera ramek (`ACC#01`, `CAN1:...`), limity RPM z symulatora i sygnały auta (teksty z flagą sygnałów auta
i bez niej). Blokady przekaźnika (`io_manager.c`)
test hostowy nie obejmuje, sprawdza ją test na płytce (punkt 5).

```bash
python tools/test_io_cmd_host.py [-v]
```

**Test na płytce:**
1. Po resecie `ZRODLO?` pokazuje `ZRODLO=IO`, a ramka zmienia się z przyciskami.
2. `ZRODLO#UART` nie zmienia ramki, a przyciski fizyczne przestają na nią wpływać (poza zamkami).
3. Każda komenda zmienia właściwy bit lub bajt: `OBROTY#750` daje `data[2]=0xFA`, `OBROTY#99` daje
   `0x21`, a `OBROTY#100` zwraca `ERR`.
4. Przekaźnik działa z ramki `0x01F2F3F0`, a bit 4 ramki telemetrii pokazuje jego stan.
5. `AWARIA_ZAMKA#1`: ramka `0x01F2F3F0` nie przełącza przekaźnika (PA8 bez zboczy, log `ignored`),
   bit 4 ramki telemetrii się nie zmienia. Po `AWARIA_ZAMKA#0` przekaźnik znowu reaguje.
6. `IO_RESET` zeruje `data[0]` (poza bitem 4), `data[1]` i `data[2]` oraz wyłącza awarię zamka.
7. `ZRODLO#IO` przywraca wejścia fizyczne i sprawny zamek, a reset przywraca `ZRODLO=IO`.
