# Dodawanie auta do symulatorów (`app/car_sim/sims/`)

Instrukcja dodania symulatora auta na podstawie parsera urządzenia LX (plik `.c` z tablicą `ThisCar`).
Działanie modułu: [car_sim.md](car_sim.md). Wzorzec: `app/car_sim/sims/sim_renault_arkana.c` (parser 196).

> **Zasada:** profil opisuje **auto**, nie parser. Parser urządzenia jest tylko źródłem wiedzy o ramkach
> (ID, bity, wzory). Symulatora nie dopasowuje się do błędów ani skrótów parsera, bo test ma je wykazać.
> **Źródło parsera LX nie trafia do repozytorium:** w commitowanych plikach są tylko ID, maski i wzory.

## Spis treści
1. [Kroki](#1-kroki)
2. [Generator szkieletu](#2-generator-szkieletu)
3. [Opis ramek i sygnałów](#3-opis-ramek-i-sygnałów)
4. [Komendy LX, zamek i PIN](#4-komendy-lx-zamek-i-pin)
5. [Reguły profilu](#5-reguły-profilu)
6. [Testy](#6-testy)

---

## 1. Kroki

1. Skopiuj parser urządzenia do `app/temp_lx_parsers/` (katalog w `.gitignore`, tylko do odczytu).
2. Wygeneruj szkielet (rozdział 2):
   ```bash
   python tools/create_car_sim.py ARKANA app/temp_lx_parsers/196_CanA_RenaultArkana_carshare.c --name "Renault Arkana"
   ```
3. Dla każdej ramki opisz sygnały na podstawie wklejonego handlera LX (rozdział 3), dopisz komendy LX, bajty
   pilota i limity obrotów (rozdział 4).
4. **Usuń wklejony kod parsera LX** z komentarzy i zastąp go opisem: ID, bajty, maski, wzory (jak w nagłówku
   `sim_renault_arkana.c`).
5. Dopisz kroki testu hostowego z dekoderem referencyjnym (rozdział 6), zbuduj wszystkie konfiguracje.
6. Zaktualizuj [car_sim.md](car_sim.md) (tabela symulatorów, tabela sygnałów auta) i `docs/changelog.md`.
7. Test na sprzęcie z LX według [car_sim.md, rozdział 8](car_sim.md#8-testy).

## 2. Generator szkieletu

```bash
python tools/create_car_sim.py <KLUCZ> <parser_LX.c> [--name "Nazwa auta"] [--dev-parser 196] [--dry-run]
```

- `KLUCZ`: `A-Z 0-9 _`, maks. 8 znaków (kolumna `SIM_LIST`), nie `AUTO` / `BRAK`. Plik: `sim_<klucz>.c/.h`.
- `--name`: nazwa w `SIM_LIST` i w logu, maks. 20 znaków (domyślnie klucz).
- `--dev-parser`: numer parsera urządzenia (domyślnie z początku nazwy pliku, np. `196_...`). Numer zajęty
  przez inny symulator jest błędem, bo jeden numer wybiera jeden symulator.
- `--dry-run`: tylko analiza i podsumowanie.

Generator czyta parser LX tylko w czasie uruchomienia (kodowanie UTF-8 albo cp1250 linia po linii) i:

| Krok | Wynik |
|---|---|
| Tablica `ThisCar`: `BTR_500K` → `CAN_BAUDRATE_500K`, wpisy `{CAN_x, MCR_..., ID, flagi, handler}` (starszy szkielet bez `CAN_x`: wpis na CAN1) | ramki cykliczne: 100 ms, DLC 8, puste listy sygnałów, ext przy `bt_CAN_B` |
| Ciało handlera każdej ramki | komentarz nad ramką + podpowiedzi `TODO: { .sig = SIG_?, .enc = SIM_ENC_BIT, .byte = N, .mask = 0xMM }` z wyrażeń `Msg[N] & 0xMM` i `TODO: UINT field from bytes [..]` dla bajtów czytanych bez maski |
| Handler korzystający z `PinCode` | pilot `s_fob` (ID wpisu, `pin_byte` z `Data[N] = PinCode >> 8` w `CAN_TransmitACK`, PIN domyślny `0x1234`, bajty unlock / lock do uzupełnienia) |
| Wywołania `CAN_TransmitACK(0, 0xID, Data, ...)` | lista kandydatów na komendy RX w nagłówku pliku, z bajtami `Data[]` (`PIN_H` / `PIN_L`, `??` = nieznane), ID ≥ `0x700` oznaczone jako diagnostyka (etap 2) |
| Wpisy CAN locker (`LOCKER_*`, `Lincor`, `LockAnswer`), odpowiedzi diagnostyczne (ID ≥ `0x700`, `Response`), wpisy CAN2 | pominięte, wypisane w nagłówku pliku |
| Rejestracja | `CAR_SIM_ID_<KLUCZ>` w `car_sim_registry.h` (przed `CAR_SIM_ID_NONE`), getter w `s_sims[]` (`car_sim_registry.c`), źródło w `CMakeLists.txt` i w `SOURCES` testu hostowego |

Dla parsera 196 (27 wpisów `ThisCar`) powstaje kompilujący się szkielet z 22 ramkami cyklicznymi, pilotem `0x46F`
(`pin_byte = 1`), kandydatami komend `0x46F 32 PIN_H PIN_L 18|10 04 84 07 30`, diagnostyką `0x763` / `0x765`
i pominiętymi wpisami CAN locker. Szkielet kompiluje się i przechodzi kontrolę rejestru, ale nadaje same zera.

## 3. Opis ramek i sygnałów

Profil to dane `static const`, jak plik DBC. Ramka (`sim_frame_t`): `id`, `ext`, `dlc`, `period_ms` (wielokrotność
10 ms), `init[8]` (bajty domyślne), tablica `signals` i opcjonalny hook `encode` (wywoływany po sygnałach).
Sygnał (`sim_signal_t`) koduje jedno pole stanu auta (`vehicle_state_t`):

| Sygnał | Źródło v-box | Typowe kodowanie |
|---|---|---|
| `SIG_ACC`, `SIG_DOOR_DRV`, `SIG_DOOR_OTHER`, `SIG_LIGHTS`, `SIG_HAZARD`, `SIG_HOOD`, `SIG_HANDBRAKE` | SW0..SW7 / komendy IO | `BIT` |
| `SIG_LOCKED` | SW4 (sprzężenie przekaźnika) | `BIT`, często kilka wpisów (zamek w kilku ramkach) |
| `SIG_RPM` | SW8 (`idle_rpm`) / `OBROTY#`; **0 przy wyłączonej stacyjce** (D13) | `UINT` |
| `SIG_FUEL` | potencjometr / `PALIWO#` (0..100 %) | `UINT` |
| `SIG_SPEED`, `SIG_ODOMETER` (m), `SIG_ENGINE_TEMP` (°C), `SIG_LIGHT_HIGH`, `SIG_BRAKE_PEDAL` | komendy UART (`ZRODLO=UART`) | `UINT` / `BIT` |

Kodowania:
- **`SIM_ENC_BIT`**: bit `mask` w bajcie `byte` ustawiony, gdy wartość ≠ 0. Ten sam sygnał może mieć kilka wpisów
  (Arkana: zamek = 4 bity w 2 ramkach).
- **`SIM_ENC_UINT_BE` / `SIM_ENC_UINT_LE`**: pole `len` bajtów (1..4) od `byte`. `raw = wartość × mul / div + offset`,
  nasycone do `0..max` (i szerokości pola), przesunięte o `shift` bitów w lewo; bity poniżej `shift` zostają z
  `init`. Wartość bool w polu `UINT` daje 0 / 1 × `mul`.

Przykłady z Arkany (wzór parsera LX → wpis profilu):

| Parser LX | Wpis profilu |
|---|---|
| `if (Msg[6] & 0x80) ACC = 1` | `{ SIG_ACC, SIM_ENC_BIT, .byte = 6, .mask = 0x80 }` |
| `rpm = ((Msg[5] << 8) \| Msg[6]) * 0.125` | `{ SIG_RPM, SIM_ENC_UINT_BE, .byte = 5, .len = 2, .mul = 8 }` |
| `speed = ((Msg[0] << 8) \| Msg[1]) * 0.00625`, `Msg[0] == 0xFF` = brak | `{ SIG_SPEED, SIM_ENC_UINT_BE, .byte = 0, .len = 2, .mul = 160, .max = 0xFEFF }` |
| `dist = (Msg[0..2] << 4 \| Msg[3] >> 4) * 10`, `FF FF FF F0` = brak | `{ SIG_ODOMETER, SIM_ENC_UINT_BE, .byte = 0, .len = 4, .shift = 4, .div = 10, .max = 0x0FFFFFFE }` |
| `temp = Msg[0] - 40` | `{ SIG_ENGINE_TEMP, SIM_ENC_UINT_BE, .byte = 0, .len = 1, .offset = 40 }` |
| `if (((Msg[6] << 8) \| Msg[7]) > 5) brake = 1`, `!(Msg[4] & 0x04)` = zdarzenie ESP | `{ SIG_BRAKE_PEDAL, SIM_ENC_UINT_BE, .byte = 6, .len = 2, .mul = 100 }`, `init[4] = 0x04` |

Hooki (`encode`, `on_rx`, `on_select`) tylko na wyjątki, których nie da się opisać tablicą (np. licznik ramek
`V_BOX`). Hooki to też czyste C: bez FreeRTOS, HAL i `io_manager`.

## 4. Komendy LX, zamek i PIN

- **Komendy** (`sim_cmd_t`): `id`, `ext`, `match[8]` / `match_mask[8]` (porównywane bity; bajt z maską ≠ 0 musi
  być w ramce), `pin_byte` (pierwszy bajt PIN-u BE, −1 = bez PIN-u), `action`: `SIM_ACT_UNLOCK` / `SIM_ACT_LOCK`
  (przekaźnik zamka, `AWARIA_ZAMKA#1` blokuje), `SIM_ACT_LOG`, `SIM_ACT_CUSTOM` (`on_rx` zwraca akcję). Bajtów PIN-u
  nie maskuj: PIN sprawdza silnik, a zły PIN daje `WARN` bez akcji (D8).
- **Zamek:** sygnał `SIG_LOCKED` (SW4) zakoduj dokładnie tak, jak parser rozpoznaje „zamknięte” (wszystkie bity,
  wszystkie ramki). Komenda zmienia przekaźnik, a stan w ramkach wynika z jego sprzężenia, jak w aucie.
- **Pilot** (`sim_fob_t`): ramki `unlock` / `lock` auta, `pin_byte`, `default_pin`. `PILOT#` nadaje je z PIN-em
  v-box i przełącza przekaźnik. Pilota nigdy nie wysyłaj automatycznie.
- **Limity obrotów:** `rpm_max`, `rpm_step` (krok `OBROTY#`), `idle_rpm` (SW8).
- **Magistrala:** `bus_config` (Classic / FD, prędkość). Unikalne ID komend + 2 filtry BOOT_OTA ≤ 16; kontroler:
  maks. 8 filtrów extended.

## 5. Reguły profilu

Kontrola przy starcie (`car_sim_registry_check()`, test hostowy `CHECK`) odrzuca profil z błędem w logu, a
firmware nie startuje:

| Reguła | Błąd |
|---|---|
| 1..32 ramek TX, ID w zakresie formatu, bez powtórzonych ID, okres wielokrotnością 10 ms | `ramka TX: ...` |
| Sygnał w granicach DLC, maska ≠ 0, `len` 1..4, `shift` < szerokość pola | `sygnał: pole poza ramką lub zła maska / długość` |
| PIN komendy w ramce (`pin_byte + 2 ≤ 8`), `SIM_ACT_CUSTOM` z `on_rx` | `komenda RX: PIN poza ramką`, `komenda RX: SIM_ACT_CUSTOM bez on_rx` |
| Pilot: ID w zakresie, DLC ≤ 8, PIN w DLC | `pilot: złe ID, DLC lub pozycja PIN-u` |
| Limity obrotów (`rpm_step` ≥ 1, `idle_rpm` ≤ `rpm_max`), konfiguracja magistrali | `złe limity obrotów`, `brak konfiguracji magistrali` |
| Unikalny klucz, numer parsera urządzenia w jednym symulatorze, numer ≠ 0 (`BRAK`) | `klucz powtórzony`, `parser urządzenia N ...` |
| Filtry auta + BOOT_OTA ≤ 16 | `symulator X: N filtrów + 2 BOOT_OTA > 16` |

Zasady auta (nie wykrywa ich kontrola, sprawdza je test z dekoderem):
- auto nie nadaje kodów „brak danych” parsera (`max` sygnału), chyba że test tego wymaga,
- bez stacyjki obroty 0 (D13, zapewnia `vehicle_state_build()`),
- bity, które parser czyta razem jako inny stan (Arkana: oba bity świateł `0x5BB`), nie są ustawiane naraz,
- nieznane bajty = 0, a bajty, od których zależą zdarzenia w parserze (Arkana: ESP `0x206 [4]`), w `init`.

## 6. Testy

1. **Test hostowy** (`tools/test_car_sim_host.py`): dopisz dekoder referencyjny auta (maski i wzory parsera
   wpisane na stałe, test nie czyta pliku parsera), jak `arkana_decode()`, listę stanów auta (każdy sygnał osobno,
   kombinacje, wartości skrajne, D13) i kroki `SIM <KLUCZ>`, `RX` (komendy z PIN-em zgodnym, niezgodnym,
   nadpisanym, obce ramki), `FOB`, `SCHEDSTAT 1000`, `MAP <numer>`. Generator dodaje plik do `SOURCES`.
   ```bash
   python tools/test_car_sim_host.py --docker-image vbox-builder:latest
   ```
2. **Buildy** bez ostrzeżeń: domyślny, `-DRS485_LINK=OFF`, `-DCAN_FD_TEST=ON -DCAN_FD_TEST_LOOP=ON`,
   `-DCAN_UART_BRIDGE=OFF`.
3. **Sprzęt:** procedura z [car_sim.md, rozdział 8](car_sim.md#8-testy) z numerem i sygnałami nowego auta.
