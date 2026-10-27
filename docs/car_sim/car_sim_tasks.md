# Symulatory aut (`car_sim`) – zadania implementacyjne

Plan modułu, który zamienia v-box w symulator **konkretnego auta**: v-box nadaje na CAN1 ramki danego
modelu (stacyjka, obroty, drzwi, światła, zamek, ...), a urządzenie telematyczne (LX, LX_micro, Roma,
Viale) dekoduje je swoim prawdziwym parserem `NNN.CAN`. Symulator jest wybierany automatycznie na podstawie
numeru parsera urządzenia odczytanego przez RS485 ([rs485_link.md](../rs485/rs485_link.md)) albo ręcznie z UART.

## Spis treści
1. [Cel i zakres](#1-cel-i-zakres)
2. [Decyzje projektowe](#2-decyzje-projektowe)
3. [Analiza parsera LX 196 (Renault Arkana)](#3-analiza-parsera-lx-196-renault-arkana)
4. [Architektura](#4-architektura)
5. [Zadania](#5-zadania)
6. [Kolejność i zależności](#6-kolejność-i-zależności)
7. [Definition of Done](#7-definition-of-done)

---

## 1. Cel i zakres

**Cel:** v-box zachowuje się jak auto, którego parser jest wgrany do urządzenia. Zmiana parsera w urządzeniu
(zmiana konfiguracji lub wgranie pliku, przez RS485 albo online) przełącza symulator v-box bez udziału
człowieka. Gdy dla numeru parsera nie ma symulatora, v-box wyraźnie to sygnalizuje zamiast udawać inne auto.

**Zasada:** profil symulatora opisuje **auto**, nie parser. Parser urządzenia jest tylko źródłem wiedzy o
ramkach auta (ID, bity, wzory). v-box nadaje to, co nadawałoby auto, i reaguje na komendy tak jak auto (np. zły
PIN → brak reakcji, uszkodzony zamek → brak zmiany stanu). Symulatora nie dopasowujemy do błędów ani skrótów
parsera, bo test ma je wykazać.

**v-box jako auto:**

| Element auta | W v-box |
|---|---|
| stacyjka, drzwi, światła, maska, hamulec ręczny | SW0, SW1 / SW2, SW3, SW6, SW7 albo komendy UART (`ZRODLO#UART`) |
| silnik (obroty) | SW8 / `OBROTY#`; pracuje tylko przy włączonej stacyjce (D13) |
| poziom paliwa | potencjometr / `PALIWO#` |
| prędkość, przebieg, temperatura silnika, pedał hamulca, drogowe | komendy UART (T1); przebieg rośnie z prędkością |
| siłownik centralnego zamka | przekaźnik zamka v-box |
| czujnik stanu zamka | SW4 (sprzężenie przekaźnika) |
| uszkodzony zamek | `AWARIA_ZAMKA#1` |
| sterownik nadwozia przyjmujący komendy carsharingu (z PIN-em) | `car_sim_on_rx()` + PIN v-box |
| pilot (kluczyk) | `PILOT#OTWORZ` / `PILOT#ZAMKNIJ` |
| magistrala CAN auta | CAN1 z prędkością z profilu auta |

```text
LX: parser 196 w konfiguracji ──RS485 superStatus──► v-box: parser urządzenia CAN1 = 196
                                                            └─► symulator "Renault Arkana" ──CAN1──► ramki Arkany ──► LX
LX: carsharing cmd_close ──CAN1 0x46F (PIN)──► v-box: PIN OK → przekaźnik zamka ──SW4──► 0x418 + 0x46B "zamknięte" ──► LX: ACK
LX: parser 197 (brak symulatora) ──RS485──► v-box: symulator BRAK ──CAN1──► tylko ramka CA FE 00 BA BE 00
```

**Nazewnictwo (obowiązuje w kodzie i dokumentacji):**

| Pojęcie | Znaczenie | Nazwy w kodzie |
|---|---|---|
| **parser urządzenia** | plik `NNN.CAN` wgrany do LX (dekoduje ramki auta) | `dev_parser_no` |
| **symulator auta** | profil v-box, który nadaje ramki danego auta i reaguje na komendy LX | `car_sim_*`, `sim_<auto>.c` |
| **profil parsera CAN v-box** | istniejący mechanizm filtrów RX v-box | `can_parser_*`, `cp_*` |

**W zakresie (etap 1):**
- model stanu auta niezależny od marki, zasilany z przycisków / potencjometru albo z UART (jak dziś),
- deklaratywne profile aut (tablice ramek i sygnałów, jak plik DBC) z hookami na wyjątki,
- harmonogram nadawania ramek cyklicznych i obsługa komend carsharingu LX → auto (`cmd_open` / `cmd_close` →
  przekaźnik) z kontrolą PIN-u,
- rejestr symulatorów z mapą „numer parsera urządzenia → symulator”, wybór automatyczny i ręczny,
- symulator **`V_BOX`**: obecna ramka telemetrii `0x01F2F3F4` (`io_manager_canCreateFrame`) i ramka
  przekaźnika `0x01F2F3F0`, przypisany do parsera urządzenia **192**, bajt w bajt jak dziś,
- symulator **Renault Arkana** (parser urządzenia **196**) z pilotem (nauka PIN-u w LX),
- symulator **`BRAK`**: ramka znacznikowa `CA FE 00 BA BE 00` dla numeru parsera bez symulatora,
- komendy UART (`SIM?`, `SIM_LIST`, `SIM#...`, `SIM_PIN#...`, `PILOT#...`, nowe sygnały auta), testy
  hostowe, generator szkieletu.

**Poza zakresem (etap 2, T13):** odpowiedzi diagnostyczne UDS (przebieg z `0x743` → `0x763`), symulacja
uśpionej magistrali i budzenia, blokada silnika z CAN, zamknięcie awaryjne, kolejne auta, symulator na CAN2,
zapis wyboru we Flash.

---

## 2. Decyzje projektowe

Decyzje D1–D12 potwierdzone 2026-10-27, D13 wynika z zasady z sekcji 1 („v-box symuluje auto”).

| # | Decyzja | Ustalenie | Wpływ |
|---|---|---|---|
| D1 | Telemetria v-box | `io_manager_canCreateFrame` przestaje być wywoływane w `io_manager_task` i staje się symulatorem **`V_BOX`**, przypisanym do parsera urządzenia **192** (parser domyślny; plik `192.CAN` nie jest analizowany). | T4 |
| D2 | Moment przełączenia | Od razu po zmianie numeru parsera w konfiguracji urządzenia (`can1_dev_parser_no`), bez czekania na wgranie pliku (`can_mod_revision`). | T5, T6 |
| D3 | Tryb wyboru | `AUTO` (z RS485) albo `RECZNY` (`SIM#<auto>` blokuje automat do `SIM#AUTO`). Bez zapisu we Flash: po resecie `AUTO` + `V_BOX` do pierwszego odczytu superStatus. | T5, T7 |
| D4 | Numer parsera bez symulatora | Symulator **`BRAK`**: v-box przestaje nadawać ramki poprzedniego auta i nadaje tylko ramkę znacznikową `CA FE 00 BA BE 00`, log `WARN`. Ramki poprzedniego auta sugerowałyby, że testowany parser jest źle napisany. Dotyczy też numeru 0 (brak parsera). Utrata łącza RS485 nie zmienia symulatora. Ramka: ID ext `0x01F2F3FF` (rodzina v-box, bez kolizji z 11-bitowymi ID aut), DLC 6, co 1000 ms, CAN1 Classic 500 kbps. | T3, T5 |
| D5 | Obroty | Zakres i krok z profilu aktywnego symulatora: `V_BOX` 0..750 co 3 (bez zmian, te same komunikaty), auta 0..8000 co 1. SW8 (fizyczny) = obroty biegu jałowego z profilu (`V_BOX` 750, Arkana 800). | T1 |
| D6 | Światła | Każdy symulator koduje SW3 tak, jak nadaje je dane auto (bity znane z parsera urządzenia; sygnał `lights` może mieć kilka wpisów w tablicy). Arkana: SW3 = same mijania (`0x5BB [4] = 0x10`). Drogowe tylko z UART (`DROGOWE#`). | T1, T8 |
| D7 | Zamek | Komenda carsharingu z LX (`cmd_open` / `cmd_close`, Arkana `0x46F`) steruje przekaźnikiem, czyli siłownikiem zamka. Potwierdzenie zamknięcia = SW4 (sprzężenie przekaźnika), nadawane w ramkach stanu zamka tak, jak nadaje je auto (Arkana: `0x418` i `0x46B` naraz, sekcja 3.3). `AWARIA_ZAMKA#1` działa dla każdego auta. | T5, T8 |
| D8 | PIN / pilot | Na podstawie analizy (sekcja 3.3): `PILOT#OTWORZ\|ZAMKNIJ` nadaje ramkę pilota auta z PIN-em v-box i przełącza przekaźnik, jak prawdziwy pilot. Bez automatycznego wysyłania. PIN v-box: domyślny z profilu (Arkana `0x1234`), zmiana w RAM `SIM_PIN#<hex>`. Komenda LX z innym PIN-em jest ignorowana jak w aucie, log `WARN` z odebranym PIN-em. | T5, T7, T8 |
| D9 | Magistrala i OTA | Symulator tylko na CAN1, konfiguracja magistrali z profilu auta. OTA na CAN1 przy prędkości aktywnego auta (`can_parser_attach_boot_ota()` po każdym wyborze, narzędzie OTA musi użyć tej prędkości). Na CAN2 nie ma OTA (jak dziś, CAN2 zostaje z profilem `EXAMPLE`). | T5, T6 |
| D10 | Okresy ramek | 100 ms dla wszystkich ramek auta (rzeczywiste okresy nieznane; LX uznaje CAN za nieaktywny po 1 s ciszy). Fazy rozłożone automatycznie (kolejka TX CAN ma 16 miejsc). | T2, T5 |
| D11 | Profile jako dane | Ramki i sygnały w tablicach `static const` (jak DBC) + opcjonalne hooki `encode` / `on_rx` na wyjątki. Profile w czystym C (bez FreeRTOS, HAL, `io_manager`): akcje zwracane jako kody, wykonuje je silnik. | T2, T4, T8 |
| D12 | Źródła parserów LX | `app/temp_lx_parsers/` w `.gitignore` (pierwszy commit, przed implementacją). W dokumentacji symulatora tylko ID i maski bitów. | commit 0 |
| D13 | Spójność stanu auta | Wynika z zasady z sekcji 1: silnik pracuje tylko przy włączonej stacyjce, więc bez SW0 ramki auta nadają obroty 0 (prawdziwe auto nie nada obrotów przy wyłączonej stacyjce). `V_BOX` bez zmian: nadaje surowe wejścia jak dziś. | T2, T9 |

---

## 3. Analiza parsera LX 196 (Renault Arkana)

Źródło: `196_CanA_RenaultArkana_carshare.c`, CAN1 500 kbps (`BTR_500K`), ID 11-bit, `CAN_TYPE_NORMAL`.
Każda odebrana ramka z listy odświeża `CanActive` (timeout 1 s → `CS_CAN_INACTIVE`).

### 3.1. Auto → LX (symulator nadaje cyklicznie)

| ID | Bajty / maska | Znaczenie w LX | Sygnał v-box | Etap |
|---|---|---|---|---|
| `0x46C` | `[6] & 0x80` | stacyjka (`virtualACC`, `CS_IGNITION_ON`) | `acc` (SW0) | 1 |
| `0x46C` | `[6] & 0x10` | przycisk Start/Stop (awaryjne zamknięcie po 50 ramkach) | `ss_button` | 2 |
| `0x0A1` | `[5..6]` BE × 0.125 | obroty (`CS_ENGINE_RUNNING` = stacyjka i obroty > 0) | `rpm` (SW8) | 1 |
| `0x41C` | `[0..1]` BE × 0.00625, `[0] = 0xFF` = brak | prędkość km/h | `speed_kmh` | 1 |
| `0x46A` | `[1] & 0x04` | drzwi kierowcy (`CS_DOOR_MAIN`, `virtualD6`) | `door_drv` (SW2) | 1 |
| `0x46A` | `[1] & 0x01` (lub `[1]&0x40`, `[2]&0x10`, `[2]&0x01`) | pozostałe drzwi (`CS_DOOR_OTHER`) | `door_other` (SW1) | 1 |
| `0x46A` | `[3] & 0x40` | światła drogowe | `light_high` | 1 |
| `0x5BB` | `[4] & 0x10` (mijania); `[4] & 0x08` (pozycyjne) zawsze 0, bo oba bity naraz LX traktuje jako „oba wyłączone” | mijania (`CS_NOR_LIGHTS_ON`) | `lights` (SW3, D6) | 1 |
| `0x418` | `[2] & 0x04` | zamek 1 (`lock1`) | `locked` (SW4, sekcja 3.3) | 1 |
| `0x46B` | `[2] & 0x01`, `[2] & 0x02`, `[7] & 0x80` naraz | zamek 2 (`lock2`) | `locked` (SW4, sekcja 3.3) | 1 |
| `0x47F` | `[4] & 0x02` | hamulec ręczny (`CS_HANDBRAKE`) | `handbrake` (SW7) | 1 |
| `0x47F` | `[3]` → `CanData_SetFuelLevel()` | poziom paliwa (jednostka do weryfikacji w T12, przyjęte %) | `fuel_pct` (potencjometr) | 1 |
| `0x47C` | `[4] & 0x40` | maska | `hood` (SW6) | 1 |
| `0x206` | `[6..7]` BE > 5 | pedał hamulca; `[4] & 0x04` = 0 → zdarzenie ESP (domyślnie ustawić bit) | `brake_pedal` | 1 |
| `0x438` | `[0..3]` 28 bitów (bez dolnego półbajtu `[3]`) × 10 m, `FF FF FF F0` = brak | przebieg | `odometer_m` | 1 |
| `0x417` | `[0]` − 40 | temperatura silnika (> 105 = kontrolka) | `engine_temp_c` | 1 |
| `0x3AA` | `[2]&0x10`, `[3]&0x40`, `[2]&0x02` | check engine, olej, ładowanie | stałe 0 | 1 |
| `0x538`, `0x0B6`, `0x4F5`, `0x3BD`, `0x4C2`, `0x4E4`, `0x404`, `0x47D`, `0x497` | | poduszka, przepustnica, obciążenie, TC, LPG, paliwo, światła 2, ABS (część handlerów w LX nic nie robi) | – | 2 |

SW5 (awaryjne): parser 196 ich nie czyta, dla Arkany bez efektu.

### 3.2. LX → auto (symulator reaguje)

| ID | Dane | Znaczenie | Reakcja symulatora | Etap |
|---|---|---|---|---|
| `0x46F` | `32 PIN_H PIN_L 18 04 84 07 30` | carsharing `cmd_open` | PIN zgodny → przekaźnik otwarty; niezgodny → brak reakcji + `WARN` (D8) | 1 |
| `0x46F` | `32 PIN_H PIN_L 10 04 84 07 30` | carsharing `cmd_close` / zamknięcie awaryjne | PIN zgodny → przekaźnik zamknięty; niezgodny → jak wyżej | 1 |
| `0x46F` | `11 11 11 00 ...` co ok. 100 ms przez 5 s | budzenie magistrali | log (etap 2: wybudzenie) | 2 |
| `0x743` | UDS `10 03`, `3E 00`, `22 02 07`, `10 01` | odczyt przebiegu | odpowiedzi `0x763` (`06 50 03`, `02 7E`, `07 62 02 07 km32`) | 2 |
| `0x46C` | `03 12 02` | blokada silnika (przy obrotach > 200) | obroty → 0 | 2 |
| `0x418` | `00 00 00 50 ...` | klakson (awaryjne zamknięcie) | log | 2 |
| `0x7E0`, `0x5xx` | CAN locker | blokada Lincor, nie auto | – | – |

Bez odpowiedzi UDS LX trzy razy po 10 s loguje `OBDResponseTimeout`, potem bierze przebieg z `0x438`.

### 3.3. Zamek, carsharing i PIN (zweryfikowane w kodzie parsera)

**Weryfikacja ustaleń (2026-10-27):**

| Ustalenie | Miejsce w parserze 196 | Wynik |
|---|---|---|
| LX zamyka `0x46F 32 PIN_H PIN_L 10 04 84 07 30`, otwiera z `[3] = 18` | `CarShareProcess`, stany `cmd_open_close_*`; to samo `0x10` w `EmergencyClose` | potwierdzone |
| Nauka PIN-u z `0x46F`, gdy `[3] == 0x18`, PIN = `[1..2]`, zapis raz | `CanA_Arkana_OpenClosePilot`: tylko gdy `PinCodeStatus != 1`, potem `StartConfigUpdate()` | potwierdzone; pozostałe bajty nie są sprawdzane |
| Zamknięte = `0x418 [2] & 0x04` i `0x46B [2] & 0x01`, `[2] & 0x02`, `[7] & 0x80` naraz | `lock1 && lock2` → `CAN_BusDoors \|= 0x40` → `CS_DOORLOCK` | potwierdzone; brak którejkolwiek ramki albo bitu = „otwarte” |
| Drzwi `0x46A`: kierowcy `[1] & 0x04`, pozostałe `[1] & 0x01`, `[1] & 0x40`, `[2] & 0x01`, `[2] & 0x10` | `CanA_Arkana_LPG_DOORS` → `CS_DOOR_MAIN`, `CS_DOOR_OTHER` | potwierdzone |

**Przebieg komendy w LX:**
1. Sprawdzenie warunków wstępnych (tabela niżej). Niespełnione → NACK z kodem błędu, bez ramki na CAN.
2. Magistrala nieaktywna (`CS_CAN_INACTIVE`) → LX budzi ją ramkami `0x46F 11 11 11 00 00 00 00 00` przez ok. 5 s,
   potem NACK `ER_WAKEUP_CAN`.
3. Brak PIN-u (`PinCodeStatus != 1`) → NACK `ER_OTHER_ON_SEAT`, zawsze jako odpowiedź na otwórz, także dla zamknij.
4. Nadanie `0x46F`, czekanie 500 ms (zamknij: 200 + 500 ms), sprawdzenie `CS_DOORLOCK`.
5. Stan się nie zgadza → ponowienie, razem 6 prób; potem NACK `ER_UNKNOWN`. Twardy limit 10 s.

| Komenda | Warunek w LX | Źródło | Ustawienie v-box |
|---|---|---|---|
| otwórz | zamek zamknięty, inaczej NACK `ER_ISCLOSE_OR_ISOPEN` | `0x418` + `0x46B` | SW4 = 1 |
| zamknij | zamek otwarty, inaczej NACK `ER_ISCLOSE_OR_ISOPEN` | `0x418` + `0x46B` | SW4 = 0 |
| zamknij | stacyjka wyłączona, silnik nie pracuje | `0x46C`, `0x0A1` | SW0 = 0 |
| zamknij | ręczny zaciągnięty | `0x47F` | SW7 = 1 |
| zamknij | wszystkie drzwi zamknięte | `0x46A` | SW1 = SW2 = 0 |
| zamknij | pozycyjne, mijania i drogowe wyłączone | `0x5BB`, `0x46A` | SW3 = 0, `DROGOWE#0` |
| zamknij | kluczyk (iButton `DriverID`) na tacce, GPS ≥ 3 satelity (jeśli sprawdzany), GSM ≥ 2 | wejścia LX | poza v-box |

Warunki dla „zamknij” nie są sprawdzane w trybie testowym carsharingu LX (`sCarShare->Test`).

**Budżet czasu v-box:** odbiór `0x46F` → `io_manager_setRelayStateAsync()` → impuls 40 ms → SW4 po debouncingu
(4 × 10 ms) → najbliższe ramki `0x418` / `0x46B` (≤ 100 ms). Razem ok. 200 ms, czyli mniej niż 500 ms, po
których LX sprawdza zamek. Ponowienia z LX są nieszkodliwe: przekaźnik już w stanie docelowym nie dostaje
impulsu (`io_manager_setRelayState()`).

**PIN (D8):**
- LX uczy się PIN-u raz i zapisuje go w konfiguracji. PIN przeżywa reset i zmianę parsera. Ponowna nauka
  wymaga skasowania `PinCodeStatus` w konfiguracji LX.
- Ramki pilota v-box: `0x46F 32 PIN_H PIN_L 18 04 84 07 30` (otwórz, uczy LX) i `... 10 ...` (zamknij).
- Bez automatycznego wysyłania pilota: LX nauczyłby się PIN-u bez wiedzy testującego, a scenariusz „brak PIN-u”
  (NACK `ER_OTHER_ON_SEAT`) ma zostać testowalny. v-box nie wie też, czy LX zna już jakiś PIN.
- Ścisła kontrola PIN-u w komendach LX: prawdziwe auto ignoruje zły PIN, a kontrola wykrywa błędy parsera
  (kolejność bajtów, stary PIN). Log `WARN` podaje odebrany PIN, a `SIM_PIN#<hex>` dopasowuje v-box do PIN-u,
  którego LX nauczył się wcześniej (np. z prawdziwego auta).

---

## 4. Architektura

```text
 przyciski SW0..SW8, potencjometr ──► io_manager (debounce, przekaźnik) ──┐
 UART: ZRODLO#UART, OBROTY#, PREDKOSC#, ... ──► io_sim ───────────────────┤
                                                                          ▼
                                                     car_sim_task (co 10 ms, vTaskDelayUntil)
                                                     ├─ vehicle_state_build()  → vehicle_state_t
                                                     ├─ harmonogram ramek TX aktywnego profilu
                                                     │     └─ car_sim_codec: sygnały → bajty (+ hook encode)
                                                     │           └─► can_manager_send(&can1_inst)
                                                     └─ żądania: wybór symulatora, PILOT#
 CAN1 RX ──► can_rx_task ──► cp_car_sim (filtry z tablicy rx profilu) ──► car_sim_on_rx()
                                    └─ dopasowanie komendy + kontrola PIN-u → akcja (OTWÓRZ / ZAMKNIJ / ...)
                                          └─► io_manager_setRelayStateAsync() ──► SW4 → vehicle_state.locked
 RS485 superStatus ──► on_dev_parser_change() (main.c) ──► car_sim_request_dev_parser(196)
                                                                 └─► (task) mapa: 196 → ARKANA, nieznany → BRAK
                                                                       ├─ can_parser_select(CAN1, CAR_SIM)
                                                                       └─ can_parser_attach_boot_ota(CAN1)
```

| Warstwa | Pliki | Zależności |
|---|---|---|
| Typy | `app/car_sim/car_sim_types.h` | `can_msg_types.h`, `can_port.h` (bez HAL, bez FreeRTOS) |
| Koder sygnałów | `app/car_sim/car_sim_codec.c/.h` | typy (czysty C) |
| Rejestr i mapa | `app/car_sim/car_sim_registry.c/.h` | profile (czysty C) |
| Profile aut | `app/car_sim/sims/sim_v_box.c/.h`, `sim_none.c/.h`, `sim_renault_arkana.c/.h` | typy (czysty C) |
| Silnik / task | `app/car_sim/car_sim.c/.h` | FreeRTOS, `can_manager`, `can_parser`, `io_manager`, `io_sim` |
| Komendy UART | `app/car_sim/car_sim_cmd.c/.h` | silnik (wywoływane z mostu UART jak `io_cmd`) |
| Profil RX | `app/can_bus/can_parser/parsers/cp_car_sim.c/.h` | silnik (filtry aktywnego auta) |

### 4.1. Model stanu auta

```c
typedef struct {
    bool acc, door_drv, door_other, hood, handbrake, hazard;
    bool lights;          /* SW3: światła wg parsera urządzenia (D6), np. Arkana = mijania */
    bool light_high;      /* UART DROGOWE# */
    bool locked;          /* SW4: sprzężenie przekaźnika zamka (D7) */
    bool brake_pedal, ss_button;
    uint16_t rpm;         /* obr/min; 0 przy wyłączonej stacyjce (D13) */
    uint16_t rpm_in;      /* obroty z wejść bez reguły D13 (dla symulatora V_BOX) */
    uint16_t speed_kmh;
    uint8_t fuel_pct;     /* 0..100 */
    uint32_t odometer_m;  /* całkowany z prędkości w car_sim_task */
    int16_t engine_temp_c;
    uint16_t btn;         /* surowe SW0..SW8 (dla symulatora V_BOX) */
} vehicle_state_t;
```

### 4.2. Opis profilu (tablice jak DBC)

```c
typedef enum { SIM_ENC_BIT, SIM_ENC_UINT_BE, SIM_ENC_UINT_LE } sim_enc_t;

typedef struct {
    sim_sig_t sig;        /* SIG_ACC, SIG_RPM, SIG_LIGHTS, SIG_LOCKED, ... (ten sam sygnał może mieć kilka wpisów) */
    sim_enc_t enc;
    uint8_t byte;         /* pierwszy bajt pola */
    uint8_t mask;         /* BIT: maska bitu */
    uint8_t len;          /* UINT: liczba bajtów 1..4 */
    uint8_t shift;        /* UINT: przesunięcie w lewo w polu (np. przebieg Arkany o 4 bity) */
    uint16_t mul, div;    /* raw = value * mul / div + offset */
    int16_t offset;
} sim_signal_t;

typedef struct {
    uint32_t id; bool ext; uint8_t dlc; uint16_t period_ms;
    uint8_t init[8];                                  /* bajty domyślne (np. ESP 0x206[4] |= 0x04) */
    const sim_signal_t *signals; uint8_t num_signals;
    void (*encode)(const vehicle_state_t *vs, uint8_t data[8]);   /* hook po sygnałach, opcjonalny */
} sim_frame_t;

typedef struct {
    uint32_t id; bool ext;
    uint8_t match[8], match_mask[8];                  /* dopasowanie bajtów komendy (bez bajtów PIN-u) */
    int8_t pin_byte;                                  /* pierwszy bajt PIN-u (BE, 2 bajty), -1 = komenda bez PIN-u */
    sim_action_t action;                              /* SIM_ACT_UNLOCK, SIM_ACT_LOCK, SIM_ACT_LOG, SIM_ACT_CUSTOM */
    sim_action_t (*on_rx)(const CAN_msg_t *msg);      /* dla SIM_ACT_CUSTOM: zwraca akcję do wykonania */
} sim_cmd_t;

typedef struct {
    uint32_t id; bool ext; uint8_t dlc;
    uint8_t unlock[8], lock[8];                       /* ramki pilota auta, PIN wstawiany od pin_byte */
    uint8_t pin_byte;                                 /* Arkana: 1 ([1] = PIN_H, [2] = PIN_L) */
    uint16_t default_pin;                             /* Arkana: 0x1234 */
} sim_fob_t;

typedef struct {
    const char *key, *name;                           /* "ARKANA", "Renault Arkana" */
    const uint16_t *dev_parsers; uint8_t num_dev_parsers;   /* {196}; BRAK: brak wpisów */
    const can_config_t *bus_config;
    const sim_frame_t *tx; uint8_t num_tx;
    const sim_cmd_t *rx; uint8_t num_rx;
    uint16_t rpm_max, rpm_step, idle_rpm;
    const sim_fob_t *fob;                             /* pilot z PIN-em (opcjonalny) */
    void (*on_select)(void);                          /* zerowanie liczników profilu */
} car_sim_profile_t;
```

Hooki są wyjątkiem. Arkana etapu 1 nie potrzebuje żadnego: światła i zamek to kilka wpisów `BIT` tego samego
sygnału, pedał hamulca to pole `UINT` (`mul = 100`), a przebieg pole 28-bitowe z `shift = 4`. Hook ma tylko
`V_BOX` (licznik telemetrii).

### 4.3. Zasady współbieżności

- Wybór symulatora wykonuje wyłącznie `car_sim_task`. Callback RS485 i komendy UART tylko zgłaszają żądanie
  (zmienna + `xTaskNotifyGive`), nie blokują.
- `car_sim_on_rx()` działa w `can_rx_task`: tylko dopasowanie, kontrola PIN-u i nieblokujące akcje
  (`io_manager_setRelayStateAsync()`, `safe_printf`), zgodnie z sekcją 14 `gemini.md`.
- Wskaźnik aktywnego profilu jest `volatile` i zmieniany przed `can_parser_select()`. Profile są `static const`,
  więc stary wskaźnik zawsze wskazuje poprawne dane.
- PIN v-box to `volatile uint16_t` zapisywany z kontekstu komend UART, czytany w `can_rx_task` (zapis 16-bitowy
  jest atomowy na Cortex-M).

---

## 5. Zadania

### T1 – Model stanu auta i nowe sygnały w `io_sim` / `io_cmd`

**Pliki:** `app/io/io_sim.c/.h`, `app/io/io_cmd.c/.h`, `tools/host/io_cmd_cli.c`, `tools/test_io_cmd_host.py`

1. Limity obrotów ustawiane z zewnątrz (D5): `io_sim_setRpmLimits(max, step, engine_on_rpm)`, domyślnie
   wartości `V_BOX` (750, 3, 750). `io_sim_setRpm()` i SW8 w trybie IO korzystają z bieżących limitów.
   Komunikaty błędów `OBROTY#` bez zmian dla `V_BOX`.
2. Nowe sygnały w `io_sim_values_t` (tylko z UART, w trybie IO wartości domyślne):
   prędkość 0..250 km/h, przebieg (km, domyślnie 10000), drogowe 0/1, pedał hamulca 0/1,
   temperatura silnika −40..150 °C (domyślnie 90), Start/Stop 0/1 (etap 2).
3. Komendy w `io_cmd.c` (tylko `ZRODLO=UART`, odpowiedzi `[IO] OK` / `[IO] ERR:` jak dotąd):
   `PREDKOSC#<0..250>`, `PRZEBIEG#<km>`, `DROGOWE#<0|1>`, `HAMULEC_NOZNY#<0|1>`, `TEMP_SILNIKA#<-40..150>`.
   `ZRODLO?`, `IO_HELP` i `IO_RESET` uwzględniają nowe sygnały. `SWIATLA#` (SW3) bez zmian; znaczenie świateł
   zależy od symulatora (D6).
4. Rozszerzenie testu hostowego `test_io_cmd_host.py`: nowe komendy, limity obrotów `V_BOX` i auta.

**Kryteria akceptacji:** wszystkie dotychczasowe kroki testu `io_cmd` zgodne bez zmian, nowe kroki zgodne,
build bez ostrzeżeń.

---

### T2 – Typy i koder sygnałów (`car_sim_types.h`, `car_sim_codec.c`)

**Pliki:** `app/car_sim/car_sim_types.h`, `app/car_sim/car_sim_codec.c/.h` (nowe)

1. Typy z sekcji 4.1–4.2, enum sygnałów `sim_sig_t`, enum akcji `sim_action_t`.
2. `vehicle_state_build(const io_sim_values_t *eff, uint16_t idle_rpm, uint32_t odometer_m, vehicle_state_t *out)`:
   mapowanie SW0..SW8 (SW3 → `lights`, SW4 → `locked`, SW8 → obroty), paliwo, sygnały z UART.
   Reguła D13: `rpm = acc ? rpm_in : 0`.
3. `car_sim_encode_frame(const sim_frame_t *f, const vehicle_state_t *vs, CAN_msg_t *out)`:
   bajty `init`, kodowanie sygnałów (BIT / UINT BE / UINT LE, `mul/div/offset/shift`, nasycenie do zakresu pola),
   hook `encode`, `CAN_msg_set_standard_id()` / `CAN_msg_set_extended_id()` (sekcja 13 `gemini.md`).
4. `car_sim_match_cmd(const sim_cmd_t *table, n, const CAN_msg_t *msg, uint16_t pin, sim_match_t *out)`:
   dopasowanie ID i bajtów z maską, potem PIN (`pin_byte`, BE). Wynik: akcja, odebrany PIN i flaga
   `pin_mismatch` (akcja nie jest wtedy wykonywana).
5. `car_sim_build_fob(const sim_fob_t *fob, bool unlock, uint16_t pin, CAN_msg_t *out)`: ramka pilota z PIN-em.
6. Harmonogram jako czysta funkcja: `car_sim_schedule_init()` (fazy rozłożone co 10 ms w obrębie okresu) i
   `car_sim_schedule_due(tick_ms)` → lista ramek do nadania w danym ticku.

**Kryteria akceptacji:** test hostowy T9 (koder, dopasowanie z PIN-em, pilot, harmonogram) zgodny, kod bez
FreeRTOS / HAL.

---

### T3 – Rejestr symulatorów i mapa parserów urządzenia

**Pliki:** `app/car_sim/car_sim_registry.c/.h`, `app/car_sim/sims/sim_none.c/.h` (nowe)

1. `car_sim_id_t` (`CAR_SIM_ID_V_BOX = 0`, `CAR_SIM_ID_NONE`, `CAR_SIM_ID_RENAULT_ARKANA`, `CAR_SIM_COUNT`)
   i tablica getterów `s_sims[]` (wzorzec `s_parsers[]` z `can_parser.c`, zero `extern`).
2. Symulator **`BRAK`** (`sim_none.c`, D4): jedna ramka TX `0x01F2F3FF` ext, DLC 6, `CA FE 00 BA BE 00`,
   co 1000 ms; bez komend RX, bez pilota, bez numerów parsera urządzenia; CAN1 Classic 500 kbps;
   limity obrotów jak `V_BOX`.
3. `car_sim_find_by_dev_parser(uint16_t no)` → symulator albo `CAR_SIM_ID_NONE` (także dla 0),
   `car_sim_find_by_key(const char *key)` (bez rozróżniania wielkości liter), `car_sim_get(id)`.
4. Kontrola spójności przy starcie: ten sam numer parsera urządzenia w dwóch profilach → log `ERROR`
   (test hostowy sprawdza to statycznie).

**Kryteria akceptacji:** test hostowy: 192 → `V_BOX`, 196 → `ARKANA`, 197 i 0 → `BRAK`, brak duplikatów.

---

### T4 – Symulator `V_BOX` (dzisiejsza telemetria)

**Pliki:** `app/car_sim/sims/sim_v_box.c/.h` (nowe), `app/io/io_manager.c`

1. Ramka `0x01F2F3F4` (ext, 100 ms, 8 B): `data[0]` = SW0..SW7, `data[1]` = paliwo, `data[2]` =
   `io_sim_rpmToByte(rpm_in)`, `data[3]` = 0, `data[4..7]` = licznik BE. Logika przeniesiona 1:1 z
   `io_manager_canCreateFrame()` do hooka `encode`, licznik zerowany w `on_select`.
2. Komenda `0x01F2F3F0` (ext, bez PIN-u): `data[2] & 0x01` → `SIM_ACT_LOCK` / `SIM_ACT_UNLOCK`
   (logika z `cp_v_box.c:on_v_box_relay_cmd_rx`, długość < 3 B ignorowana).
3. `.dev_parsers = {192}` (D1), magistrala Classic 500 kbps, `rpm_max = 750`, `rpm_step = 3`, `idle_rpm = 750`,
   bez pilota.
4. `io_manager_task`: usunięcie nadawania ramki CAN i `io_manager_canCreateFrame()`. Log `IO_MGR` bez zmian.
5. `cp_v_box` zostaje w rejestrze `can_parser` bez zmian (nieużywany na CAN1); usunięcie jako osobny refactor
   po akceptacji.

**Kryteria akceptacji:** test hostowy: dla scenariusza z `test_io_cmd_host.py` bajty `data[0..2]` ramki
`V_BOX` identyczne z dzisiejszymi (`B9 2A FA` itd.), licznik rośnie o 1 na ramkę. Na sprzęcie LX z parserem
192 działa jak przed zmianą.

---

### T5 – Silnik `car_sim` (task, RX, wybór symulatora)

**Pliki:** `app/car_sim/car_sim.c/.h` (nowe), `app/can_bus/can_parser/parsers/cp_car_sim.c/.h` (nowe),
`app/can_bus/can_parser/can_parser.c/.h`, `app/tasks_config.h`

1. `car_sim_init(can_instance_t *can1)`, task `car_sim` (priorytet `tskIDLE_PRIORITY + 2`, stos 512 słów,
   `vTaskDelayUntil` co 10 ms):
   - stan wejść: `io_manager_getPhysInputs()` + `io_sim_resolve()`, budowa `vehicle_state_t`,
   - całkowanie przebiegu z prędkości,
   - nadawanie ramek z harmonogramu, licznik ramek odrzuconych przez pełną kolejkę (log co 5 s, nie co ramkę),
   - obsługa żądań: wybór symulatora, `PILOT#`.
2. `car_sim_select(car_sim_id_t id)` (tylko z tasku): ustawienie aktywnego profilu, `io_sim_setRpmLimits()`,
   `on_select()`, reset harmonogramu, `can_parser_select(can1, CAN_PARSER_ID_CAR_SIM)`,
   `can_parser_attach_boot_ota(can1)` (OTA przy prędkości auta, D9),
   log `[CAR_SIM] symulator: Renault Arkana (parser urządzenia 196, CAN1 500k)`.
   Błąd konfiguracji magistrali → powrót do poprzedniego symulatora.
3. Wybór automatyczny (`car_sim_request_dev_parser(no)`, wykonywany w tasku, D2–D4):
   - tryb `AUTO`, numer w mapie → wybór od razu (gdy inny niż aktywny),
   - tryb `AUTO`, numer bez symulatora (także 0) → `BRAK` i log
     `[CAR_SIM] WARN: brak symulatora dla parsera urządzenia 197, nadaję ramkę CA FE 00 BA BE 00`,
   - tryb `RECZNY` → tylko zapamiętanie numeru i log `[CAR_SIM] tryb RECZNY: parser urządzenia 197 zapamiętany`,
   - utrata łącza RS485 → bez zmiany symulatora.
4. `cp_car_sim`: profil `can_parser` z `bus_config` aktywnego auta i filtrami zbudowanymi z unikalnych ID
   tablicy `rx` (jeden callback `car_sim_on_rx()`), `CAN_PARSER_ID_CAR_SIM` w enumie i w `s_parsers[]`.
   Kontrola limitu `CAN_MAX_FILTERS` razem z filtrami OTA. `BRAK` ma tylko filtry OTA.
5. `car_sim_on_rx()` (kontekst `can_rx_task`): `car_sim_match_cmd()` z bieżącym PIN-em v-box →
   `UNLOCK` / `LOCK` → `io_manager_setRelayStateAsync()` (`AWARIA_ZAMKA#1` blokuje jak dziś),
   `LOG` → `safe_printf`, `CUSTOM` → `on_rx()` profilu. Niezgodny PIN → brak akcji i
   `[CAR_SIM] WARN: 0x46F PIN F15C != 1234, komenda zignorowana (SIM_PIN#F15C)`.
6. PIN v-box (D8): `car_sim_set_pin(pin)` / `car_sim_reset_pin()`. Nadpisanie obowiązuje dla każdego profilu
   z pilotem do resetu albo `SIM_PIN#DOMYSLNY`, bez niego obowiązuje `default_pin` aktywnego profilu.
7. API: `car_sim_request_select(id)`, `car_sim_request_dev_parser(no)`, `car_sim_request_fob(unlock)`,
   `car_sim_set_mode(AUTO|MANUAL)`, `car_sim_get_info()` (aktywny symulator, tryb, ostatni numer parsera,
   PIN, liczniki).

**Kryteria akceptacji:** przełączanie `V_BOX` ↔ Arkana ↔ `BRAK` z UART 100 razy bez utraty OTA (discovery na
CAN1 odpowiada po każdym przełączeniu), brak przepełnień kolejki TX, wolna sterta ≥ 2 KB.

---

### T6 – Integracja i automatyczny wybór z RS485

**Pliki:** `main.c`, `CMakeLists.txt`, `app/rs485/rs485_link.h` (bez zmian API)

1. `main.c`: zamiast `can_parser_select(&can1_inst, CAN_PARSER_ID_V_BOX)` → `car_sim_init(&can1_inst)`
   (start z `V_BOX`, tryb `AUTO`, D3), potem `can_parser_attach_boot_ota()` jak dotąd.
   CAN2 bez zmian: profil `EXAMPLE`, bez OTA (D9).
2. Callback `on_dev_parser_change()`: dla CAN1 woła `car_sim_request_dev_parser(new_no)` (logika D2–D4
   w `car_sim`, nie w `main.c`). Log zmiany parsera urządzenia zostaje. Numery z CAN2 tylko w logu.
3. `CMakeLists.txt`: źródła `app/car_sim/**` (moduł obowiązkowy, bez opcji CMake), katalogi include.
4. Build `CAN_FD_TEST` (profil `CP_FD_TEST` na CAN1): `car_sim` nie przejmuje CAN1 (flaga w `car_sim_init`).

**Kryteria akceptacji:** build produkcyjny, `CAN_FD_TEST` i `RS485_LINK=OFF` bez ostrzeżeń; po zmianie
parsera w konfiguracji LX na 196 v-box w ≤ 5 s przełącza się na Arkanę, po powrocie do 192 na `V_BOX`,
po zmianie na numer bez symulatora na `BRAK`.

---

### T7 – Komendy UART symulatora

**Pliki:** `app/car_sim/car_sim_cmd.c/.h` (nowe), `app/can_bus/can_uart_bridge/can_uart_bridge.c`

| Komenda | Działanie |
|---|---|
| `SIM?` | aktywny symulator, tryb `AUTO`/`RECZNY`, ostatni parser urządzenia, PIN (domyślny / nadpisany), liczba ramek TX, liczniki (nadane, odrzucone, komendy LX, odrzucone przez PIN) |
| `SIM_LIST` | tabela ASCII: klucz, nazwa, parsery urządzenia, magistrala, liczba ramek, pilot |
| `SIM#<KLUCZ\|numer>` | wybór ręczny (`SIM#ARKANA`, `SIM#196`, `SIM#BRAK`), tryb `RECZNY`; numer bez symulatora → `[SIM] ERR: brak symulatora dla parsera urządzenia 197` |
| `SIM#AUTO` | powrót do automatu, natychmiastowe zastosowanie ostatniego numeru parsera urządzenia |
| `SIM_PIN#<hex4>` / `SIM_PIN#DOMYSLNY` | nadpisanie PIN-u v-box w RAM / powrót do PIN-u z profilu (D8) |
| `PILOT#<OTWORZ\|ZAMKNIJ>` | ramka pilota z PIN-em (jeśli profil ma pilota) + przekaźnik; profil bez pilota (`V_BOX`, `BRAK`) → tylko przekaźnik |
| `SIM_HELP` | lista komend |

Routing w moście jak `io_cmd_process()`, odpowiedzi z tagiem `[SIM]`, `HELP` mostu wymienia `SIM_HELP`.

**Kryteria akceptacji:** test hostowy parsera komend (składnia, wielkość liter, błędy), ręcznie z Serial Commandera.

---

### T8 – Symulator Renault Arkana (parser urządzenia 196)

**Pliki:** `app/car_sim/sims/sim_renault_arkana.c/.h` (nowe), rejestr (T3)

1. Ramki etapu 1 z sekcji 3.1 (13 ramek, 100 ms, ID 11-bit, DLC 8, bajty nieznane = 0): `0x46C`, `0x0A1`,
   `0x41C`, `0x46A`, `0x5BB`, `0x418`, `0x46B`, `0x47F`, `0x47C`, `0x206`, `0x438`, `0x417`, `0x3AA`.
2. Sygnały bez hooków (sekcja 4.2):
   - `lights` → `0x5BB [4] 0x10` (D6),
   - `locked` → `0x418 [2] 0x04` oraz `0x46B [2] 0x01`, `[2] 0x02`, `[7] 0x80` (D7, sekcja 3.3),
   - `brake_pedal` → `0x206 [6..7]` = 100, `init[4] = 0x04` (bez zdarzenia ESP),
   - `odometer_m` → `0x438 [0..3]`, `div = 10`, `shift = 4` (nigdy `FF FF FF F0`),
   - `speed_kmh` → `0x41C [0..1]`, `mul = 160`; `engine_temp_c` → `0x417 [0]`, `offset = 40`.
3. Komendy RX: `0x46F 32 __ __ 18 04 84 07 30` → `UNLOCK`, `0x46F 32 __ __ 10 04 84 07 30` → `LOCK`,
   `pin_byte = 1`.
4. Pilot: `0x46F`, `unlock = 32 PH PL 18 04 84 07 30`, `lock = 32 PH PL 10 04 84 07 30`, `pin_byte = 1`,
   `default_pin = 0x1234`.
5. `.dev_parsers = {196}`, Classic 500 kbps, `rpm_max = 8000`, `rpm_step = 1`, `idle_rpm = 800`.

**Kryteria akceptacji:** test hostowy z dekoderem referencyjnym (T9): każdy sygnał stanu auta odczytany tak jak
w parserze 196. Na sprzęcie (T12) LX pokazuje stacyjkę, obroty, drzwi, światła, ręczny, maskę, zamek, uczy się
PIN-u z `PILOT#OTWORZ` i wykonuje carsharingowe otwórz / zamknij.

---

### T9 – Testy hostowe

**Pliki:** `tools/host/car_sim_cli.c`, `tools/test_car_sim_host.py` (nowe)

1. Harness (jak `rs485_cli.c`): ustawienie stanu auta, wybór profilu, `ENCODE` wszystkich ramek,
   `RX <id> <bajty> [pin]` → akcja, `FOB <OTWORZ|ZAMKNIJ> <pin>` → ramka, `SCHED <ms>` → ramki do nadania,
   `MAP <numer>` → symulator.
2. **Dekodery referencyjne w Pythonie** przepisane z parsera LX 196 (te same maski i wzory co w sekcji 3.1),
   np. `rpm == ((d[5] << 8) | d[6]) * 0.125`, `locked == lock1 and lock2`. Test: dla serii stanów (każdy
   sygnał osobno + kombinacje) dekoder odtwarza stan auta; `0x5BB` nigdy nie ma obu bitów świateł naraz.
   To samo dla `V_BOX` (bajty identyczne z dzisiejszymi, T4). Reguła D13: SW0 = 0 i SW8 = 1 → Arkana
   `0x0A1` = 0 obr/min, `V_BOX` `data[2]` bez zmian.
3. Komendy LX: otwórz / zamknij Arkany z PIN-em `0x1234` → `UNLOCK` / `LOCK`; z PIN-em `0xF15C` → brak akcji,
   `pin_mismatch`, odebrany PIN `F15C`; po nadpisaniu PIN-u na `F15C` → akcja; budzenie i obce ramki → brak akcji.
4. Pilot: `FOB OTWORZ 1234` → `0x46F 32 12 34 18 04 84 07 30`, a dekoder nauki PIN-u z LX odczytuje `0x1234`.
5. `BRAK`: jedyna ramka to `0x01F2F3FF` ext, DLC 6, `CA FE 00 BA BE 00`.
6. Harmonogram: przez 1 s każda ramka 100 ms nadana 10 razy, w żadnym ticku nie więcej niż `ceil(n / 10)` ramek.
7. Rejestr: brak duplikatów numerów parserów urządzenia, mapa 192 / 196 / 197 / 0.

**Kryteria akceptacji:** wszystkie kroki zgodne w Dockerze `vbox-builder` (lokalnie brak gcc).

---

### T10 – Generator szkieletu symulatora

**Pliki:** `tools/create_car_sim.py` (nowe)

`python tools/create_car_sim.py <KLUCZ> <plik_parsera_LX.c> [--name "Renault Arkana"] [--dev-parser 196]`

1. Z tablicy `ThisCar` parsera LX wyciąga prędkość (`BTR_500K` → `CAN_BAUDRATE_500K`), listę
   `{CAN_x, ..., ID, ..., handler}` i odrzuca wpisy niebędące ramkami auta (`LOCKER_*`, odpowiedzi diagnostyczne
   oznacza jako kandydatów na etap 2).
2. Tworzy `sim_<klucz>.c/.h` z ramkami TX (100 ms, puste listy sygnałów) i **ciałem handlera LX wklejonym jako
   komentarz** nad każdą ramką, a wyrażenia `MsgObj->Msg[N] & 0xMM` zamienia w podpowiedzi
   `/* TODO: { SIG_?, SIM_ENC_BIT, .byte = N, .mask = 0xMM } */`.
3. Z wywołań `CAN_TransmitACK(..., 0xID, Data, ...)` w parserze LX tworzy listę kandydatów na komendy RX,
   a z odwołań do `PinCode` podpowiedź pilota (`pin_byte`).
4. Rejestruje profil: enum `car_sim_id_t`, `s_sims[]`, `CMakeLists.txt` (jak `tools/create_parser.py`).

**Kryteria akceptacji:** dla parsera 196 (27 wpisów `ThisCar`) powstaje kompilujący się szkielet z 22 ramkami
cyklicznymi i komentarzami, ramką pilota `0x46F`, kandydatami diagnostycznymi `0x763` / `0x765` i pominiętymi
wpisami CAN locker; dopisanie sygnałów z sekcji 3.1 daje profil z T8.

---

### T11 – Dokumentacja

**Pliki:** `docs/car_sim/car_sim.md` (nowy: działanie, komendy, mapa parserów, `BRAK`, PIN i pilot, test),
`docs/car_sim/adding_a_car.md` (nowy: jak dodać auto z parsera LX, generator, testy, zamek i PIN),
`docs/project_documentation.md` (task, przepływ CAN, sekcja symulatorów), `docs/io/io_uart_simulation.md`
(nowe sygnały, SW3 zależne od symulatora), `docs/can_bus/can_uart_bridge.md` (komendy `SIM`), `README.md`,
`docs/changelog.md`, `gemini.md` (nowa sekcja: wzorzec profilu symulatora, zakaz FreeRTOS / HAL w profilach).

---

### T12 – Test na sprzęcie

| # | Krok | Oczekiwany wynik |
|---|---|---|
| 1 | LX z parserem 192, v-box po resecie | `SIM?`: `V_BOX`, `AUTO`; LX jak przed zmianą (telemetria, przekaźnik z `0x01F2F3F0`) |
| 2 | Zmiana konfiguracji LX na 196 (online lub RS485), potem wgranie `196.CAN` | od razu po zmianie konfiguracji: `[RS485] ZMIANA parsera urządzenia CAN1: 192 -> 196`, `[CAR_SIM] symulator: Renault Arkana` |
| 3 | LX bez PIN-u, carsharing otwórz (jeśli LX zna już PIN, pominąć i ustawić `SIM_PIN#<PIN z LX>`) | LX: NACK `ER_OTHER_ON_SEAT`, brak `0x46F` na CAN |
| 4 | `PILOT#OTWORZ` | LX zapisuje PIN `0x1234` (log konfiguracji), przekaźnik otwarty |
| 5 | Przełączniki SW0, SW1, SW2, SW3, SW6, SW7, SW8, potencjometr 0 / 50 / 100 % | platforma LX pokazuje stacyjkę, drzwi, mijania, maskę, ręczny, obroty (tylko przy SW0), paliwo; ustalić jednostkę `0x47F [3]` i poprawić profil, jeśli to nie % |
| 6 | `ZRODLO#UART`, `PREDKOSC#50`, `PRZEBIEG#12345`, `DROGOWE#1`, potem `ZRODLO#IO` | prędkość, przebieg, drogowe w LX |
| 7 | Warunki zamknięcia (sekcja 3.3: SW0 = 0, SW1 = SW2 = SW3 = 0, SW7 = 1, iButton w LX), carsharing zamknij, potem otwórz | v-box loguje `0x46F` z PIN-em `1234`, przekaźnik zmienia stan, SW4 śledzi go, LX: ACK w ≤ 1 s |
| 8 | `ZRODLO#UART`, warunki zamknięcia komendami (`STACYJKA#0`, `HAMULEC#1`, ...), `AWARIA_ZAMKA#1`, carsharing zamknij | przekaźnik nie reaguje na 6 prób, LX: NACK `ER_UNKNOWN` |
| 9 | `SIM_PIN#ABCD`, carsharing zamknij; potem `SIM_PIN#DOMYSLNY` | `WARN: 0x46F PIN 1234 != ABCD`, LX: NACK `ER_UNKNOWN`; po powrocie działa |
| 10 | Zmiana konfiguracji LX na numer bez symulatora (np. 197) | `[CAR_SIM] WARN: brak symulatora ...`, na CAN1 tylko `0x01F2F3FF CA FE 00 BA BE 00` co 1 s, carsharing → LX: NACK `ER_WAKEUP_CAN` po ok. 5 s budzenia |
| 11 | `SIM#V_BOX` przy parserze 196 | tryb `RECZNY`, LX traci dane auta (`CS_CAN_INACTIVE` po 1 s); `SIM#AUTO` przywraca Arkanę |
| 12 | Przez cały test: OTA discovery na CAN1 przy prędkości aktywnego auta | odpowiedzi po każdym przełączeniu |

---

### T13 – Etap 2 (osobne zadania, po akceptacji etapu 1)

- Odpowiedzi UDS (przebieg) dla Arkany: `0x743` → `0x763`, bez ostrzeżeń `OBDResponseTimeout` w LX.
- Uśpiona magistrala: `SIM_SEN#1` wstrzymuje nadawanie do ramki budzenia `0x46F 11 11 11` (test `ER_WAKEUP_CAN`
  w aucie, które się budzi).
- Blokada silnika z CAN (`0x46C 03 12 02` → obroty 0), klakson `0x418` → log, przycisk Start/Stop i zamknięcie
  awaryjne (50 ramek Start/Stop + drzwi kierowcy).
- Ramki etapu 2 Arkany (poduszka, przepustnica, obciążenie, TC, LPG).
- Wierniejsze ramki z logu prawdziwego auta, gdy będzie dostępny: rzeczywiste okresy, bajty bazowe, ramki
  milknące przy wyłączonej stacyjce.
- Kolejne auta z generatora (T10), symulator na CAN2 dla parserów dwumagistralowych, zapis wyboru we Flash.
- Zgłaszanie pary „numer parsera + plik wgrany” z RS485 (`can_mod_revision`), np. do `SIM?`.

---

## 6. Kolejność i zależności

```text
T1 (stan auta, io_sim) ──┐
T2 (typy, koder) ────────┼──► T4 (V_BOX) ──► T5 (silnik) ──► T6 (integracja) ──► T7 (komendy) ──► T12 (sprzęt)
T3 (rejestr, BRAK) ──────┘          │                                               ▲
                                    └──► T8 (Arkana) ───────────────────────────────┘
T9 (testy hostowe) rośnie razem z T2, T3, T4, T8      T10 (generator) po T8      T11 (docs) na końcu
```

| Zadanie | Zależy od | Szacunek |
|---|---|---|
| T1 Stan auta, `io_sim`, `io_cmd` | – | 1 dzień |
| T2 Typy, koder, PIN, harmonogram | – | 1 dzień |
| T3 Rejestr, mapa, `BRAK` | T2 | 0.5 dnia |
| T4 Symulator `V_BOX` | T1, T2, T3 | 0.5 dnia |
| T5 Silnik, `cp_car_sim`, wybór automatyczny | T4 | 1.5 dnia |
| T6 Integracja, RS485 | T5 | 0.5 dnia |
| T7 Komendy UART | T5 | 0.5 dnia |
| T8 Arkana | T2, T5 | 1 dzień |
| T9 Testy hostowe | T2, T3, T4, T8 | 1 dzień |
| T10 Generator | T8 | 1 dzień |
| T11 Dokumentacja | T1–T10 | 0.5 dnia |
| T12 Test na sprzęcie | T6, T7, T8 | 0.5 dnia |

Proponowane commity (Conventional Commits, każdy kompilowalny):
0. `chore(git): ignore device parser sources` (D12) i `docs(car-sim): add car simulator implementation plan`
1. `feat(io): add configurable RPM limits and new vehicle signals to the input simulation` (T1)
2. `feat(car-sim): add vehicle state model, signal codec and frame scheduler` (T2)
3. `feat(car-sim): add car simulator registry, device parser map and BRAK marker simulator` (T3)
4. `feat(car-sim): move the v-box telemetry frame into the V_BOX car simulator` (T4)
5. `feat(car-sim): add car simulator task, CAN RX commands and automatic selection` (T5, T6)
6. `feat(car-sim): route car simulator commands from the UART bridge` (T7)
7. `feat(car-sim): add Renault Arkana car simulator for device parser 196` (T8)
8. `test(tools): add host test for car simulators` (T9)
9. `feat(tools): add car simulator skeleton generator` (T10)
10. `docs(car-sim): document car simulators and adding a new car` (T11)

---

## 7. Definition of Done

- [x] Decyzje D1–D13 ustalone (2026-10-27).
- [ ] Symulator `V_BOX` nadaje bajt w bajt to samo co dotychczasowe `io_manager_canCreateFrame()`;
      LX z parserem 192 działa bez zmian.
- [ ] Po zmianie parsera w konfiguracji LX (online i przez RS485) v-box w ≤ 5 s przełącza symulator, a dla
      numeru bez symulatora nadaje tylko ramkę `CA FE 00 BA BE 00`; `SIM#...` / `SIM#AUTO` działają.
- [ ] LX z parserem 196 odczytuje wszystkie sygnały etapu 1 Arkany, uczy się PIN-u z `PILOT#OTWORZ` i wykonuje
      carsharingowe otwórz / zamknij na przekaźniku v-box; `AWARIA_ZAMKA#1` i zły PIN dają NACK.
- [ ] Przyciski i komendy `io_cmd` działają jak dotąd dla każdego symulatora.
- [ ] Profile aut bez FreeRTOS / HAL, testy hostowe zgodne, build produkcyjny, `CAN_FD_TEST` i `RS485_LINK=OFF`
      bez ostrzeżeń, wolna sterta FreeRTOS ≥ 2 KB.
- [ ] OTA na CAN1 działa po każdym przełączeniu symulatora (przy prędkości aktywnego auta).
- [ ] Dokumentacja, `gemini.md` i changelog zaktualizowane.
