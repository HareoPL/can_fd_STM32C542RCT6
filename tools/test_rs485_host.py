#!/usr/bin/env python3
"""
Test hostowy kodeka ramek RS485 i superStatus: app/rs485/rs485_frame.c + app/rs485/superstatus.c.

Kompiluje tools/host/rs485_cli.c (lokalny gcc albo obraz Dockera vbox-builder), przepuszcza przez
niego scenariusz krok po kroku (stan dekodera strumieniowego jest zachowywany między krokami)
i porównuje wyjście każdego kroku. Wektory pochodzą z podsłuchu magistrali RS485 urządzenia LX
(COM20, 57600 8N1, 2026-10-26): odpowiedzi superStatus, ramki aktualizacji parsera urządzenia,
tekstowy log urządzenia, kolizje i ramki z utraconą synchronizacją.

Użycie:
    python tools/test_rs485_host.py [--docker-image vbox-builder:latest] [-v]
Kod wyjścia 0 = wszystkie kroki zgodne.
"""

import argparse
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["tools/host/rs485_cli.c", "app/rs485/rs485_frame.c", "app/rs485/superstatus.c"]
INCLUDES = ["app/rs485"]
CFLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1"]

# Zapytanie superStatus programu serwisowego (src 0x90, wersja 0x0F45, hasło "9999")
REQ = "AA 55 62 1D 0E 50 90 45 0F 01 39 39 39 39 00 00 00 00 00"

# Odpowiedź superStatus, parser urządzenia CAN1 = 260 (13:33)
SS_260 = ("AA 55 86 A9 98 90 50 4E 25 01 23 05 72 32 00 00 0A 05 00 27 03 00 00 00 00 0B 8D 50 00 45 32 36 30 30 32 00 "
          "38 39 34 38 30 32 32 32 32 31 31 32 30 33 36 31 39 37 35 00 00 00 00 45 7A 48 DC 48 36 32 99 A1 01 FF 01 00 "
          "FF FF FF FF FF FF FF FF 00 00 00 01 24 05 72 32 00 00 00 00 00 00 00 00 01 04 03 33 35 33 33 32 32 30 38 32 "
          "36 36 34 35 32 36 00 01 00 00 00 00 00 00 00 04 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
          "00 00 00 00 00 00 00 00 00 00 00 00 00")
SS_260_PAYLOAD = SS_260[15:]
SS_260_DECODED = ("SS fw=9550 time=2026-10-26 12:33:39 in=00 out=00 pwr=1290 temp=39 sat=11 hdop=80 gsm=45 op=26002 "
                  "sim=8948022221120361975 modem=1.4.3 imei=353322082664526 can1=260 can2=0")

# Odpowiedź superStatus, parser urządzenia CAN1 = 194 (13:09)
SS_194 = ("AA 55 A4 93 98 90 50 4E 25 01 A8 FF 71 32 00 00 0A 05 00 29 03 00 00 00 00 09 8D 64 00 45 32 36 30 30 32 00 "
          "38 39 34 38 30 32 32 32 32 31 31 32 30 33 36 31 39 37 35 00 00 00 00 05 7A 48 DC 48 36 32 99 A1 01 FF 01 00 "
          "FF FF FF FF FF FF FF FF 00 00 00 01 A8 FF 71 32 00 00 00 00 00 00 00 00 01 04 03 33 35 33 33 32 32 30 38 32 "
          "36 36 34 35 32 36 00 01 00 00 00 00 00 00 00 C2 00 00 00 00" + " 00" * 29)
SS_194_DECODED = ("SS fw=9550 time=2026-10-26 12:10:16 in=00 out=00 pwr=1290 temp=41 sat=9 hdop=100 gsm=45 op=26002 "
                  "sim=8948022221120361975 modem=1.4.3 imei=353322082664526 can1=194 can2=0")

HEARTBEAT = "AA 55 3E DC 05 A1 D5 4E 25 01"
HEARTBEAT_F = "F A1 D5 4E 25 01"
WRONG_PWD = "AA 55 BA 3D 05 90 D5 0C 06 02"
CAN_INIT_LOG_TEXT = ("0D 0A 21 21 20 5B 45 4E 41 42 4C 45 44 20 43 41 4E 5F 4E 4F 52 4D 41 4C 5D 0D 0A 43 41 4E 31 20 49 "
                     "4E 49 54 20 4F 4B 20 5B 42 54 52 5F 35 30 30 4B 5D 0D 0A")
CAN_INIT_FRAME = "AA 55 37 25 15 90 16 43 41 4E 20 49 4E 49 54 20 43 4F 4D 50 4C 45 54 45 2E 00"
CAN_INIT_F = "F 90 16 43 41 4E 20 49 4E 49 54 20 43 4F 4D 50 4C 45 54 45 2E 00"
UPDATE_START_194 = ("AA 55 39 FA 21 26 90 00 00 20 39 39 39 39 00 00 00 00 00 00 00 00 00 16 00 00 31 39 34 2E 43 41 4E "
                    "00 00 00 00 00")
# Blok zapisu parsera urządzenia kończący się bajtami AA 55 w danych
UPDATE_WRITE_PART1 = ("AA 55 D2 4A 86 24 90 00 C1 03 08 E0 07 00 00 10 1F D1 03 08 01 99 55 6F 04 00 00 10 21 D0 03 08 "
                      "01 55 55" + " 00" * 9 + (" 01 55 55" + " 00" * 9) * 6 + " 01 00 00 01 00 00 00 02 00 00 00 03")
UPDATE_WRITE_PART2 = "00 00 00 91 C1 03 08 FF 01 AA 55"
UPDATE_ACK = "AA 55 79 A7 0C 90 26 4E 25 04 08 00 00 00 C1 03 08"


def payload_of(frame_hex):
    return "F " + frame_hex[15:]


# (krok, oczekiwane wyjście)
SCENARIO = [
    # --- Zapytanie superStatus: bajty identyczne z programem serwisowym ---
    ("REQ 90 0F45 9999", REQ),
    ("REQ 90 0F45 123456789", "~AA 55"),
    ("REQ 90 0F45 1234567890", "ERR"),
    ("FEED " + REQ, payload_of(REQ) + " | SS REQUEST"),

    # --- Odpowiedzi superStatus z podsłuchu ---
    ("FEED " + SS_260, "F " + SS_260_PAYLOAD + " | " + SS_260_DECODED),
    ("FEED " + SS_194, payload_of(SS_194) + " | " + SS_194_DECODED),
    ("FEED " + WRONG_PWD, "F 90 D5 0C 06 02 | SS WRONG_PASSWORD"),

    # --- Ramka podzielona na kawałki ---
    ("FEED " + SS_260[:200], "-"),
    ("FEED " + SS_260[200:], "F " + SS_260_PAYLOAD + " | " + SS_260_DECODED),

    # --- Dwie ramki sklejone (37 i 50 w jednym odczycie) ---
    ("FEED AA 55 04 BA 0E 37 90 45 0F 01 39 39 39 39 00 00 00 00 00 " + REQ,
     "F 37 90 45 0F 01 39 39 39 39 00 00 00 00 00 | " + payload_of(REQ) + " | SS REQUEST"),

    # --- Tekstowy log urządzenia przed ramką ---
    ("FEED " + CAN_INIT_LOG_TEXT + " " + CAN_INIT_FRAME, CAN_INIT_F),

    # --- Kolizja: utracone AA 55 (12:58:37.800), potem poprawna ramka ---
    ("FEED 43 41 4E 31 20 49 4E C9 B5 D5 3B 86 24 90 00 DB 03 08 0C 01 B0 F8 B5 01 00 12 8D F8", "-"),
    ("FEED " + HEARTBEAT, HEARTBEAT_F),
    # --- Utracone samo AA (12:58:39.568) ---
    ("FEED 01 21 51 48 00 68 FE F7 55 37 25 15 90 16 43 41 4E 20 49 4E 49 54 20 43 4F 4D 50 4C 45 54 45 2E 00", "-"),
    ("FEED " + HEARTBEAT, HEARTBEAT_F),

    # --- Fałszywa synchronizacja z dużym LEN połyka prawdziwą ramkę: odzysk po ciszy na magistrali ---
    ("FEED AA 55 12 34 F0 " + HEARTBEAT, "-"),
    ("IDLE", HEARTBEAT_F),
    ("STATS", "~pending=0"),

    # --- Błąd CRC: ramka odrzucona, następna poprawna ---
    ("FEED AA 55 3E DD 05 A1 D5 4E 25 01", "-"),
    ("FEED " + HEARTBEAT, HEARTBEAT_F),

    # --- Ramki aktualizacji parsera urządzenia (AA 55 wewnątrz danych bloku) ---
    ("FEED " + UPDATE_START_194, payload_of(UPDATE_START_194)),
    ("FEED " + UPDATE_WRITE_PART1, "-"),
    ("FEED " + UPDATE_WRITE_PART2, "F " + (UPDATE_WRITE_PART1 + " " + UPDATE_WRITE_PART2)[15:]),
    ("FEED " + UPDATE_ACK, payload_of(UPDATE_ACK)),

    # --- Samotne AA na końcu odczytu zostaje do następnego odczytu ---
    ("FEED 0D 0A AA", "-"),
    ("FEED " + HEARTBEAT[3:], HEARTBEAT_F),
    ("IDLE", "-"),
    ("STATS", "~pending=0"),

    # --- Czas urządzenia: sekundy od 2000-01-01 UTC ---
    ("TIME 0", "2000-01-01 00:00:00"),
    ("TIME 5097600", "2000-02-29 00:00:00"),
    ("TIME 762566399", "2024-02-29 23:59:59"),
    ("TIME 846333357", "2026-10-26 12:35:57"),
    ("TIME 4294967295", "2136-02-07 06:28:15"),
]


def gcc_command(root, output):
    cmd = ["gcc", *CFLAGS]
    cmd += [f"-I{root}/{inc}" for inc in INCLUDES]
    cmd += [f"{root}/{src}" for src in SOURCES]
    return cmd + ["-o", output]


def run_harness(stdin_text, docker_image):
    """Kompiluje i uruchamia CLI. Zwraca listę linii wyjścia."""
    if shutil.which("gcc") and not docker_image:
        exe = os.path.join(REPO, "build", "rs485_cli")
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

    lines = run_harness("".join(step + "\n" for step, _ in SCENARIO), args.docker_image)
    if len(lines) != len(SCENARIO):
        raise SystemExit(f"harness zwrócił {len(lines)} linii, oczekiwano {len(SCENARIO)}")

    failed = 0
    for i, ((step, expected), line) in enumerate(zip(SCENARIO, lines), 1):
        ok = (expected[1:] in line) if expected.startswith("~") else (line == expected)
        if not ok:
            failed += 1
        if not ok or args.verbose:
            print(f"[{'OK ' if ok else 'BŁĄD'}] {i:2}: {step[:70]}")
            if not ok:
                print(f"      oczekiwane: {expected}\n      jest:       {line}")

    print(f"\n{len(SCENARIO) - failed}/{len(SCENARIO)} kroków zgodnych")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
