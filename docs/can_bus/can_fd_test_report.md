# Raport testu CAN FD

- **Data:** 2026-10-23 07:56
- **Wynik (bez błędów protokołu):** 2/14 PASS
- **Wynik funkcjonalny (wszystkie ramki dostarczone poprawnie):** 4/13
- **Węzeł:** NUCLEO-C542RC, FDCAN1, zegar FDCAN 144 MHz (PSI z HSE 24 MHz)
- **Adapter PC:** candle:0:0 — Candlelight 2.5 - Multiboard (SN: 2074326D554850142)
- **Pętla CAN1↔CAN2:** nie (firmware bez CAN_FD_TEST_LOOP)
- **Powtórzenia echo:** 10 (każde: wszystkie długości DLC × 6 wzorców × ID std/ext)
- **Czas testu:** 120 s

## Matryca prędkości

| Przypadek | Konfiguracja | Timing węzła (presc/TSEG1/TSEG2) | Echo PC OK/wysł. | Burst | OTA | Błędy (CEL) | Typy błędów arb. / dane | TDCV | Wynik |
|---|---|---|---|---|---|---|---|---|---|
| `classic_125k` | Classic 125k | N 4/251/36 | 1080/1080 | 100/100 | – | 0 | – / – | – | **PASS** |
| `classic_250k` | Classic 250k | N 2/251/36 | – | – | – | – | – | – | **FAIL** |
| `classic_500k` | Classic 500k | N 1/251/36 | 1080/1080 | 100/100 | – | 0 | – / – | – | **PASS** |
| `classic_1000k` | Classic 1000k | N 1/125/18 | 1079/1080 | 100/100 | – | 29 | STUFF 32 / – | – | **FAIL** |
| `fd_500k` | FD 500k | N 1/251/36 | 3000/3000 | 100/100 | OK | 187 | STUFF 2, FORM 150, BIT1 35 / – | – | **FAIL** |
| `fd_1000k` | FD 1000k | N 1/125/18 | 2967/3000 | 100/100 | OK | 647 | STUFF 255, FORM 203, BIT1 255, BIT0 255 / – | – | **FAIL** |
| `fd_brs_500k_1m` | FD+BRS 500k/1000k | N 4/62/9, D 4/26/9 | 2944/3000 | 99/100 | OK | 2386 | FORM 132, ACK 1, BIT0 255 / STUFF 167, FORM 255, BIT1 255 | 0 | **FAIL** |
| `fd_brs_500k_2m` | FD+BRS 500k/2000k | N 2/125/18, D 2/26/9, TDCO 52 | 2981/3000 | 100/100 | OK | 788 | BIT0 255 / STUFF 36, FORM 255, ACK 1, BIT1 255 | 66 | **FAIL** |
| `fd_brs_500k_4m` | FD+BRS 500k/4000k | N 1/251/36, D 1/26/9, TDCO 26 | 2997/3000 | 96/100 | OK | 401 | BIT0 128 / STUFF 7, FORM 255, ACK 1, BIT1 107 | 40 | **FAIL** |
| `fd_brs_500k_8m` | FD+BRS 500k/8000k | N 1/251/36, D 1/12/5, TDCO 12 | 3000/3000 | 100/100 | OK | 207 | – / FORM 166, BIT1 41 | 26 | **FAIL** |
| `fd_brs_1000k_2m` | FD+BRS 1000k/2000k | N 2/62/9, D 2/26/9, TDCO 52 | 2992/3000 | 100/100 | OK | 290 | – / STUFF 67, FORM 54, BIT1 96 | 66 | **FAIL** |
| `fd_brs_1000k_4m` | FD+BRS 1000k/4000k | N 1/125/18, D 1/26/9, TDCO 26 | 2993/3000 | 0/100 | BRAK | – | – | – | **FAIL** |
| `fd_brs_1000k_8m` | FD+BRS 1000k/8000k | N 1/125/18, D 1/12/5, TDCO 12 | – | – | – | – | – | – | **FAIL** |

## Testy negatywne i odporności

| Test | Wynik | Szczegóły |
|---|---|---|
| N0 return to Classic 500k | **FAIL** | no SET_CONFIG acknowledge |

## Uwagi do przypadków

- `classic_250k`: switch failed: no PING response in new configuration; recovered via fallback
- `classic_1000k`: PC traffic bus errors: 29, arb {'STUFF': 32}, data {}, TX rejected 0
- `fd_500k`: PC traffic bus errors: 187, arb {'STUFF': 2, 'FORM': 150, 'BIT1': 35}, data {}, TX rejected 0
- `fd_1000k`: echo mismatch id=0x1F2F410 fd=True brs=False len=3 -> fd=True brs=False len=3; PC traffic bus errors: 647, arb {'STUFF': 255, 'FORM': 203, 'BIT1': 255, 'BIT0': 255}, data {}, TX rejected 32
- `fd_brs_500k_1m`: PC traffic bus errors: 2386, arb {'FORM': 132, 'ACK': 1, 'BIT0': 255}, data {'STUFF': 167, 'FORM': 255, 'BIT1': 255}, TX rejected 12
- `fd_brs_500k_2m`: PC traffic bus errors: 788, arb {'BIT0': 255}, data {'STUFF': 36, 'FORM': 255, 'ACK': 1, 'BIT1': 255}, TX rejected 7
- `fd_brs_500k_4m`: PC traffic bus errors: 401, arb {'BIT0': 128}, data {'STUFF': 7, 'FORM': 255, 'ACK': 1, 'BIT1': 107}, TX rejected 1
- `fd_brs_500k_8m`: PC traffic bus errors: 207, arb {}, data {'FORM': 166, 'BIT1': 41}, TX rejected 0
- `fd_brs_1000k_2m`: echo mismatch id=0x1F2F410 fd=True brs=True len=64 -> fd=True brs=True len=64; echo mismatch id=0x410 fd=True brs=True len=64 -> fd=True brs=True len=64; PC traffic bus errors: 290, arb {}, data {'STUFF': 67, 'FORM': 54, 'BIT1': 96}, TX rejected 0
- `fd_brs_1000k_4m`: echo mismatch id=0x1F2F410 fd=True brs=True len=64 -> fd=True brs=True len=64; burst: no completion response; PC traffic bus errors: ?, arb None, data None, TX rejected ?
- `fd_brs_1000k_8m`: switch failed: no SET_CONFIG acknowledge

## Kryteria

- **Funkcjonalnie:** echo – każda ramka wraca z ID+1, tym samym formatem (Classic/FD), flagą BRS, długością i danymi (na magistrali FD wysyłane są też ramki Classic); burst – 100 ramek węzła (64 B FD / 8 B Classic) bez strat; dla FD działa OTA discovery (ramki Classic na magistrali FD).
- **PASS (bez błędów):** dodatkowo 0 błędów protokołu w węźle (licznik CEL – każdy wzrost TEC/REC, nie maleje, więc widać też błędy ukryte przez automatyczną retransmisję), brak bus-off, brak odrzuconych ramek TX; z pętlą – również 200/200 ramek CAN1↔CAN2 bez błędów.
- **Typy błędów:** liczone w przerwaniu PEA/PED (STUFF, FORM, ACK, BIT1, BIT0, CRC). TDCV: zmierzone położenie SSP (opóźnienie transceivera + TDCO) w okresach zegara FDCAN (6.94 ns).

## Log UART węzła (bez cyklicznych logów IO/MAIN)

```text
[CAN_FD_TEST] SET_CONFIG Classic nominal=125 kbps data=0 kbps
[CAN1] bus config: Classic nominal=125000 bps data=125000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG Classic nominal=250 kbps data=0 kbps
[CAN1] bus config: Classic nominal=250000 bps data=250000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] no test traffic in new configuration, falling back to Classic 500 kbps
[CAN1] bus config: Classic nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] SET_CONFIG Classic nominal=500 kbps data=0 kbps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG Classic nominal=1000 kbps data=0 kbps
[CAN1] bus config: Classic nominal=1000000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN_FD_TEST] SET_CONFIG FD nominal=500 kbps data=0 kbps
[CAN1] bus config: FD nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[OTA_MGR] [DISCOVERY] Responded on 0x01F29FE9 (CAN1) after 248 ms slot (UID: 46009900...)
[CAN_FD_TEST] SET_CONFIG FD nominal=1000 kbps data=0 kbps
[CAN1] bus config: FD nominal=1000000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN1] BUS-OFF detected (TEC=253), restarting controller (#1)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#2)
[OTA_MGR] [DISCOVERY] Responded on 0x01F29FE9 (CAN1) after 248 ms slot (UID: 46009900...)
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=1000 kbps
[CAN1] bus config: FD+BRS nominal=500000 bps data=1000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN1] BUS-OFF detected (TEC=249), restarting controller (#3)
[CAN1] BUS-OFF detected (TEC=252), restarting controller (#4)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#5)
[CAN1] BUS-OFF detected (TEC=251), restarting controller (#6)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#7)
[CAN1] BUS-OFF detected (TEC=252), restarting controller (#8)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#9)
[CAN1] BUS-OFF detected (TEC=251), restarting controller (#10)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#11)
[CAN1] BUS-OFF detected (TEC=252), restarting controller (#12)
[CAN1] BUS-OFF detected (TEC=249), restarting controller (#13)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#14)
[CAN1] BUS-OFF detected (TEC=252), restarting controller (#15)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#16)
[CAN1] BUS-OFF detected (TEC=252), restarting controller (#17)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#18)
[CAN1] BUS-OFF detected (TEC=252), restarting controller (#19)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#20)
[CAN1] BUS-OFF detected (TEC=250), restarting controller (#21)
[OTA_MGR] [DISCOVERY] Responded on 0x01F29FE9 (CAN1) after 248 ms slot (UID: 46009900...)
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=2000 kbps
[CAN1] bus config: FD+BRS nominal=500000 bps data=2000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#22)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#23)
[CAN1] BUS-OFF detected (TEC=255), restarting controller (#24)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#25)
[CAN1] BUS-OFF detected (TEC=255), restarting controller (#26)
[CAN1] BUS-OFF detected (TEC=255), restarting controller (#27)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#28)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#29)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#30)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#31)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#32)
[CAN1] BUS-OFF detected (TEC=250), restarting controller (#33)
[CAN1] BUS-OFF detected (TEC=248), restarting controller (#34)
[OTA_MGR] [DISCOVERY] Responded on 0x01F29FE9 (CAN1) after 248 ms slot (UID: 46009900...)
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=4000 kbps
[CAN1] bus config: FD+BRS nominal=500000 bps data=4000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[CAN1] BUS-OFF detected (TEC=250), restarting controller (#35)
[OTA_MGR] [DISCOVERY] Responded on 0x01F29FE9 (CAN1) after 248 ms slot (UID: 46009900...)
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=500 kbps data=8000 kbps
[CAN1] bus config: FD+BRS nominal=500000 bps data=8000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[OTA_MGR] [DISCOVERY] Responded on 0x01F29FE9 (CAN1) after 248 ms slot (UID: 46009900...)
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=1000 kbps data=2000 kbps
[CAN1] bus config: FD+BRS nominal=1000000 bps data=2000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[OTA_MGR] [DISCOVERY] Responded on 0x01F29FE9 (CAN1) after 248 ms slot (UID: 46009900...)
[CAN_FD_TEST] SET_CONFIG FD+BRS nominal=1000 kbps data=4000 kbps
[CAN1] bus config: FD+BRS nominal=1000000 bps data=4000000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
[CAN_FD_TEST] configuration confirmed by PC
[OTA_MGR] [DISCOVERY] Responded on 0x01F29FE9 (CAN1) after 248 ms slot (UID: 46009900...)
[CAN_FD_TEST] SET_CONFIG Classic nominal=500 kbps data=0 kbps
[CAN1] bus config: Classic nominal=500000 bps data=500000 bps
[CAN_PARSER] Active parser: ID 3 (5 filters)
[CAN_PARSER] Attached BOOT_OTA parser to CAN1 (total filters: 7)
```

## Jak powtórzyć test

Procedura opisana w [can_fd_test_guide.md](can_fd_test_guide.md).
