# Raport testu CAN FD

- **Data:** 2026-10-23 13:44
- **Wynik (bez błędów protokołu):** 16/16 PASS
- **Wynik funkcjonalny (wszystkie ramki dostarczone poprawnie):** 13/13
- **Węzeł:** NUCLEO-C542RC, zegar FDCAN 144 MHz (PSI z HSE 24 MHz)
- **Magistrala testowana:** CAN2 (FDCAN2); OTA discovery pominięte (ota_manager działa na CAN1)
- **Adapter PC:** candle:0:0 — Candlelight 2.5 - Multiboard (SN: 2074326D554850142)
- **Pętla CAN2→CAN1→CAN2:** pominięta (--skip-loop)
- **Powtórzenia echo:** 10 (każde: wszystkie długości DLC × 6 wzorców × ID std/ext)
- **Czas testu:** 83 s

## Matryca prędkości

| Przypadek | Konfiguracja | Timing węzła (presc/TSEG1/TSEG2) | Echo PC OK/wysł. | Burst | OTA | Błędy (CEL) | Typy błędów arb. / dane | TDCV | Wynik |
|---|---|---|---|---|---|---|---|---|---|
| `classic_125k` | Classic 125k | N 4/251/36 | 1080/1080 | 100/100 | – | 0 | – / – | – | **PASS** |
| `classic_250k` | Classic 250k | N 2/251/36 | 1080/1080 | 100/100 | – | 0 | – / – | – | **PASS** |
| `classic_500k` | Classic 500k | N 1/251/36 | 1080/1080 | 100/100 | – | 0 | – / – | – | **PASS** |
| `classic_1000k` | Classic 1000k | N 1/125/18 | 1080/1080 | 100/100 | – | 0 | – / – | – | **PASS** |
| `fd_500k` | FD 500k | N 1/251/36 | 3000/3000 | 100/100 | – | 0 | – / – | – | **PASS** |
| `fd_1000k` | FD 1000k | N 1/125/18 | 3000/3000 | 100/100 | – | 0 | – / – | – | **PASS** |
| `fd_brs_500k_1m` | FD+BRS 500k/1000k | N 4/62/9, D 4/26/9 | 3000/3000 | 100/100 | – | 0 | – / – | 0 | **PASS** |
| `fd_brs_500k_2m` | FD+BRS 500k/2000k | N 2/125/18, D 2/26/9, TDCO 52 | 3000/3000 | 100/100 | – | 0 | – / – | 65 | **PASS** |
| `fd_brs_500k_4m` | FD+BRS 500k/4000k | N 1/251/36, D 1/26/9, TDCO 26 | 3000/3000 | 100/100 | – | 0 | – / – | 39 | **PASS** |
| `fd_brs_500k_8m` | FD+BRS 500k/8000k | N 1/251/36, D 1/12/5, TDCO 12 | 3000/3000 | 100/100 | – | 0 | – / – | 25 | **PASS** |
| `fd_brs_1000k_2m` | FD+BRS 1000k/2000k | N 2/62/9, D 2/26/9, TDCO 52 | 3000/3000 | 100/100 | – | 0 | – / – | 65 | **PASS** |
| `fd_brs_1000k_4m` | FD+BRS 1000k/4000k | N 1/125/18, D 1/26/9, TDCO 26 | 3000/3000 | 100/100 | – | 0 | – / – | 39 | **PASS** |
| `fd_brs_1000k_8m` | FD+BRS 1000k/8000k | N 1/125/18, D 1/12/5, TDCO 12 | 3000/3000 | 100/100 | – | 0 | – / – | 25 | **PASS** |

## Testy negatywne i odporności

| Test | Wynik | Szczegóły |
|---|---|---|
| N1 FD+BRS 500k/5M rejected, previous config kept | **PASS** | switched=False (rejected by node); node after: Classic 500k |
| N2 FD TX refused on Classic bus, Classic TX accepted | **PASS** | FD probe status=3, Classic probe status=0 |
| N3 unconfirmed switch falls back to Classic 500k | **PASS** | switch accepted=True; node after 5s: Classic 500k |

## Kryteria

- **Funkcjonalnie:** echo – każda ramka wraca z ID+1, tym samym formatem (Classic/FD), flagą BRS, długością i danymi (na magistrali FD wysyłane są też ramki Classic); burst – 100 ramek węzła (64 B FD / 8 B Classic) bez strat; dla FD działa OTA discovery (ramki Classic na magistrali FD).
- **PASS (bez błędów):** dodatkowo 0 błędów protokołu w węźle (licznik CEL – każdy wzrost TEC/REC, nie maleje, więc widać też błędy ukryte przez automatyczną retransmisję), brak bus-off, brak odrzuconych ramek TX; z pętlą – również 200/200 ramek CAN2→CAN1→CAN2 bez błędów.
- **Typy błędów:** liczone w przerwaniu PEA/PED (STUFF, FORM, ACK, BIT1, BIT0, CRC). TDCV: zmierzone położenie SSP (opóźnienie transceivera + TDCO) w okresach zegara FDCAN (6.94 ns).

## Log UART węzła (bez cyklicznych logów IO/MAIN)

```text
[CAN_FD_TEST] bus under test: CAN2 (loop partner: CAN1)
[CAN_FD_TEST] SET_CONFIG Classic nominal=125 kbps data=0 kbps
[CAN2] bus config: Classic nominal=125000 bps data=125000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: Classic nominal=125000 bps data=125000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG Classic nominal=250 kbps data=0 kbps
[CAN2] bus config: Classic nominal=250000 bps data=250000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: Classic nominal=250000 bps data=250000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG Classic nominal=500 kbps data=0 kbps
[CAN2] bus config: Classic nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: Classic nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG Classic nominal=1000 kbps data=0 kbps
[CAN2] bus config: Classic nominal=1000000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: Classic nominal=1000000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD nominal=500 kbps data=0 kbps
[CAN2] bus config: FD nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD nominal=1000 kbps data=0 kbps
[CAN2] bus config: FD nominal=1000000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD nominal=1000000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=1000 kbps
[CAN2] bus config: FD+BRS nominal=500000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD+BRS nominal=500000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=2000 kbps
[CAN2] bus config: FD+BRS nominal=500000 bps data=2000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD+BRS nominal=500000 bps data=2000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=4000 kbps
[CAN2] bus config: FD+BRS nominal=500000 bps data=4000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD+BRS nominal=500000 bps data=4000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=8000 kbps
[CAN2] bus config: FD+BRS nominal=500000 bps data=8000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD+BRS nominal=500000 bps data=8000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=1000 kbps data=2000 kbps
[CAN2] bus config: FD+BRS nominal=1000000 bps data=2000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD+BRS nominal=1000000 bps data=2000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=1000 kbps data=4000 kbps
[CAN2] bus config: FD+BRS nominal=1000000 bps data=4000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD+BRS nominal=1000000 bps data=4000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=1000 kbps data=8000 kbps
[CAN2] bus config: FD+BRS nominal=1000000 bps data=8000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD+BRS nominal=1000000 bps data=8000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG Classic nominal=500 kbps data=0 kbps
[CAN2] bus config: Classic nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: Classic nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=5000 kbps
[CAN2] ERROR: bus config FD+BRS 500000/5000000 bps not supported, restoring previous
[CAN_FD_TEST] SET_CONFIG rejected, previous configuration kept
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=2000 kbps
[CAN2] bus config: FD+BRS nominal=500000 bps data=2000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: FD+BRS nominal=500000 bps data=2000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] no test traffic in new configuration, falling back to Classic 500 kbps
[CAN2] bus config: Classic nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN1] bus config: Classic nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] SET_CONFIG Classic nominal=500 kbps data=0 kbps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
```

## Jak powtórzyć test

Procedura opisana w [can_fd_test_guide.md](can_fd_test_guide.md).
