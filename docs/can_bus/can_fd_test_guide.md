# Test magistrali CAN FD – instrukcja

Moduł testowy sprawdza wszystkie tryby i prędkości CAN / CAN FD węzła V-Box na żywej magistrali.
Firmware testowy przełącza konfigurację **przez te same warstwy co produkcja** (profil parsera →
`can_manager_setBusConfig()` → adapter portu z kalkulatorem bit timingu), a narzędzie PC
(`tools/can_fd_test.py`) steruje przebiegiem, weryfikuje ramki i generuje raport Markdown.

## 1. Elementy

| Element | Plik | Rola |
|---|---|---|
| Moduł testowy | `app/can_bus/can_fd_test/can_fd_test.c/.h` | Protokół testowy, przełączanie konfiguracji, echo, burst, pętla CAN1↔CAN2, statystyki |
| Parser testowy | `app/can_bus/can_parser/parsers/cp_fd_test.c/.h` | Profil `CAN_PARSER_ID_FD_TEST` ze **zmienną** `bus_config` |
| Narzędzie PC | `tools/can_fd_test.py` | Matryca testów, kontrola ramek, log UART, raport |
| Diagnostyka portu | `can_interface_t::get_status` | TEC/REC, LEC/DLEC, TDCV, licznik błędów (CEL), błędy per typ (przerwanie PEA/PED) |

Protokół (ID, komendy, formaty odpowiedzi) jest opisany w nagłówku `can_fd_test.h`.

## 2. Wymagania

- NUCLEO-C542RC podłączone przez USB (ST-Link: programowanie SWD + VCP z logami, np. `COM22`).
- Adapter **candleLight z obsługą CAN FD** (python-can-candle / `candle_api`) na magistrali CAN1.
- Terminacja: **dokładnie dwa** rezystory 120 Ω na końcach magistrali (pomiar między CAN_H a CAN_L
  przy wyłączonym zasilaniu ≈ 60 Ω). Każdy dodatkowy moduł z własnym terminatorem obniża rezystancję.
- Python 3.8+: `pip install -r tools/requirements_ota.txt` (python-can, python-can-candle, pyserial).

> [!WARNING]
> Na stanowisku z raportu na tej samej magistrali wisi też zewnętrzny transceiver **FDCAN2** (PB5/PB6)
> z własnym terminatorem. W buildzie testowym CAN2 domyślnie **nie jest uruchamiany** (milczy), ale jego
> transceiver nadal obciąża linię. Do pomiarów „na czysto” odłącz moduł CAN2 od magistrali.

## 3. Build i wgranie firmware testowego

Firmware testowy budowany jest w **osobnym katalogu**, żeby opcja nie trafiła do builda produkcyjnego:

```bash
cmake --preset debug_GCC_NUCLEO-C542RC -B build/can_fd_test -DCAN_FD_TEST=ON
cmake --build build/can_fd_test
STM32_Programmer_CLI -c port=SWD mode=UR -w build/can_fd_test/v_box_full.hex -v -rst
```

Opcje:

| Opcja CMake | Domyślnie | Znaczenie |
|---|---|---|
| `CAN_FD_TEST` | `OFF` | Moduł testowy, parser `CP_FD_TEST` zastępuje `V_BOX` na CAN1, wyłączone logi każdej ramki TX |
| `CAN_FD_TEST_LOOP` | `OFF` | CAN2 jako partner pętli CAN1↔CAN2 (zawsze w tej samej konfiguracji co CAN1). **Tylko gdy transceiver CAN2 obsługuje CAN FD.** |

Po starcie w logu UART pojawia się:

```text
[CAN_FD_TEST] CAN FD test module active on CAN1 (loop partner: none)
[MAIN] FreeRTOS heap free: ... B
```

## 4. Uruchomienie testu

```bash
# pełna matryca + testy negatywne, raport do docs/can_bus/can_fd_test_report.md
python tools/can_fd_test.py --uart COM22

# wybrane przypadki, więcej powtórzeń, raport w innym miejscu
python tools/can_fd_test.py --uart COM22 --cases "fd_brs_500k_2m,fd_brs_1000k_8m" --reps 20 --report my.md
```

| Parametr | Znaczenie |
|---|---|
| `--uart` | Port VCP węzła – log UART dołączany do raportu (opcjonalny) |
| `--reps` | Powtórzenia zestawu echo (domyślnie 10; jeden zestaw = 108 ramek Classic + 192 FD) |
| `--cases` | Lista przypadków (`classic_125k`, `classic_250k`, `classic_500k`, `classic_1000k`, `fd_500k`, `fd_1000k`, `fd_brs_500k_1m/2m/4m/8m`, `fd_brs_1000k_2m/4m/8m`) |
| `--skip-negative` | Pomija testy negatywne (N1–N3) |
| `--channel` | Kanał adaptera (`candle:0:0`), gdy podłączonych jest kilka |

Kod wyjścia: `0` – wszystko PASS, `1` – jest FAIL, `2` – węzeł nie odpowiada.

## 5. Przebieg jednego przypadku

1. **Przełączenie:** `SET_CONFIG` w bieżącej konfiguracji → ACK → PC **natychmiast zamyka adapter**,
   węzeł po 200 ms przełącza CAN1 przez `can_parser_select()` → PC otwiera adapter w nowej konfiguracji →
   `PING` musi zwrócić nową konfigurację. Jeśli węzeł konfigurację odrzucił, `PING` zwraca starą.
2. **Zabezpieczenie:** bez żadnej ramki testowej przez 5 s po przełączeniu węzeł sam wraca do Classic 500k.
3. **Pętla CAN1↔CAN2** (tylko `CAN_FD_TEST_LOOP=ON`): PC zamyka adapter, węzeł wysyła 200 ramek
   CAN1→CAN2→CAN1 – referencja bez udziału PC.
4. **Echo:** wszystkie długości DLC (0–8, 12, 16, 20, 24, 32, 48, 64) × 6 wzorców × ID std/ext;
   na magistrali FD także ramki Classic (ruch mieszany). Kontrola ID, formatu, BRS, długości i danych.
5. **Burst:** węzeł nadaje 100 ramek (64 B FD / 8 B Classic) z numeracją – kontrola ciągłości i treści.
6. **Statystyki:** licznik błędów protokołu (CEL – każdy wzrost TEC/REC, nie maleje), błędy per typ
   (przerwanie PEA/PED), TEC/REC, TDCV, odrzucone ramki TX, restarty po bus-off.
7. **FD:** OTA discovery (`can_updater.scan_nodes`) – ramki Classic na magistrali FD.

Testy negatywne: **N1** 500k/5M musi zostać odrzucone (nieosiągalne przy 144 MHz) i węzeł zostaje
w poprzedniej konfiguracji, **N2** ramka FD na magistrali Classic odrzucona przez port, Classic przechodzi,
**N3** niepotwierdzona konfiguracja wraca do Classic 500k po 5 s.

## 6. Interpretacja wyników

- **Funkcjonalnie OK** – wszystkie ramki dostarczone poprawnie (ewentualnie po retransmisji).
- **PASS** – dodatkowo **0 błędów protokołu** po stronie węzła. Automatyczna retransmisja ukrywa
  błędy przed aplikacją, dlatego kryterium opiera się na liczniku CEL, a nie na samym echo.
- Typy błędów: `BIT0` w fazie arbitrażu (nadajnik wystawia dominantę, a widzi recesyw) nie powinien
  wystąpić na poprawnej magistrali – wskazuje na problem warstwy fizycznej (transceiver, połączenia).
- **TDCV** (tylko BRS z TDC): `TDCV − TDCO` = zmierzone opóźnienie pętli transceivera w okresach
  6.94 ns (na stanowisku: 14 ≈ 97 ns, stałe dla 2/4/8 Mbps).

## 7. Po teście – powrót do firmware produkcyjnego

```bash
cmake --build build/debug_GCC_NUCLEO-C542RC
STM32_Programmer_CLI -c port=SWD mode=UR -w build/debug_GCC_NUCLEO-C542RC/v_box_full.hex -v -rst
```

## 8. Znane ograniczenia narzędzia PC

- python-can nie znajduje timingu 125 kbps przy zegarze adaptera 160 MHz i dla FD wybiera grube
  prescalery fazy danych – `tools/can_fd_test.py` podstawia własny kalkulator (ten sam algorytm co węzeł).
- `CandleBus.shutdown()` nie zamyka urządzenia USB – tester zamyka je jawnie, inaczej ponowne otwarcie
  z innym timingiem kończy się `Cannot open device`.
- Przerwanie skryptu w trakcie operacji USB może zostawić proces, którego nie da się zabić i który
  trzyma adapter oraz port COM – pomaga dopiero ponowne podłączenie USB.
