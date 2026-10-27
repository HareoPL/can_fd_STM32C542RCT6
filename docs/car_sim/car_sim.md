# Symulatory aut (`app/car_sim/`)

v-box zachowuje się jak **auto**, którego parser jest wgrany do urządzenia telematycznego (LX). Numer parsera
urządzenia CAN1 odczytany przez [RS485](../rs485/rs485_link.md) wybiera symulator: v-box nadaje na CAN1 ramki
tego auta (stacyjka, drzwi, obroty, zamek...) i reaguje na komendy LX tak jak auto (carsharing otwórz / zamknij
z PIN-em → przekaźnik zamka). Dla numeru bez symulatora v-box nadaje tylko ramkę znacznikową, żeby test
parsera nie wyglądał na udany z danymi innego auta.

> **Nazewnictwo:** **parser urządzenia** (`dev_parser_no`) to plik `NNN.CAN` wgrany do LX.
> **Symulator auta** (`car_sim_*`, `sim_<auto>.c`) to profil v-box, który nadaje ramki auta.
> **Profil parsera CAN v-box** (`can_parser_*`, `cp_*`) to filtry RX v-box. Plan i decyzje D1–D13:
> [car_sim_tasks.md](car_sim_tasks.md). Dodawanie auta: [adding_a_car.md](adding_a_car.md).

## Spis treści
1. [Szybki start](#1-szybki-start)
2. [Symulatory](#2-symulatory)
3. [Wybór symulatora](#3-wybór-symulatora)
4. [Komendy LX, PIN i pilot](#4-komendy-lx-pin-i-pilot)
5. [Komendy UART](#5-komendy-uart)
6. [Architektura](#6-architektura)
7. [CAN1: filtry, OTA, build CAN_FD_TEST](#7-can1-filtry-ota-build-can_fd_test)
8. [Testy](#8-testy)
9. [Ograniczenia i etap 2](#9-ograniczenia-i-etap-2)

---

## 1. Szybki start

Po resecie aktywny jest `V_BOX` w trybie `AUTO`, czyli telemetria `0x01F2F3F4` bajt w bajt jak przed
wprowadzeniem symulatorów (LX z parserem 192 działa bez zmian):

```text
[CAR_SIM] symulator: telemetria v-box (parser urządzenia 192, CAN1 500k)
[CAR_SIM] sterta FreeRTOS: wolne 2304 B, minimum od startu 2304 B
```

Zmiana parsera w konfiguracji LX (online albo programem serwisowym) przełącza symulator po najbliższym
superStatus (co 5 s):

```text
[RS485] ZMIANA parsera urządzenia CAN1: 192 -> 196
[CAR_SIM] symulator: Renault Arkana (parser urządzenia 196, CAN1 500k)
```

Numer bez symulatora (D4):

```text
[RS485] ZMIANA parsera urządzenia CAN1: 196 -> 197
[CAR_SIM] WARN: brak symulatora dla parsera urządzenia 197, nadaję ramkę CA FE 00 BA BE 00
[CAR_SIM] symulator: brak symulatora (bez parsera urządzenia, CAN1 500k)
```

Stan i ręczny wybór z konsoli UART (USART2, jak [most UART → CAN](../can_bus/can_uart_bridge.md)):

```text
> SIM?
[SIM] SIM=ARKANA "Renault Arkana", tryb AUTO, parser urządzenia CAN1: 196
[SIM] CAN1 500k, ramek TX: 13, komend LX: 2, pilot: tak
[SIM] PIN: 1234 (domyślny)
[SIM] liczniki: TX nadane 1520, odrzucone 0; komendy LX odebrane 2, odrzucone przez PIN 0
> SIM#V_BOX
[SIM] OK SIM=V_BOX, tryb RECZNY
```

## 2. Symulatory

| Klucz | Nazwa | Parser urządzenia | CAN1 | Ramki TX | Komendy LX | Pilot |
|---|---|---|---|---|---|---|
| `V_BOX` | telemetria v-box | 192 (domyślny, D1) | Classic 500k | `0x01F2F3F4` ext, 100 ms | przekaźnik `0x01F2F3F0` ext | nie |
| `ARKANA` | Renault Arkana | 196 | Classic 500k | 13 ramek 11-bit, 100 ms | `0x46F` z PIN-em | `0x46F`, PIN `0x1234` |
| `BRAK` | brak symulatora | 0 i każdy numer bez symulatora | Classic 500k | `0x01F2F3FF` ext, DLC 6, `CA FE 00 BA BE 00`, 1000 ms | – | nie |

### 2.1. `V_BOX`

Ramka `0x01F2F3F4` (ext, DLC 8, 100 ms): `data[0]` = SW0..SW7, `data[1]` = paliwo 0..100 %,
`data[2]` = RPM / 3 (z wejść, **bez** reguły D13), `data[3]` = 0, `data[4..7]` = licznik ramek BE (zerowany przy
wyborze symulatora). Komenda `0x01F2F3F0` (ext): `data[2] & 0x01` = 1 zamyka, 0 otwiera przekaźnik, ramka
krótsza niż 3 B jest ignorowana. Szczegóły bajtów: [project_documentation.md, sekcja 5.3](../project_documentation.md).

### 2.2. `ARKANA` (Renault Arkana, parser urządzenia 196)

Ramki 11-bit, DLC 8, co 100 ms, nieznane bajty = 0. Sygnały tak, jak czyta je parser urządzenia 196:

| ID | Bajty / maska | Sygnał | Źródło v-box |
|---|---|---|---|
| `0x46C` | `[6] & 0x80` | stacyjka | SW0 / `STACYJKA#` |
| `0x0A1` | `[5..6]` BE, 0.125 obr/min na bit | obroty (0 przy wyłączonej stacyjce, D13) | SW8 (800) / `OBROTY#0..8000` |
| `0x41C` | `[0..1]` BE, 0.00625 km/h na bit (nigdy `[0] = FF`) | prędkość | `PREDKOSC#` |
| `0x46A` | `[1] & 0x04` / `[1] & 0x01` / `[3] & 0x40` | drzwi kierowcy / pozostałe / drogowe | SW2 / SW1 / `DROGOWE#` |
| `0x5BB` | `[4] & 0x10` (bit `0x08` nigdy razem z `0x10`) | mijania (D6) | SW3 / `SWIATLA#` |
| `0x418` | `[2] & 0x04` | zamek 1 | SW4 (sprzężenie przekaźnika) |
| `0x46B` | `[2] & 0x01`, `[2] & 0x02`, `[7] & 0x80` | zamek 2 | SW4 |
| `0x47F` | `[3]` / `[4] & 0x02` | paliwo (przyjęte %, do weryfikacji) / ręczny | potencjometr / SW7 |
| `0x47C` | `[4] & 0x40` | maska | SW6 |
| `0x206` | `[6..7]` BE = 100 przy wciśniętym, `[4] = 0x04` (bez zdarzenia ESP) | pedał hamulca | `HAMULEC_NOZNY#` |
| `0x438` | `[0..3]`: 28 bitów × 10 m nad dolnym półbajtem `[3]` (nigdy `FF FF FF F0`) | przebieg | `PRZEBIEG#`, rośnie z prędkością |
| `0x417` | `[0]` − 40 | temperatura silnika | `TEMP_SILNIKA#` |
| `0x3AA` | wszystkie bity 0 | kontrolki (check, olej, ładowanie) | – |

LX widzi zamek zamknięty tylko przy wszystkich czterech bitach `0x418` + `0x46B`. SW5 (awaryjne) parser 196
pomija. Sygnały spoza przycisków (prędkość, przebieg, drogowe, pedał hamulca, temperatura) ustawia się w
trybie `ZRODLO=UART` ([io_uart_simulation.md](../io/io_uart_simulation.md)).

### 2.3. `BRAK`

Jedyna ramka `0x01F2F3FF` (ext, DLC 6) `CA FE 00 BA BE 00` co 1000 ms, bez filtrów RX auta (tylko OTA).
LX z parserem bez symulatora dostaje więc na CAN1 ramkę, której żaden parser nie zna: nie raportuje danych
auta, a carsharing kończy się NACK `ER_WAKEUP_CAN`.

## 3. Wybór symulatora

| Zdarzenie | Tryb `AUTO` (po resecie, D3) | Tryb `RECZNY` |
|---|---|---|
| Numer parsera urządzenia CAN1 z RS485 (pierwszy odczyt lub zmiana) | symulator z mapy, numer bez symulatora i 0 → `BRAK` z logiem `WARN` | numer tylko zapamiętany: `[CAR_SIM] tryb RECZNY: parser urządzenia 197 zapamiętany` |
| `SIM#<KLUCZ\|numer>` | przejście na `RECZNY` i wybór | wybór |
| `SIM#AUTO` | od razu ostatni numer parsera urządzenia | przejście na `AUTO` i to samo |
| Utrata łącza RS485 | bez zmiany symulatora | bez zmiany |
| Numer parsera urządzenia CAN2 | tylko log `[RS485]` | tylko log |

Wybór wykonuje wyłącznie task `car_sim` (najbliższy tick 10 ms): czeka na opróżnienie kolejki TX CAN1
(maks. 50 ms), ustawia konfigurację magistrali i filtry auta, dołącza filtry BOOT_OTA, ustawia limity obrotów
(`OBROTY#`, SW8) i zeruje harmonogram ramek. Błąd konfiguracji CAN1 przywraca poprzedni symulator
(`[CAR_SIM] ERROR: konfiguracja CAN1 dla symulatora ... nieudana, powrót do ...`). Wybór nie jest zapisywany
we Flash: po resecie zawsze `V_BOX` / `AUTO`.

## 4. Komendy LX, PIN i pilot

Komendy LX → auto obsługuje `car_sim_on_rx()` w kontekście `can_rx_task` (tylko dopasowanie, kontrola PIN-u,
`io_manager_setRelayStateAsync()` i log):

```text
[CAR_SIM] RELAY CMD (ID=0x46F, len=8): target=0, PIN OK, QUEUED
[CAR_SIM] RELAY CMD (ID=0x01F2F3F0, len=8): target=1, QUEUED
```

- **Arkana:** `0x46F 32 PIN_H PIN_L 18 04 84 07 30` otwiera, `... 10 ...` zamyka przekaźnik. Inne bajty,
  ramka budzenia `0x46F 11 11 11 00 ...` i ramki krótsze niż 8 B nie wywołują akcji.
- **PIN ściśle (D8):** komenda z innym PIN-em niż PIN v-box nie robi nic, jak w aucie:
  `[CAR_SIM] WARN: 0x46F PIN F15C != 1234, komenda zignorowana (SIM_PIN#F15C)`. Podpowiedź w nawiasie
  dopasowuje v-box do PIN-u, którego LX nauczył się wcześniej (np. z prawdziwego auta).
- **PIN v-box:** domyślny z profilu (Arkana `0x1234`), `SIM_PIN#<hex4>` nadpisuje go w RAM dla każdego auta
  z pilotem, `SIM_PIN#DOMYSLNY` przywraca domyślny. Reset MCU też.
- **Pilot:** `PILOT#OTWORZ` / `PILOT#ZAMKNIJ` nadaje ramkę pilota auta z PIN-em v-box (Arkana `0x46F` z
  `[3] = 18` / `10`; LX uczy się PIN-u raz, z ramki „otwórz”) i przełącza przekaźnik, jak prawdziwy pilot.
  Auto bez pilota (`V_BOX`, `BRAK`): tylko przekaźnik. Pilot **nigdy nie jest wysyłany automatycznie**, więc
  scenariusz „LX bez PIN-u” (NACK `ER_OTHER_ON_SEAT`) zostaje testowalny.
- **Uszkodzony zamek:** `AWARIA_ZAMKA#1` blokuje przekaźnik dla każdego symulatora (komendy LX i pilot).
- **Budżet czasu:** odbiór `0x46F` → impuls przekaźnika 40 ms → SW4 po debouncingu (40 ms) → najbliższe ramki
  `0x418` / `0x46B` (≤ 100 ms), razem ok. 200 ms. LX sprawdza zamek po 500 ms.

## 5. Komendy UART

Wielkość liter nie ma znaczenia, spacje wokół `#` są pomijane. Odpowiedzi z tagiem `[SIM]`. Komendy tylko
zgłaszają żądania do tasku `car_sim`, więc log `[CAR_SIM] symulator: ...` pojawia się zaraz po odpowiedzi.

| Komenda | Działanie |
|---|---|
| `SIM?` | aktywny symulator, tryb, ostatni parser urządzenia CAN1, magistrala, liczba ramek, PIN (domyślny / nadpisany), liczniki: TX nadane i odrzucone (pełna kolejka), komendy LX odebrane i odrzucone przez PIN |
| `SIM_LIST` | tabela ASCII: klucz, nazwa, parsery urządzenia, magistrala, liczba ramek, pilot; `*` = aktywny |
| `SIM#<KLUCZ\|numer>` | wybór ręczny, tryb `RECZNY`: `SIM#ARKANA`, `SIM#196`, `SIM#BRAK`, `SIM#0` (= `BRAK`); numer bez symulatora → `[SIM] ERR: brak symulatora dla parsera urządzenia 197 (SIM_LIST)` |
| `SIM#AUTO` | powrót do automatu: `[SIM] OK tryb AUTO: parser urządzenia 196 -> SIM=ARKANA` |
| `SIM_PIN#<hex4>` | PIN v-box w RAM, dokładnie 4 cyfry hex: `SIM_PIN#F15C` |
| `SIM_PIN#DOMYSLNY` | PIN z profilu auta |
| `PILOT#<OTWORZ\|ZAMKNIJ>` | ramka pilota z PIN-em (auto z pilotem) i przekaźnik |
| `SIM_HELP` | lista komend |

Linie, które nie są komendami `SIM*` / `PILOT`, trafiają dalej do parsera ramek mostu. W buildzie `CAN_FD_TEST`
`SIM#...` zwraca `ERR: CAN1 zajęty przez test CAN FD, wybór symulatora wyłączony`.

## 6. Architektura

```text
RS485 task ── on_dev_parser_change(CAN1) ──► car_sim_request_dev_parser() ─┐
UART bridge ── car_sim_cmd_process() ──────► car_sim_request_*() ──────────┤  (żądania, bez blokowania)
                                                                            ▼
car_sim task (10 ms): żądania ─► select: cp_car_sim + can_parser_select + attach_boot_ota
                      io_manager_getPhysInputs() + io_sim_resolve() ─► vehicle_state_build() (D13)
                      przebieg += prędkość ─► harmonogram ─► car_sim_encode_frame() ─► can_manager_send(CAN1)
can_rx_task ─► filtr cp_car_sim ─► car_sim_on_rx() ─► car_sim_match_cmd() (PIN) ─► io_manager_setRelayStateAsync()
```

| Plik | Rola |
|---|---|
| `app/car_sim/car_sim_types.h` | Model stanu auta `vehicle_state_t`, opis profilu: ramki i sygnały jak w pliku DBC (`sim_frame_t`, `sim_signal_t`), komendy (`sim_cmd_t`), pilot (`sim_fob_t`) |
| `app/car_sim/car_sim_codec.c/.h` | Stan auta z wejść (D13), kodowanie sygnałów `BIT` / `UINT` BE / LE, dopasowanie komend z PIN-em, ramka pilota, harmonogram ramek, walidacja profilu. Czyste C (test hostowy) |
| `app/car_sim/car_sim_registry.c/.h` | Rejestr symulatorów `s_sims[]`, mapa „numer parsera urządzenia → symulator”, wyszukiwanie po kluczu, kontrola spójności. Czyste C |
| `app/car_sim/sims/sim_*.c/.h` | Profile aut jako dane `static const` (bez FreeRTOS, HAL i `io_manager`); hooki `encode` / `on_rx` / `on_select` tylko na wyjątki (`V_BOX`: licznik telemetrii) |
| `app/car_sim/car_sim.c/.h` | Silnik: task `car_sim`, wybór symulatora, `car_sim_on_rx()`, PIN, pilot, liczniki |
| `app/car_sim/car_sim_cmd.c/.h` | Komendy UART `SIM*` / `PILOT#` |
| `app/can_bus/can_parser/parsers/cp_car_sim.c/.h` | Profil parsera CAN v-box `CAN_PARSER_ID_CAR_SIM`: konfiguracja magistrali aktywnego auta i jeden filtr na unikalne ID komend (callback `car_sim_on_rx()`) |

- **Task `car_sim`:** priorytet `tskIDLE_PRIORITY + 2`, stos 512 słów, `vTaskDelayUntil` co 10 ms. Przy starcie
  loguje wolną stertę FreeRTOS.
- **Harmonogram:** ramka o okresie P ms nadawana w tickach `faza + k·P`, fazy rozłożone co 10 ms, więc w jednym
  ticku najwyżej `ceil(n / 10)` ramek 100 ms (Arkana: 2). Pełna kolejka TX: ramka odrzucona i policzona, log
  najwyżej co 5 s: `[CAR_SIM] WARN: kolejka TX CAN1 pełna, odrzucone ramki: 3 (razem 3)`.
- **Przebieg:** w trybie UART rośnie z prędkością (`PREDKOSC#`), w trybie IO prędkość jest 0.
- **Współbieżność:** aktywny profil to `volatile` wskaźnik na dane `static const`, zmieniany przed
  `can_parser_select()`, więc `can_rx_task` zawsze czyta poprawny profil. PIN to jedno słowo 32-bitowe
  (flaga + PIN), zapisywane atomowo.
- **`io_manager`** nie nadaje już telemetrii: odświeża wejścia i paliwo (ADC co 100 ms), log `IO_MGR` bez zmian.

## 7. CAN1: filtry, OTA, build `CAN_FD_TEST`

- `car_sim_init(&can1_inst, true)` w `main.c` zastępuje `can_parser_select(CAN_PARSER_ID_V_BOX)`. CAN2 bez
  zmian (`EXAMPLE`, bez OTA). `cp_v_box` zostaje w rejestrze `can_parser`, nieużywany.
- Po każdym wyborze symulatora filtry BOOT_OTA są dołączane ponownie, więc OTA na CAN1 działa przy prędkości
  aktywnego auta (D9).
- Przy starcie każdy symulator musi zmieścić swoje filtry (unikalne ID komend) razem z filtrami BOOT_OTA w
  `CAN_MAX_FILTERS` (16). Przekroczenie to błąd startu
  (`[CAR_SIM] ERROR: symulator X: 15 filtrów + 2 BOOT_OTA > 16`, `[MAIN] ERROR: car simulator init failed!`),
  a nie cicha utrata OTA. Kontroler ma dodatkowo limit 8 filtrów extended i 28 standard.
- `BRAK` nie ma filtrów auta: przy samych filtrach OTA kontroler przyjmuje do FIFO wszystkie ramki standard,
  a menedżer CAN je odrzuca (tak samo jak dotychczas dla `V_BOX`).
- **Build `CAN_FD_TEST`:** CAN1 należy do testu CAN FD (`cp_fd_test`). `car_sim_init(&can1_inst, false)` tylko
  nadaje `V_BOX` (jak wcześniej `io_manager`), nie zmienia profilu CAN1, a wybór symulatora (RS485, `SIM#`)
  jest wyłączony.

## 8. Testy

**Test hostowy** (bez sprzętu, lokalny `gcc` albo obraz Dockera `vbox-builder`):

```bash
python tools/test_car_sim_host.py --docker-image vbox-builder:latest [-v]
```

Harness `tools/host/car_sim_cli.c` kompiluje kodek, rejestr, wszystkie profile i komendy UART (na zaślepce
silnika). Scenariusz sprawdza: kodowania i D13, komendy z PIN-em (zgodny, niezgodny, nadpisany, budzenie,
obce ramki), pilota, harmonogram (13 ramek 100 ms: każda 10 razy na 1 s, w ticku najwyżej 2), rejestr i mapę
(192 → `V_BOX`, 196 → `ARKANA`, 197 i 0 → `BRAK`), wykrywanie błędnych profili, `V_BOX` bajt w bajt jak
dotychczas oraz komendy `SIM*` / `PILOT#`. **Dekodery referencyjne** w Pythonie (maski i wzory parsera
urządzenia 196 wpisane na stałe) odczytują ramki Arkany dla 27 stanów auta i muszą odtworzyć stan wejść.

**Test na sprzęcie** (LX z parserami 192 i 196, konsola v-box na USART2):

| # | Krok | Oczekiwany wynik |
|---|---|---|
| 1 | LX z parserem 192, v-box po resecie | `SIM?`: `V_BOX`, `AUTO`; LX jak przed zmianą (telemetria, przekaźnik z `0x01F2F3F0`); log sterty ≥ 2 KB wolnego |
| 2 | Zmiana konfiguracji LX na 196 (online lub RS485), potem wgranie `196.CAN` | od razu po zmianie konfiguracji: `[RS485] ZMIANA parsera urządzenia CAN1: 192 -> 196`, `[CAR_SIM] symulator: Renault Arkana ...` |
| 3 | LX bez PIN-u, carsharing otwórz (jeśli LX zna już PIN, pominąć i ustawić `SIM_PIN#<PIN z LX>`) | LX: NACK `ER_OTHER_ON_SEAT`, brak `0x46F` na CAN |
| 4 | `PILOT#OTWORZ` | LX zapisuje PIN `0x1234` (log konfiguracji), przekaźnik otwarty |
| 5 | Przełączniki SW0, SW1, SW2, SW3, SW6, SW7, SW8, potencjometr 0 / 50 / 100 % | LX pokazuje stacyjkę, drzwi, mijania, maskę, ręczny, obroty (tylko przy SW0), paliwo; ustalić jednostkę `0x47F [3]` i poprawić profil, jeśli to nie % |
| 6 | `ZRODLO#UART`, `PREDKOSC#50`, `PRZEBIEG#12345`, `DROGOWE#1`, potem `ZRODLO#IO` | prędkość, przebieg, drogowe w LX (przebieg z `0x438` dopiero po 3 × 10 s bez odpowiedzi UDS) |
| 7 | Warunki zamknięcia (SW0 = 0, SW1 = SW2 = SW3 = 0, SW7 = 1, iButton w LX), carsharing zamknij, potem otwórz | `[CAR_SIM] RELAY CMD (ID=0x46F, ...) PIN OK`, przekaźnik zmienia stan, SW4 go śledzi, LX: ACK w ≤ 1 s |
| 8 | `ZRODLO#UART`, warunki zamknięcia komendami (`STACYJKA#0`, `HAMULEC#1`, ...), `AWARIA_ZAMKA#1`, carsharing zamknij | przekaźnik nie reaguje na 6 prób, LX: NACK `ER_UNKNOWN` |
| 9 | `SIM_PIN#ABCD`, carsharing zamknij; potem `SIM_PIN#DOMYSLNY` | `WARN: 0x46F PIN 1234 != ABCD`, LX: NACK `ER_UNKNOWN`; po powrocie działa |
| 10 | Zmiana konfiguracji LX na numer bez symulatora (np. 197) | `[CAR_SIM] WARN: brak symulatora ...`, na CAN1 tylko `0x01F2F3FF CA FE 00 BA BE 00` co 1 s, carsharing → LX: NACK `ER_WAKEUP_CAN` po ok. 5 s budzenia |
| 11 | `SIM#V_BOX` przy parserze 196 | tryb `RECZNY`, LX traci dane auta (`CS_CAN_INACTIVE` po 1 s); `SIM#AUTO` przywraca Arkanę |
| 12 | Przez cały test: OTA discovery na CAN1 przy prędkości aktywnego auta | odpowiedzi po każdym przełączeniu |

## 9. Ograniczenia i etap 2

- Wolna sterta FreeRTOS po starcie (szacunek ok. 2.2 KB) do potwierdzenia logiem na sprzęcie. Poniżej 2 KB
  podnieść `configTOTAL_HEAP_SIZE`.
- Jednostka paliwa Arkany (`0x47F [3]`) przyjęta w %, do weryfikacji na LX.
- Parser urządzenia 196 liczy temperaturę silnika na `signed char`: powyżej 127 °C kontrolka temperatury w LX
  się nie zapala (v-box nadaje poprawne bajty).
- `can_parser_get_current_id()` jest wspólne dla obu magistral i po starcie zwraca profil CAN2 (wybierany
  ostatni). Nikt go nie wywołuje.
- **Etap 2** (plan, T13): odpowiedzi UDS przebiegu (`0x743` → `0x763`), uśpiona magistrala i budzenie, blokada
  silnika z CAN, klakson, zamknięcie awaryjne Start/Stop, ramki etapu 2 Arkany, kolejne auta, symulator na
  CAN2, zapis wyboru we Flash.
