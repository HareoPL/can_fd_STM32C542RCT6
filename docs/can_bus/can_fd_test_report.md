# Raport testu CAN FD – 2026-10-22

Stanowisko: NUCLEO-C542RC (FDCAN1, transceiver MCP2562FD, zegar FDCAN 144 MHz z PSI/HSE 24 MHz),
adapter **Candlelight 2.5 Multiboard** (CAN FD, zegar 160 MHz), terminacja na magistrali.
Na tej samej magistrali jest też **zewnętrzny transceiver FDCAN2** (PB5/PB6) – patrz wnioski.

> [!IMPORTANT]
> Raport opisuje pomiary z sesji uruchomieniowej narzędzia. Ostatni pełny przebieg automatyczny
> (z finalną wersją narzędzia) nie został wykonany: zawieszony proces testowy zablokował adapter
> candleLight i port COM22 do czasu ponownego podłączenia USB. Procedura powtórzenia:
> [can_fd_test_guide.md](can_fd_test_guide.md).

## 1. Podsumowanie

| Obszar | Wynik |
|---|---|
| Obliczenia bit timingu (kalkulator vs zegar 144 MHz) | **OK** – wszystkie prędkości dokładne, SP 87.5% / 75% (8M: 72.2%), wspólny prescaler faz |
| Przełączanie trybu/prędkości przez warstwę parsera | **OK** – 13 konfiguracji, odrzucenie 5M, powrót awaryjny do Classic 500k |
| Transmisja Classic 125k / 500k / 1M (PC↔węzeł) | **PASS** – 0 błędów protokołu |
| Transmisja Classic 250k | **FAIL** – brak komunikacji, przyczyna nieustalona (pkt 5) |
| CAN FD 500k/1M, FD+BRS 1/2/4/8 Mbps – funkcjonalnie | **OK** – ramki 0–64 B, Classic i FD w ruchu mieszanym, BRS, OTA discovery na magistrali FD |
| CAN FD – bez błędów protokołu | **1 z 9** konfiguracji FD (1M/8M) bez błędów; w pozostałych błędy STUFF/FORM ukryte przez retransmisję |
| TDC (kompensacja opóźnienia transceivera) | **OK** – zmierzone opóźnienie 14 × 6.94 ns ≈ 97 ns, stałe dla 2/4/8 Mbps |
| Testy negatywne N1–N3 | **PASS** |

**Wniosek główny:** konfiguracja CAN FD węzła (timing, BRS, TDC, format ramek) jest poprawna, co
potwierdza bezbłędna praca przy 1M/8M BRS. Pozostałe błędy nie zależą od prędkości fazy danych
(występują nawet w FD 500k bez BRS, gdzie obowiązuje wyłącznie timing nominalny, identyczny z bezbłędnym
Classic 500k). Wskazują więc na warstwę fizyczną stanowiska lub adapter PC, a nie na obliczenia węzła.
Do potwierdzenia po uporządkowaniu magistrali (pkt 6).

## 2. Timing węzła (kalkulator `stm32c5_calc_bit_timing`, 144 MHz)

| Konfiguracja | Nominal presc/TSEG1/TSEG2 | Data presc/TSEG1/TSEG2 | TDCO | TDCV zmierzone |
|---|---|---|---|---|
| Classic 125k / 250k / 500k / 1M | 4/251/36, 2/251/36, 1/251/36, 1/125/18 | – | – | – |
| FD 500k / 1M (bez BRS) | 1/251/36, 1/125/18 | – | – | – |
| FD+BRS 500k/1M | 4/62/9 | 4/26/9 (75%) | – (presc > 2) | – |
| FD+BRS 500k/2M | 2/125/18 | 2/26/9 (75%) | 52 | 66 |
| FD+BRS 500k/4M | 1/251/36 | 1/26/9 (75%) | 26 | 40 |
| FD+BRS 500k/8M | 1/251/36 | 1/12/5 (72.2%) | 12 | 26 |
| FD+BRS 1M/2M, 4M, 8M | 2/62/9, 1/125/18, 1/125/18 | jak wyżej | 52/26/12 | – |
| FD+BRS 500k/5M | – | **nieosiągalne** przy 144 MHz (144/5 niecałkowite) – odrzucone (N1) | | |

## 3. Wyniki – PC ↔ węzeł (CAN2 wyłączony, 1 powtórzenie echo)

Firmware testowy bez CAN2 (FDCAN2 w INIT). Echo: 108 ramek Classic (+192 FD dla trybów FD).
„Błędy” = licznik CEL węzła (każdy wzrost TEC/REC) w fazie echo+burst.

| Przypadek | Echo OK/wysł. | Burst | OTA | Błędy | Wynik |
|---|---|---|---|---|---|
| classic_125k | 108/108 | 100/100 | – | 0 | **PASS** |
| classic_250k | – | – | – | – | **FAIL** (brak komunikacji, pkt 5) |
| classic_500k | 108/108 | 100/100 | – | 0 | **PASS** |
| classic_1000k | 108/108 | 100/100 | – | 0 | **PASS** |
| fd_500k | 300/300 | 100/100 | OK | 20 | funkcjonalnie OK |
| fd_1000k | 300/300 | 100/100 | OK | 40 | funkcjonalnie OK |
| fd_brs_500k_1m | 294/300 | 100/100 | OK | 272 | FAIL |
| fd_brs_500k_2m | 298/300 | 100/100 | OK | 98 | FAIL |
| fd_brs_500k_4m | 299/300 | 100/100 | OK | 93 | FAIL |
| fd_brs_500k_8m | 300/300 | 100/100 | OK | 21 | funkcjonalnie OK |
| fd_brs_1000k_2m | 300/300 | 100/100 | OK | 22 | funkcjonalnie OK |
| fd_brs_1000k_4m | 300/300 | 7/100 | OK | 17 | FAIL (bus-off węzła podczas burst) |
| fd_brs_1000k_8m | 300/300 | 100/100 | OK | **0** | **PASS** |

Testy negatywne:

| Test | Wynik | Szczegóły |
|---|---|---|
| N1 FD+BRS 500k/5M odrzucone | **PASS** | węzeł zostaje w Classic 500k |
| N2 TX ramki FD na magistrali Classic | **PASS** | port odrzuca FD (status 3), Classic przechodzi |
| N3 niepotwierdzona konfiguracja | **PASS** | po 5 s powrót do Classic 500k |

## 4. Analiza błędów

### 4.1 Kierunek i typ (przerwanie PEA/PED, 30 ramek na pozycję)

| Ruch | FD 500k (bez BRS) | FD+BRS 500k/2M |
|---|---|---|
| PC → węzeł, Classic 8 B | 0 | 0 |
| PC → węzeł, FD 8 B | 16 × STUFF (arb.) | 10 × STUFF + 9 × FORM (dane) |
| PC → węzeł, FD 64 B | 1 × STUFF | 22 × STUFF (dane) |
| węzeł → PC, burst FD 64 B | 2 × BIT1 | 2 × BIT1 (dane) |

- Błędy dotyczą **wyłącznie ramek FD** i głównie **ramek nadawanych przez adapter PC**. Ramki Classic
  z tego samego adaptera, przy tej samej prędkości, są bezbłędne.
- Ramki FD z PC wysyłane pojedynczo: prawie każda zawodzi 1–2 razy (FORM dla 0–6 B, STUFF dla ≥ 7 B),
  a potem przechodzi przy retransmisji, więc aplikacja dostaje poprawne dane.
- Błędy występują już w FD 500k bez BRS, gdzie cała ramka idzie timingiem nominalnym (tym samym, co
  bezbłędny Classic 500k). **Wyklucza to timing fazy danych i TDC jako przyczynę.**

### 4.2 Referencja bez PC – pętla CAN1 ↔ CAN2 (`CAN_FD_TEST_LOOP=ON`, adapter PC zamknięty)

| Konfiguracja | Pętla | Błędy | Typy |
|---|---|---|---|
| Classic 500k | 200/200 | 0 | – |
| Classic 1M | 50/50 (raz), 93/200 (raz) | 0 / 74 | BIT1, **BIT0** (arb.) |
| FD 500k | 198/200 | 2–15 | BIT1 |
| FD+BRS 500k/1M | 193/200 | 98 | **BIT0** 128 (arb.), STUFF/FORM/BIT1 (dane) |
| FD+BRS 500k/2M | 184/200 | 248 | **BIT0** (arb.), BIT1 (dane), 9 × bus-off |
| FD+BRS 500k/4M | 179/200 | 376 | **BIT0** (arb.), BIT1/STUFF/FORM/ACK (dane) |
| FD+BRS 500k/8M | 196/200 | 36 | **BIT0** (arb.), BIT1 (dane) |
| FD+BRS 1M/8M | 200/200 | **0** | – |

**BIT0 w fazie arbitrażu** oznacza, że nadajnik wystawia bit dominujący, a odczytuje recesywny. Na
poprawnej magistrali to niemożliwe, bo dominanta zawsze wygrywa. Ten sam MCU z identycznym timingiem
po obu stronach daje niestabilne wyniki, także w Classic 1M. **Pętla przez transceiver CAN2 nie jest
wiarygodną referencją na tym stanowisku.**

## 5. Problemy otwarte

1. **Classic 250k:** ani PC (prescalery 4/5/8), ani CAN2 w tej samej konfiguracji nie komunikują się
   z CAN1, a przy 125k i 500k wszystko działa. Wyliczony timing (2/251/36, 72 MHz / 288 tq) jest
   arytmetycznie poprawny. Przygotowany był odczyt zwrotny rejestru NBTP, ale przerwała go blokada USB.
   **Do zbadania** (odczyt `FDCAN1->NBTP` przez SWD po przełączeniu na 250k).
2. **Błędy ramek FD na stanowisku** – patrz pkt 4 i rekomendacje.

## 6. Rekomendacje (sprzęt)

1. Zmierzyć rezystancję CAN_H–CAN_L przy wyłączonym zasilaniu: powinna wynosić ≈ 60 Ω. Moduł
   transceivera CAN2 ma własny terminator 120 Ω (wg `can_bootloader_and_ota_guide.md`), więc magistrala
   może mieć 3 terminatory (≈ 40 Ω).
2. Sprawdzić typ transceivera CAN2: SN65HVD230 (wymieniony w dokumentacji) jest przeznaczony do 1 Mbit/s
   i nie jest specyfikowany do CAN FD.
3. Odłączyć moduł CAN2 od magistrali i powtórzyć `python tools/can_fd_test.py --uart COM22`.
   Oczekiwanie: brak błędów STUFF/FORM w FD 500k. Jeśli zostaną, przyczyna leży w adapterze PC.
4. **Produkcja:** CAN2 pracuje na stałe w Classic 500k (profil EXAMPLE). Jeśli CAN1 i CAN2 mają dzielić
   jedną magistralę, CAN1 może używać wyłącznie nominalnej prędkości 500k. Inna prędkość nominalna zakłóci
   magistralę, co tu potwierdzono pomiarowo.

## 7. Poprawki wprowadzone podczas testów

| Problem | Poprawka |
|---|---|
| Po bus-off M_CAN zostawał w INIT na zawsze (stan HAL nadal ACTIVE) – węzeł „głuchł” | `can_manager`: nadzór bus-off w tasku TX (co 100 ms i po błędzie TX), restart kontrolera |
| Sterta FreeRTOS wyczerpana po wzroście `CAN_msg_t` do 72 B (`app_synctasks_init failed`) | `configTOTAL_HEAP_SIZE` 24 → 28 KB, log wolnej sterty po starcie |
| Brak diagnostyki magistrali | `get_status()` w porcie: TEC/REC, LEC/DLEC, TDCV, licznik CEL, błędy per typ z przerwania PEA/PED |
| python-can: brak timingu 125k przy 160 MHz, grube prescalery FD | `tools/can_fd_test.py`: własny kalkulator (ten sam algorytm co węzeł) |
| `CandleBus.shutdown()` nie zamyka urządzenia USB | jawne `device.close()` w testerze |
| `open_can_bus()` na sztywno `fd=False` | opcjonalne parametry `fd`, `data_bitrate`, `sample_point`, `data_sample_point` |
