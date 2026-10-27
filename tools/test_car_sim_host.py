#!/usr/bin/env python3
"""
Test hostowy symulatorów aut: app/car_sim (koder sygnałów, dopasowanie komend z PIN-em, pilot,
harmonogram ramek, rejestr i mapa parserów urządzenia, profile aut, komendy UART SIM* / PILOT#
na zaślepce silnika).

Kompiluje tools/host/car_sim_cli.c (lokalny gcc albo obraz Dockera vbox-builder), przepuszcza przez
niego scenariusz krok po kroku (stan wejść i wybrany profil są zachowywane między krokami)
i porównuje wyjście każdego kroku. Profile TEST, SCHED13 i BAD_* są zdefiniowane w harnessie
(wszystkie kodowania, harmonogram 13 ramek, błędne profile), pozostałe pochodzą z rejestru.

Ramki symulatorów Arkana i V_BOX sprawdzają dekodery referencyjne (arkana_decode(), v_box_decode()):
te same ID, maski i wzory co parser urządzenia 196 i dotychczasowa telemetria v-box, wpisane na stałe
(test nie czyta źródeł parsera urządzenia). Dla serii stanów auta dekoder ma odtworzyć stan wejść.

Użycie:
    python tools/test_car_sim_host.py [--docker-image vbox-builder:latest] [-v]
Kod wyjścia 0 = wszystkie kroki zgodne.
"""

import argparse
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["tools/host/car_sim_cli.c", "app/car_sim/car_sim_codec.c", "app/car_sim/car_sim_registry.c",
           "app/car_sim/car_sim_cmd.c", "app/car_sim/sims/sim_none.c", "app/car_sim/sims/sim_renault_arkana.c",
           "app/car_sim/sims/sim_v_box.c", "app/io/io_sim.c"]
INCLUDES = ["app/car_sim", "app/car_sim/sims", "app/io", "app/can_bus", "app/port"]
CFLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-DIO_SIM_HOST", "-DCAR_SIM_HOST"]


def state(acc=0, door_drv=0, door_other=0, lights=0, locked=0, hazard=0, hood=0, handbrake=0, high=0, brake=0,
          rpm=0, rpm_in=0, speed=0, fuel=0, odo=10000000, temp=90, btn=0):
    """Oczekiwany stan auta wypisywany przez krok EFF."""
    return (f"acc={acc} door_drv={door_drv} door_other={door_other} lights={lights} locked={locked} "
            f"hazard={hazard} hood={hood} handbrake={handbrake} high={high} brake={brake} ss=0 rpm={rpm} "
            f"rpm_in={rpm_in} speed={speed} fuel={fuel} odo={odo} temp={temp} btn={btn:03X}")


# (krok, oczekiwane wyjście); "~" = wystarczy, że wyjście zawiera tekst; funkcja = dekoder referencyjny
SCENARIO = [
    ("ENCODE", "NO_SIM"),

    # --- Rejestr i mapa parserów urządzenia (D4: numer bez symulatora i 0 -> BRAK) ---
    ("CHECK", "OK"),
    ("MAP 192", "V_BOX"),                                       # D1: parser domyślny
    ("MAP 196", "ARKANA"),
    ("MAP 0", "BRAK"),
    ("MAP 197", "BRAK"),
    ("MAP 65535", "BRAK"),
    ("KEY brak", "BRAK"),
    ("KEY Brak", "BRAK"),
    ("KEY NOPE", "NOT_FOUND"),
    ("KEY v_box", "V_BOX"),
    ("KEY arkana", "ARKANA"),

    # --- BRAK: jedyna ramka 0x01F2F3FF ext, DLC 6, CA FE 00 BA BE 00 co 1000 ms ---
    ("SIM brak", 'SIM BRAK "brak symulatora" tx=1 rx=0 fob=0 rpm=750/3/750 vsig=0'),
    ("CHECKP", "OK"),
    ("ENCODE", "01F2F3FF#CAFE00BABE00"),
    ("EFF btn=1FF fuel=100 rpm=750", state(acc=1, door_drv=1, door_other=1, lights=1, locked=1, hazard=1, hood=1,
                                            handbrake=1, rpm=750, rpm_in=750, fuel=100, btn=0x1FF)),
    ("ENCODE", "01F2F3FF#CAFE00BABE00"),                       # stan auta bez wpływu
    ("FOB OTWORZ 1234", "NO_FOB"),
    ("RX 46F 32 12 34 18 04 84 07 30", "NO_MATCH"),
    ("SCHED 0", "01F2F3FF"),
    ("SCHED 100", "-"),
    ("SCHEDSTAT 10000", "frames=1 max_per_tick=1 counts=10"),

    # --- Stan auta: mapowanie SW0..SW8, D13 (bez stacyjki obroty 0, rpm_in bez zmian) ---
    ("SIM TEST", 'SIM TEST "harness test car" tx=4 rx=6 fob=1 rpm=8000/1/800 vsig=1'),
    ("CHECKP", "OK"),
    ("EFF btn=1A9 fuel=42 rpm=800", state(acc=1, lights=1, hazard=1, handbrake=1, rpm=800, rpm_in=800, fuel=42,
                                           btn=0x1A9)),
    ("EFF btn=006", state(door_drv=1, door_other=1, rpm_in=800, fuel=42, btn=0x006)),
    ("EFF btn=050", state(locked=1, hood=1, rpm_in=800, fuel=42, btn=0x050)),
    ("EFF btn=0A8", state(lights=1, hazard=1, handbrake=1, rpm_in=800, fuel=42, btn=0x0A8)),

    # --- Koder: BIT z bitami init, UINT BE / LE, mul / div / offset / max, shift, hook ---
    # 0x100: [0] 0x80 ACC, 0x01 SW3 (init 0x41); [1..2] BE RPM x8; [3..4] LE km/h x160 (max FEFF);
    #        [5] temp + 40; [6] paliwo / 2; [7] zamek 0x04 i 0x80
    # 0x438: [0..3] BE przebieg / 10 << 4, dolny półbajt [3] z init (0x0A); 0x206: [6..7] pedał x100, init [4] 0x04
    # 0x18FEF100 ext DLC 4: hook = SW0..SW7 w [0] i licznik w [3] (zerowany przy wyborze profilu)
    ("ENCODE", "100#4100000000821500 | 438#00F4240A00000000 | 206#0000000004000000 | 18FEF100#A8000000"),
    ("EFF btn=019 rpm=800 speed=250 temp=-40 odo=2147483647 brake=1",
     state(acc=1, lights=1, locked=1, brake=1, rpm=800, rpm_in=800, speed=250, fuel=42, odo=2147483647, temp=-40,
           btn=0x019)),
    ("ENCODE", "100#C11900409C001584 | 438#CCCCCCCA00000000 | 206#0000000004000064 | 18FEF100#19000001"),
    ("EFF speed=409 temp=150 fuel=100 btn=000 brake=0 odo=0",
     state(rpm_in=800, speed=409, fuel=100, odo=0, temp=150, btn=0)),
    ("ENCODE", "100#400000FFFEBE3200 | 438#0000000A00000000 | 206#0000000004000000 | 18FEF100#00000002"),
    ("SIM TEST", 'SIM TEST "harness test car" tx=4 rx=6 fob=1 rpm=8000/1/800 vsig=1'),
    ("ENCODE", "~18FEF100#00000000"),                          # on_select zeruje licznik hooka

    # --- Harmonogram: fazy rozłożone co 10 ms, ramka 1000 ms raz na sekundę ---
    ("SCHED 0", "100"),
    ("SCHED 10", "438"),
    ("SCHED 20", "206"),
    ("SCHED 30", "18FEF100"),
    ("SCHED 40", "-"),
    ("SCHED 100", "100"),
    ("SCHED 1030", "18FEF100"),
    ("SCHEDSTAT 1000", "frames=4 max_per_tick=1 counts=10,10,10,1"),

    # --- Komendy LX: dopasowanie bajtów z maską, potem PIN (D8: zły PIN = brak akcji) ---
    ("RX 46F 32 12 34 18 04 84 07 30", "MATCH act=UNLOCK pin=1234"),
    ("RX 46F 32 12 34 10 04 84 07 30", "MATCH act=LOCK pin=1234"),
    ("RX 46F 32 F1 5C 10 04 84 07 30", "MATCH act=NONE pin=F15C MISMATCH"),
    ("RX 46F 32 F1 5C 18 04 84 07 30", "MATCH act=NONE pin=F15C MISMATCH"),
    ("PIN F15C", "PIN"),                                        # SIM_PIN#F15C
    ("RX 46F 32 F1 5C 10 04 84 07 30", "MATCH act=LOCK pin=F15C"),
    ("RX 46F 32 12 34 10 04 84 07 30", "MATCH act=NONE pin=1234 MISMATCH"),
    ("PIN 1234", "PIN"),
    ("RX 46F 32 12 34 10 04 84 07", "NO_MATCH"),                # za krótka
    ("RX 46F 11 11 11 00 00 00 00 00", "NO_MATCH"),             # budzenie magistrali
    ("RX 46F 32 12 34 11 04 84 07 30", "NO_MATCH"),             # inny bajt komendy
    ("RX 46E 32 12 34 10 04 84 07 30", "NO_MATCH"),             # obce ID
    ("RX 0000046F 32 12 34 10 04 84 07 30", "NO_MATCH"),        # to samo ID jako extended
    # --- Komenda bez PIN-u (jak przekaźnik V_BOX 0x01F2F3F0: data[2] & 0x01, krótsza niż 3 B ignorowana) ---
    ("RX 01F2F3F0 00 00 01", "MATCH act=LOCK"),
    ("RX 01F2F3F0 FF FF FF", "MATCH act=LOCK"),
    ("RX 01F2F3F0 00 00 FE", "MATCH act=UNLOCK"),
    ("RX 01F2F3F0 00 00", "NO_MATCH"),
    ("RX 1F2 00 00 01", "NO_MATCH"),
    ("RX 123", "MATCH act=LOG"),
    ("RX 124 01", "MATCH act=LOCK"),                            # SIM_ACT_CUSTOM -> on_rx
    ("RX 124 02", "MATCH act=NONE"),

    # --- Pilot: ramka z PIN-em v-box (BE od pin_byte) ---
    ("FOB OTWORZ 1234", "46F#3212341804840730"),
    ("FOB ZAMKNIJ 1234", "46F#3212341004840730"),
    ("FOB ZAMKNIJ F15C", "46F#32F15C1004840730"),

    # --- Harmonogram 13 ramek 100 ms (Arkana): każda 10 razy na 1 s, w ticku max ceil(13 / 10) = 2 ---
    ("SIM SCHED13", 'SIM SCHED13 "13 frames" tx=13 rx=0 fob=0 rpm=8000/1/800 vsig=0'),
    ("SCHEDSTAT 1000", "frames=13 max_per_tick=2 counts=10,10,10,10,10,10,10,10,10,10,10,10,10"),
    ("SCHED 0", "200 | 20A"),
    ("SCHED 30", "203"),
    ("SCHED 90", "209"),

    # --- Kontrola profili (błędy wykrywane przy starcie) ---
    ("SIM BAD_SIG", "~SIM BAD_SIG"),
    ("CHECKP", "ERR | BAD_SIG: sygnał: pole poza ramką lub zła maska / długość (ID 0x300, pozycja 0)"),
    ("SIM BAD_DUP", "~SIM BAD_DUP"),
    ("CHECKP", "ERR | BAD_DUP: ramka TX: powtórzone ID (ID 0x300, pozycja 1)"),
    ("SIM BAD_PERIOD", "~SIM BAD_PERIOD"),
    ("CHECKP", "ERR | BAD_PERIOD: ramka TX: okres nie jest wielokrotnością 10 ms (ID 0x300, pozycja 0)"),
    ("SIM BAD_ID", "~SIM BAD_ID"),
    ("CHECKP", "ERR | BAD_ID: ramka TX: ID poza zakresem (ID 0x800, pozycja 0)"),
    ("SIM BAD_NOTX", "~SIM BAD_NOTX"),
    ("CHECKP", "~ERR | BAD_NOTX: liczba ramek TX poza zakresem 1..32"),
    ("SIM BAD_RPM", "~SIM BAD_RPM"),
    ("CHECKP", "~ERR | BAD_RPM: złe limity obrotów"),
    ("SIM BAD_RXPIN", "~SIM BAD_RXPIN"),
    ("CHECKP", "ERR | BAD_RXPIN: komenda RX: PIN poza ramką (ID 0x46F, pozycja 0)"),
    ("SIM BAD_BUS", "~SIM BAD_BUS"),
    ("CHECKP", "~ERR | BAD_BUS: brak konfiguracji magistrali"),
    ("SIM NOPE", "NOT_FOUND"),

    # --- V_BOX: telemetria 0x01F2F3F4 bajt w bajt jak io_manager_canCreateFrame() (T4, D1) ---
    ("SIM v_box", 'SIM V_BOX "telemetria v-box" tx=1 rx=2 fob=0 rpm=750/3/750 vsig=0'),
    ("CHECKP", "OK"),
    ("EFF btn=1A9 fuel=42 rpm=750", "~rpm=750 rpm_in=750"),     # scenariusz PHYS 1A9 42 z test_io_cmd_host
    ("ENCODE", "01F2F3F4#A92AFA0000000000"),                    # licznik BE w [4..7] od 0
    ("ENCODE", "01F2F3F4#A92AFA0000000001"),                    # +1 na ramkę
    ("EFF btn=1B9 rpm=300 fuel=10", "~rpm=300 rpm_in=300"),
    ("ENCODE", "01F2F3F4#B90A640000000002"),                    # 300 / 3 = 0x64
    ("EFF btn=1B8 rpm=750 fuel=42", "~acc=0 "),                 # D13 nie dotyczy V_BOX: data[2] z rpm_in
    ("EFF btn=1B8", "~rpm=0 rpm_in=750"),
    ("ENCODE", "01F2F3F4#B82AFA0000000003"),
    ("EFF btn=0FF rpm=0 fuel=0", "~rpm=0 rpm_in=0"),
    ("ENCODE", "01F2F3F4#FF00000000000004"),
    ("SIM V_BOX", 'SIM V_BOX "telemetria v-box" tx=1 rx=2 fob=0 rpm=750/3/750 vsig=0'),
    ("ENCODE", "01F2F3F4#FF00000000000000"),                    # licznik zerowany przy wyborze
    # Komenda przekaźnika 0x01F2F3F0 (logika cp_v_box: data[2] & 0x01, krótsza niż 3 B ignorowana)
    ("RX 01F2F3F0 00 00 01", "MATCH act=LOCK"),
    ("RX 01F2F3F0 00 00 00 00 00 00 00 00", "MATCH act=UNLOCK"),
    ("RX 01F2F3F0 AA 55 03 00", "MATCH act=LOCK"),
    ("RX 01F2F3F0 00 00", "NO_MATCH"),
    ("RX 01F2F3F0", "NO_MATCH"),
    ("RX 01F2F3F4 00 00 01", "NO_MATCH"),
    ("RX 1F2 00 00 01", "NO_MATCH"),
    ("FOB OTWORZ 1234", "NO_FOB"),
    ("SCHED 0", "01F2F3F4"),
    ("SCHED 50", "-"),
    ("SCHED 100", "01F2F3F4"),
    ("SCHEDSTAT 1000", "frames=1 max_per_tick=1 counts=10"),

    # --- Komendy UART (car_sim_cmd.c) na zaślepce silnika: start V_BOX, tryb AUTO, parser nieodczytany ---
    ("CMD SIM_HELP", "~[SIM] PILOT#<OTWORZ|ZAMKNIJ>   ramka pilota z PIN-em (auto z pilotem) i przekaznik zamka"),
    ("CMD sim?", '[SIM] SIM=V_BOX "telemetria v-box", tryb AUTO, parser urządzenia CAN1: nieodczytany'
                 " | [SIM] CAN1 500k, ramek TX: 1, komend LX: 2, pilot: nie | [SIM] PIN: - (symulator bez pilota)"
                 " | [SIM] liczniki: TX nadane 0, odrzucone 0; komendy LX odebrane 0, odrzucone przez PIN 0"),
    ("CMD SIM_LIST", "~[SIM] +----------+----------------------+-----------+----------+----+-------+"
                     " | [SIM] | Klucz    | Nazwa                | Parsery   | CAN1     | TX | Pilot |"),
    ("CMD SIM_LIST", "~[SIM] |*V_BOX    | telemetria v-box     | 192       | 500k     |  1 | nie   |"),
    ("CMD SIM_LIST", "~[SIM] | BRAK     | brak symulatora      | 0 + inne  | 500k     |  1 | nie   |"),
    ("CMD SIM_LIST", "~[SIM] * = aktywny, tryb AUTO"),
    # Wybór ręczny: klucz bez rozróżniania wielkości liter, spacje przy '#', numer parsera urządzenia
    ("CMD SIM#BRAK", "[SIM] OK SIM=BRAK, tryb RECZNY >> select BRAK"),
    ("CMD SIM?", '~[SIM] SIM=BRAK "brak symulatora", tryb RECZNY'),
    ("CMD sim # v_box", "[SIM] OK SIM=V_BOX, tryb RECZNY >> select V_BOX"),
    ("CMD SIM#0", "[SIM] OK SIM=BRAK, tryb RECZNY >> select BRAK"),                 # D4: 0 = BRAK
    ("CMD SIM#192", "[SIM] OK SIM=V_BOX, tryb RECZNY >> select V_BOX"),
    ("CMD SIM_LIST", "~[SIM] * = aktywny, tryb RECZNY"),
    ("CMD SIM#197", "[SIM] ERR: brak symulatora dla parsera urządzenia 197 (SIM_LIST)"),
    ("CMD SIM#65536", "[SIM] ERR: numer parsera urządzenia poza zakresem 0..65535"),
    ("CMD SIM#NOPE", "[SIM] ERR: nieznany symulator NOPE (SIM_LIST)"),
    ("CMD SIM#", "[SIM] ERR: składnia: SIM#<KLUCZ|numer parsera urządzenia> albo SIM#AUTO (SIM_LIST)"),
    ("CMD SIM", "[SIM] ERR: składnia: SIM#<KLUCZ|numer parsera urządzenia> albo SIM#AUTO"),
    ("CMD SIM#BARDZO_DLUGI_KLUCZ", "[SIM] ERR: składnia: SIM#<KLUCZ|numer parsera urządzenia> albo SIM#AUTO"),
    # SIM#AUTO: natychmiast ostatni numer parsera urządzenia
    ("CMD SIM#AUTO", "[SIM] OK tryb AUTO: parser urządzenia CAN1 jeszcze nieodczytany, symulator bez zmian"
                     " >> mode AUTO"),
    ("ENGINE dev=197", "ENGINE"),
    ("CMD SIM#auto", "[SIM] OK tryb AUTO: parser urządzenia 197 -> SIM=BRAK >> mode AUTO"),
    ("CMD SIM?", '~[SIM] SIM=BRAK "brak symulatora", tryb AUTO, parser urządzenia CAN1: 197'),
    ("ENGINE dev=192", "ENGINE"),
    ("CMD SIM#AUTO", "[SIM] OK tryb AUTO: parser urządzenia 192 -> SIM=V_BOX >> mode AUTO"),
    # PIN v-box w RAM (D8)
    ("CMD SIM_PIN#f15c", "[SIM] OK SIM_PIN=F15C >> pin F15C"),
    ("CMD SIM?", "~[SIM] PIN: F15C (SIM_PIN#, symulator bez pilota)"),
    ("CMD SIM_PIN#12345", "[SIM] ERR: składnia: SIM_PIN#<4 cyfry hex> albo SIM_PIN#DOMYSLNY"),
    ("CMD SIM_PIN#12G4", "[SIM] ERR: składnia: SIM_PIN#<4 cyfry hex> albo SIM_PIN#DOMYSLNY"),
    ("CMD SIM_PIN#123", "[SIM] ERR: składnia: SIM_PIN#<4 cyfry hex> albo SIM_PIN#DOMYSLNY"),
    ("CMD SIM_PIN", "[SIM] ERR: składnia: SIM_PIN#<4 cyfry hex> albo SIM_PIN#DOMYSLNY"),
    ("CMD SIM_PIN#domyslny", "[SIM] OK SIM_PIN=DOMYSLNY (symulator bez pilota) >> pin reset"),
    ("CMD SIM?", "~[SIM] PIN: - (symulator bez pilota)"),
    # Pilot: bez pilota w profilu tylko przekaźnik
    ("CMD PILOT#OTWORZ", "[SIM] OK PILOT#OTWORZ: symulator V_BOX bez pilota, tylko przekaźnik >> fob OTWORZ"),
    ("CMD pilot # zamknij", "[SIM] OK PILOT#ZAMKNIJ: symulator V_BOX bez pilota, tylko przekaźnik >> fob ZAMKNIJ"),
    ("CMD PILOT#X", "[SIM] ERR: składnia: PILOT#<OTWORZ|ZAMKNIJ>"),
    ("CMD PILOT", "[SIM] ERR: składnia: PILOT#<OTWORZ|ZAMKNIJ>"),
    # Liczniki silnika
    ("ENGINE tx=1234 drop=2 rx=3 rej=1", "ENGINE"),
    ("CMD SIM?", "~[SIM] liczniki: TX nadane 1234, odrzucone 2; komendy LX odebrane 3, odrzucone przez PIN 1"),
    # Build CAN_FD_TEST: CAN1 należy do testu, wybór symulatora wyłączony
    ("ENGINE own=0", "ENGINE"),
    ("CMD SIM#BRAK", "[SIM] ERR: CAN1 zajęty przez test CAN FD, wybór symulatora wyłączony"),
    ("CMD SIM#AUTO", "[SIM] ERR: CAN1 zajęty przez test CAN FD, wybór symulatora wyłączony"),
    ("CMD SIM?", "~[SIM] CAN1 zajęty przez test CAN FD: nadawany tylko V_BOX, wybór symulatora wyłączony"),
    ("ENGINE own=1", "ENGINE"),
    # Linie, które nie są komendami symulatora (most traktuje je dalej, np. jako ramki CAN)
    ("CMD 1F2#01", "NOT_CMD"),
    ("CMD SIMX#1", "NOT_CMD"),
    ("CMD SIM_PIN?", "NOT_CMD"),
    ("CMD IO_HELP", "NOT_CMD"),

    # --- Renault Arkana (parser urządzenia 196): profil, komendy LX z PIN-em, pilot (T8, D8) ---
    ("SIM ARKANA", 'SIM ARKANA "Renault Arkana" tx=13 rx=2 fob=1 rpm=8000/1/800 vsig=1'),
    ("CHECKP", "OK"),
    ("RX 46F 32 12 34 18 04 84 07 30", "MATCH act=UNLOCK pin=1234"),   # cmd_open
    ("RX 46F 32 12 34 10 04 84 07 30", "MATCH act=LOCK pin=1234"),     # cmd_close
    ("RX 46F 32 F1 5C 18 04 84 07 30", "MATCH act=NONE pin=F15C MISMATCH"),
    ("RX 46F 32 F1 5C 10 04 84 07 30", "MATCH act=NONE pin=F15C MISMATCH"),
    ("PIN F15C", "PIN"),                                                # SIM_PIN#F15C
    ("RX 46F 32 F1 5C 18 04 84 07 30", "MATCH act=UNLOCK pin=F15C"),
    ("RX 46F 32 12 34 18 04 84 07 30", "MATCH act=NONE pin=1234 MISMATCH"),
    ("PIN 1234", "PIN"),
    ("RX 46F 11 11 11 00 00 00 00 00", "NO_MATCH"),                     # budzenie magistrali
    ("RX 46F 32 12 34 18 04 84 07", "NO_MATCH"),                        # za krótka
    ("RX 46F 33 12 34 18 04 84 07 30", "NO_MATCH"),
    ("RX 46F 32 12 34 18 04 84 07 31", "NO_MATCH"),
    ("RX 46F 32 12 34 11 04 84 07 30", "NO_MATCH"),
    ("RX 0000046F 32 12 34 18 04 84 07 30", "NO_MATCH"),                # to samo ID jako extended
    ("RX 01F2F3F0 00 00 01", "NO_MATCH"),                               # przekaźnik V_BOX nie dotyczy Arkany
    ("RX 418 00 00 00 50 00 00 00 00", "NO_MATCH"),                     # klakson: etap 2
    ("FOB OTWORZ 1234", "46F#3212341804840730"),
    ("FOB ZAMKNIJ 1234", "46F#3212341004840730"),
    ("FOB OTWORZ F15C", "46F#32F15C1804840730"),
    ("SCHEDSTAT 1000", "frames=13 max_per_tick=2 counts=10,10,10,10,10,10,10,10,10,10,10,10,10"),
    # Komendy UART z autem z pilotem
    ("ENGINE dev=196", "ENGINE"),
    ("CMD SIM#AUTO", "[SIM] OK tryb AUTO: parser urządzenia 196 -> SIM=ARKANA >> mode AUTO"),
    ("CMD SIM?", '[SIM] SIM=ARKANA "Renault Arkana", tryb AUTO, parser urządzenia CAN1: 196'
                 " | [SIM] CAN1 500k, ramek TX: 13, komend LX: 2, pilot: tak | [SIM] PIN: 1234 (domyślny)"
                 " | [SIM] liczniki: TX nadane 1234, odrzucone 2; komendy LX odebrane 3, odrzucone przez PIN 1"),
    ("CMD SIM_LIST", "~[SIM] |*ARKANA   | Renault Arkana       | 196       | 500k     | 13 | tak   |"),
    ("CMD SIM_LIST", "~[SIM] | V_BOX    | telemetria v-box     | 192       | 500k     |  1 | nie   |"),
    ("CMD PILOT#OTWORZ", "[SIM] OK PILOT#OTWORZ: ramka 0x46F z PIN-em 1234 i przekaźnik >> fob OTWORZ"),
    ("CMD SIM_PIN#F15C", "[SIM] OK SIM_PIN=F15C >> pin F15C"),
    ("CMD SIM?", "~[SIM] PIN: F15C (SIM_PIN#, nadpisany)"),
    ("CMD PILOT#ZAMKNIJ", "[SIM] OK PILOT#ZAMKNIJ: ramka 0x46F z PIN-em F15C i przekaźnik >> fob ZAMKNIJ"),
    ("CMD SIM_PIN#DOMYSLNY", "[SIM] OK SIM_PIN=DOMYSLNY (1234) >> pin reset"),
    ("CMD SIM#v_box", "[SIM] OK SIM=V_BOX, tryb RECZNY >> select V_BOX"),
    ("CMD SIM#196", "[SIM] OK SIM=ARKANA, tryb RECZNY >> select ARKANA"),
]


# --- Dekodery referencyjne ---------------------------------------------------------------------------

def parse_frames(line):
    """'ID#HEX | ID#HEX ...' -> {ID: bajty}."""
    frames = {}
    for item in line.split(" | "):
        ident, _, data = item.partition("#")
        frames[int(ident, 16)] = bytes.fromhex(data)
    return frames


def signed_char(v):
    """Rzutowanie na signed char jak w parserze urządzenia."""
    v &= 0xFF
    return v - 256 if v > 127 else v


ARKANA_IDS = {0x46C, 0x0A1, 0x41C, 0x46A, 0x5BB, 0x418, 0x46B, 0x47F, 0x47C, 0x206, 0x438, 0x417, 0x3AA}


def arkana_decode(fr):
    """Odczyt ramek Arkany tak jak w parserze urządzenia 196 (sekcje 3.1 i 3.3 planu)."""
    d46a, d5bb, d206, d438 = fr[0x46A], fr[0x5BB], fr[0x206], fr[0x438]
    out = {
        "acc": bool(fr[0x46C][6] & 0x80),
        "rpm": int(((fr[0x0A1][5] << 8) | fr[0x0A1][6]) * 0.125) & 0xFFFF,
        "speed": (int(((fr[0x41C][0] << 8) | fr[0x41C][1]) * 0.00625) & 0xFF) if fr[0x41C][0] != 0xFF else None,
        "door_drv": bool(d46a[1] & 0x04),
        "door_other": bool((d46a[1] & 0x01) or (d46a[2] & 0x10) or (d46a[2] & 0x01) or (d46a[1] & 0x40)),
        "high": bool(d46a[3] & 0x40),
        "both_lights_bits": bool(d5bb[4] & 0x08) and bool(d5bb[4] & 0x10),
        "locked": bool(fr[0x418][2] & 0x04) and bool(fr[0x46B][2] & 0x01) and bool(fr[0x46B][2] & 0x02)
                  and bool(fr[0x46B][7] & 0x80),
        "handbrake": bool(fr[0x47F][4] & 0x02),
        "fuel": fr[0x47F][3],
        "hood": bool(fr[0x47C][4] & 0x40),
        "brake": ((d206[6] << 8) | d206[7]) > 5,
        "esp_event": not (d206[4] & 0x04),
        "odo_m": None if d438[:4] == bytes([0xFF, 0xFF, 0xFF, 0xF0])
                 else ((d438[0] << 20) | (d438[1] << 12) | (d438[2] << 4) | (d438[3] >> 4)) * 10,
        "temp": signed_char(signed_char(fr[0x417][0]) - 40),
        "temp_data": signed_char(signed_char(fr[0x417][0]) - 40) & 0xFF,
        "warnings": bool(fr[0x3AA][2] & 0x10) or bool(fr[0x3AA][3] & 0x40) or bool(fr[0x3AA][2] & 0x02),
    }
    if out["both_lights_bits"]:
        out["low"] = False
    else:
        out["low"] = bool(d5bb[4] & 0x10)
    return out


def btn_bits(btn):
    """SW0..SW8 -> sygnały (bity jak w io_sim)."""
    return {name: bool(btn & (1 << bit)) for bit, name in enumerate(
        ("acc", "door_other", "door_drv", "lights", "locked", "hazard", "hood", "handbrake", "engine"))}


def expect_arkana(btn, fuel, rpm, speed, odo, high, brake, temp):
    """Sprawdzenie wyjścia ENCODE Arkany względem stanu wejść."""
    sw = btn_bits(btn)

    def check(line):
        fr = parse_frames(line)
        if set(fr) != ARKANA_IDS or any(len(d) != 8 for d in fr.values()):
            return f"ramki: {sorted(hex(i) for i in fr)}"
        dec = arkana_decode(fr)
        want = {
            "acc": sw["acc"], "rpm": rpm if sw["acc"] else 0, "speed": speed, "door_drv": sw["door_drv"],
            "door_other": sw["door_other"], "high": bool(high), "both_lights_bits": False, "low": sw["lights"],
            "locked": sw["locked"], "handbrake": sw["handbrake"], "fuel": fuel, "hood": sw["hood"],
            "brake": bool(brake), "esp_event": False, "odo_m": odo // 10 * 10, "temp_data": temp & 0xFF,
            "warnings": False,
        }
        if -40 <= temp <= 127:
            want["temp"] = temp
        bad = [f"{k}={dec[k]} (oczekiwane {v})" for k, v in want.items() if dec[k] != v]
        return "; ".join(bad) if bad else None

    return check


def expect_v_box(btn, fuel, rpm_in, counter):
    """Telemetria 0x01F2F3F4 jak dotychczas: SW0..SW7, paliwo, RPM / 3 (bez D13), licznik BE."""
    def check(line):
        fr = parse_frames(line)
        d = fr.get(0x01F2F3F4)
        if set(fr) != {0x01F2F3F4} or d is None or len(d) != 8:
            return f"ramki: {line}"
        want = bytes([btn & 0xFF, fuel, rpm_in // 3, 0]) + counter.to_bytes(4, "big")
        return None if d == want else f"{d.hex().upper()} != {want.hex().upper()}"

    return check


def expect_fob_pin(pin):
    """Nauka PIN-u z ramki pilota jak w parserze urządzenia: 0x46F, [3] == 0x18 -> PIN = [1..2]."""
    def check(line):
        fr = parse_frames(line)
        d = fr.get(0x46F)
        if d is None or d[3] != 0x18:
            return f"brak ramki nauki PIN-u: {line}"
        learned = (d[1] << 8) | d[2]
        return None if learned == pin else f"PIN {learned:04X} != {pin:04X}"

    return check


def eff_step(btn, fuel, rpm, speed=0, odo=10000000, high=0, brake=0, temp=90):
    return f"EFF btn={btn:03X} fuel={fuel} rpm={rpm} speed={speed} odo={odo} high={high} brake={brake} temp={temp}"


# (btn, paliwo, obroty, prędkość, przebieg [m], drogowe, pedał hamulca, temperatura)
ARKANA_STATES = [
    (0x000, 0, 0, 0, 0, 0, 0, -40),                 # wszystko wyłączone, minima
    (0x001, 0, 0, 0, 10000000, 0, 0, 90),           # stacyjka
    (0x002, 0, 0, 0, 10000000, 0, 0, 90),           # drzwi inne
    (0x004, 0, 0, 0, 10000000, 0, 0, 90),           # drzwi kierowcy
    (0x008, 0, 0, 0, 10000000, 0, 0, 90),           # mijania (SW3)
    (0x010, 0, 0, 0, 10000000, 0, 0, 90),           # zamek zamknięty (SW4)
    (0x020, 0, 0, 0, 10000000, 0, 0, 90),           # awaryjne: parser 196 ich nie czyta
    (0x040, 0, 0, 0, 10000000, 0, 0, 90),           # maska
    (0x080, 0, 0, 0, 10000000, 0, 0, 90),           # ręczny
    (0x100, 0, 800, 0, 10000000, 0, 0, 90),         # D13: SW8 bez stacyjki -> 0 obr/min
    (0x101, 0, 800, 0, 10000000, 0, 0, 90),         # bieg jałowy
    (0x101, 0, 1, 0, 10000000, 0, 0, 90),
    (0x101, 0, 8000, 0, 10000000, 0, 0, 90),        # maksimum obrotów
    (0x101, 0, 3333, 1, 10000000, 0, 0, 90),
    (0x101, 42, 2500, 123, 10000000, 0, 0, 90),
    (0x101, 100, 2500, 250, 10000000, 0, 0, 90),    # maksimum prędkości i paliwa
    (0x000, 0, 0, 0, 10000000, 1, 0, 90),           # drogowe
    (0x000, 0, 0, 0, 10000000, 0, 1, 90),           # pedał hamulca
    (0x000, 0, 0, 0, 12345, 0, 0, 90),              # przebieg w 10 m, w dół
    (0x000, 0, 0, 0, 999999000, 0, 0, 90),          # maksimum przebiegu (999999 km)
    (0x000, 0, 0, 0, 10000000, 0, 0, 0),
    (0x000, 0, 0, 0, 10000000, 0, 0, 127),
    (0x000, 0, 0, 0, 10000000, 0, 0, 150),          # > 127: parser czyta ujemną (patrz raport)
    (0x0DF, 77, 0, 0, 10000000, 1, 1, 105),         # wszystko bez SW8 i SW5
    (0x1FF, 100, 8000, 250, 999999000, 1, 1, 150),  # wszystko naraz
    (0x1E9, 15, 900, 0, 5000, 0, 0, 20),            # zamknięte auto po jeździe: carsharing "otwórz"
    (0x080, 15, 0, 0, 5000, 0, 0, 20),              # warunki "zamknij": stacyjka off, ręczny, drzwi, światła
]


def reference_steps():
    """Kroki z dekoderami referencyjnymi (T9)."""
    steps = [("SIM ARKANA", 'SIM ARKANA "Renault Arkana" tx=13 rx=2 fob=1 rpm=8000/1/800 vsig=1')]
    for st in ARKANA_STATES:
        steps.append((eff_step(*st), "~btn="))
        steps.append(("ENCODE", expect_arkana(*st)))
    steps += [
        ("FOB OTWORZ 1234", expect_fob_pin(0x1234)),
        ("FOB OTWORZ F15C", expect_fob_pin(0xF15C)),
        ("SIM V_BOX", 'SIM V_BOX "telemetria v-box" tx=1 rx=2 fob=0 rpm=750/3/750 vsig=0'),
    ]
    for counter, (btn, fuel, rpm) in enumerate([(0x1A9, 42, 750), (0x1B9, 10, 300), (0x1B8, 42, 750),
                                                (0x000, 0, 0), (0x1FF, 100, 750), (0x0FF, 100, 3)]):
        steps.append((eff_step(btn, fuel, rpm), "~btn="))
        steps.append(("ENCODE", expect_v_box(btn, fuel, rpm, counter)))
    return steps


def gcc_command(root, output):
    cmd = ["gcc", *CFLAGS]
    cmd += [f"-I{root}/{inc}" for inc in INCLUDES]
    cmd += [f"{root}/{src}" for src in SOURCES]
    return cmd + ["-o", output]


def run_harness(stdin_text, docker_image):
    """Kompiluje i uruchamia CLI. Zwraca listę linii wyjścia."""
    if shutil.which("gcc") and not docker_image:
        exe = os.path.join(REPO, "build", "car_sim_cli")
        subprocess.run(gcc_command(REPO, exe), check=True)
        out = subprocess.run([exe], input=stdin_text.encode("utf-8"), capture_output=True, check=True)
    else:
        image = docker_image or "vbox-builder:latest"
        script = " ".join(gcc_command("/src", "/tmp/cli")) + " && /tmp/cli"
        out = subprocess.run(["docker", "run", "--rm", "-i", "-v", f"{REPO}:/src:ro", image, "sh", "-c", script],
                             input=stdin_text.encode("utf-8"), capture_output=True)
        if out.returncode != 0:
            sys.stderr.write(out.stderr.decode("utf-8", errors="replace"))
            raise SystemExit("kompilacja / uruchomienie harnessu nie powiodło się")
    return out.stdout.decode("utf-8").splitlines()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--docker-image", default=None, help="wymusza kompilację w Dockerze (domyślnie gdy brak gcc)")
    ap.add_argument("-v", "--verbose", action="store_true", help="wypisuje także kroki zgodne")
    args = ap.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    scenario = SCENARIO + reference_steps()
    lines = run_harness("".join(step + "\n" for step, _ in scenario), args.docker_image)
    if len(lines) != len(scenario):
        raise SystemExit(f"harness zwrócił {len(lines)} linii, oczekiwano {len(scenario)}")

    failed = 0
    for i, ((step, expected), line) in enumerate(zip(scenario, lines), 1):
        if callable(expected):
            error = expected(line)
            ok = error is None
            expected = f"dekoder referencyjny: {error}"
        else:
            ok = (expected[1:] in line) if expected.startswith("~") else (line == expected)
        if not ok:
            failed += 1
        if not ok or args.verbose:
            print(f"[{'OK ' if ok else 'BŁĄD'}] {i:2}: {step[:70]}")
            if not ok:
                print(f"      oczekiwane: {expected}\n      jest:       {line}")

    print(f"\n{len(scenario) - failed}/{len(scenario)} kroków zgodnych")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
