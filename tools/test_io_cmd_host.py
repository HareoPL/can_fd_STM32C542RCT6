#!/usr/bin/env python3
"""
Test hostowy komend symulacji wejść: app/io/io_cmd.c + app/io/io_sim.c.

Kompiluje tools/host/io_cmd_cli.c (lokalny gcc albo obraz Dockera vbox-builder), przepuszcza
przez niego scenariusz krok po kroku (stan jest zachowywany między krokami) i sprawdza
odpowiedź każdej komendy oraz bajty data[0..2] ramki telemetrii 0x01F2F3F4.

Użycie:
    python tools/test_io_cmd_host.py [--docker-image vbox-builder:latest] [-v]
Kod wyjścia 0 = wszystkie kroki zgodne.
"""

import argparse
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["tools/host/io_cmd_cli.c", "app/io/io_cmd.c", "app/io/io_sim.c"]
INCLUDES = ["app/io"]
CFLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-DIO_SIM_HOST"]

SRC_IO = "[IO] ERR: źródło IO, najpierw ZRODLO#UART"

# (komenda, oczekiwana odpowiedź, oczekiwane data[0] data[1] data[2])
# Odpowiedź zaczynająca się od "~" = wystarczy, że wyjście zawiera ten tekst.
SCENARIO = [
    # --- Źródło IO (stan po resecie): ramka = wejścia fizyczne ---
    ("PHYS 1A9 42", "PHYS", "A9 2A FA"),
    ("ZRODLO?", "[IO] ZRODLO=IO STACYJKA=1 DRZWI_INNE=0 DRZWI_KIER=0 SWIATLA=1 ZAMKI=0(fiz) AWARYJNE=1 "
                "MASKA=0 HAMULEC=1 OBROTY=750 PALIWO=42", "A9 2A FA"),
    ("DRZWI_KIER#1", SRC_IO, "A9 2A FA"),
    ("OBROTY#300", SRC_IO, "A9 2A FA"),
    ("PALIWO#10", SRC_IO, "A9 2A FA"),
    ("OBROTY#751", "[IO] ERR: OBROTY poza zakresem 0..750", "A9 2A FA"),
    ("PHYS 000 7", "PHYS", "00 07 00"),
    ("PHYS 1A9 42", "PHYS", "A9 2A FA"),

    # --- Przejęcie przez UART: snapshot, ramka się nie zmienia, fizyczne wejścia ignorowane ---
    ("zrodlo#uart", "[IO] OK ZRODLO=UART", "A9 2A FA"),
    ("PHYS 000 0", "PHYS", "A9 2A FA"),
    ("PHYS 010 0", "PHYS", "B9 2A FA"),                       # SW4 (zamki) zawsze fizyczne
    ("DRZWI_KIER#1", "[IO] OK DRZWI_KIER=1", "BD 2A FA"),
    ("drzwi_kier # 0", "[IO] OK DRZWI_KIER=0", "B9 2A FA"),
    ("ZAMKI#0", "[IO] ERR: ZAMKI = sprzężenie przekaźnika, tylko odczyt", "B9 2A FA"),
    ("ZAMKI#1", "[IO] ERR: ZAMKI = sprzężenie przekaźnika, tylko odczyt", "B9 2A FA"),
    ("MASKA#2", "[IO] ERR: MASKA: wartość musi być 0 lub 1", "B9 2A FA"),
    ("MASKA#x", "[IO] ERR: składnia: MASKA#<0|1>", "B9 2A FA"),
    ("MASKA", "[IO] ERR: składnia: MASKA#<0|1>", "B9 2A FA"),
    ("MASKA#", "[IO] ERR: składnia: MASKA#<0|1>", "B9 2A FA"),

    # --- Obroty: 0..750, tylko wielokrotność 3, w ramce RPM/3 ---
    ("OBROTY#100", "[IO] ERR: OBROTY musi być wielokrotnością 3 (np. 99 lub 102)", "B9 2A FA"),
    ("OBROTY#1", "[IO] ERR: OBROTY musi być wielokrotnością 3 (np. 0 lub 3)", "B9 2A FA"),
    ("OBROTY#749", "[IO] ERR: OBROTY musi być wielokrotnością 3 (np. 747 lub 750)", "B9 2A FA"),
    ("OBROTY#751", "[IO] ERR: OBROTY poza zakresem 0..750", "B9 2A FA"),
    ("OBROTY#753", "[IO] ERR: OBROTY poza zakresem 0..750", "B9 2A FA"),
    ("OBROTY#100000", "[IO] ERR: OBROTY poza zakresem 0..750", "B9 2A FA"),
    ("OBROTY#-3", "[IO] ERR: składnia: OBROTY#<0..750>", "B9 2A FA"),
    ("OBROTY#3.0", "[IO] ERR: składnia: OBROTY#<0..750>", "B9 2A FA"),
    ("OBROTY#99", "[IO] OK OBROTY=99 RPM (data[2]=0x21)", "B9 2A 21"),
    ("OBROTY#0", "[IO] OK OBROTY=0 RPM (data[2]=0x00)", "B9 2A 00"),
    ("OBROTY#3", "[IO] OK OBROTY=3 RPM (data[2]=0x01)", "B9 2A 01"),
    ("OBROTY#750", "[IO] OK OBROTY=750 RPM (data[2]=0xFA)", "B9 2A FA"),

    # --- Paliwo ---
    ("PALIWO#101", "[IO] ERR: PALIWO poza zakresem 0..100", "B9 2A FA"),
    ("PALIWO#abc", "[IO] ERR: składnia: PALIWO#<0..100>", "B9 2A FA"),
    ("PALIWO#100", "[IO] OK PALIWO=100%", "B9 64 FA"),
    ("PALIWO#0", "[IO] OK PALIWO=0%", "B9 00 FA"),
    ("ZRODLO?", "[IO] ZRODLO=UART STACYJKA=1 DRZWI_INNE=0 DRZWI_KIER=0 SWIATLA=1 ZAMKI=1(fiz) AWARYJNE=1 "
                "MASKA=0 HAMULEC=1 OBROTY=750 PALIWO=0", "B9 00 FA"),

    # --- Ponowne ZRODLO#UART nie robi nowego snapshotu, powrót do IO = wejścia fizyczne ---
    ("ZRODLO#UART", "[IO] OK ZRODLO=UART", "B9 00 FA"),
    ("ZRODLO#XYZ", "[IO] ERR: składnia: ZRODLO#<UART|IO>", "B9 00 FA"),
    ("ZRODLO", "[IO] ERR: składnia: ZRODLO#<UART|IO>", "B9 00 FA"),
    ("ZRODLO#IO", "[IO] OK ZRODLO=IO", "10 00 00"),
    ("ZRODLO#UART", "[IO] OK ZRODLO=UART", "10 00 00"),

    # --- Wszystkie przełączniki -> właściwe bity data[0] ---
    ("STACYJKA#1", "[IO] OK STACYJKA=1", "11 00 00"),
    ("DRZWI_INNE#1", "[IO] OK DRZWI_INNE=1", "13 00 00"),
    ("SWIATLA#01", "[IO] OK SWIATLA=1", "1B 00 00"),
    ("MASKA#1", "[IO] OK MASKA=1", "5B 00 00"),
    ("AWARYJNE#1", "[IO] OK AWARYJNE=1", "7B 00 00"),
    ("HAMULEC#1", "[IO] OK HAMULEC=1", "FB 00 00"),
    ("Drzwi_Kier#1", "[IO] OK DRZWI_KIER=1", "FF 00 00"),
    ("PHYS 000 0", "PHYS", "EF 00 00"),
    ("STACYJKA#0", "[IO] OK STACYJKA=0", "EE 00 00"),

    # --- Linie, które nie są komendami IO, trafiają nietknięte do parsera ramek CAN ---
    ("ACC#01", "NOT_IO ACC#01", "EE 00 00"),
    ("CAN1:123#01 02", "NOT_IO CAN1:123#01 02", "EE 00 00"),
    ("123 #01", "NOT_IO 123 #01", "EE 00 00"),
    ("PALIWO?", "NOT_IO PALIWO?", "EE 00 00"),
    ("HELP", "NOT_IO HELP", "EE 00 00"),
    ("TLV#321#0xA5#Hello", "NOT_IO TLV#321#0xA5#Hello", "EE 00 00"),

    # --- Pomoc ---
    ("io_help", "~OBROTY#<0..750>    RPM, tylko wielokrotność 3 (w ramce RPM/3, odbiornik mnoży x3)", "EE 00 00"),
    ("IO_HELP", "~Zamki (SW4) = sprzężenie przekaźnika, tylko odczyt.", "EE 00 00"),
]


def gcc_command(root, output):
    cmd = ["gcc", *CFLAGS]
    cmd += [f"-I{root}/{inc}" for inc in INCLUDES]
    cmd += [f"{root}/{src}" for src in SOURCES]
    return cmd + ["-o", output]


def run_harness(stdin_text, docker_image):
    """Kompiluje i uruchamia CLI. Zwraca listę linii wyjścia."""
    if shutil.which("gcc") and not docker_image:
        exe = os.path.join(REPO, "build", "io_cmd_cli")
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

    lines = run_harness("".join(cmd + "\n" for cmd, _, _ in SCENARIO), args.docker_image)
    if len(lines) != len(SCENARIO):
        raise SystemExit(f"harness zwrócił {len(lines)} linii, oczekiwano {len(SCENARIO)}")

    failed = 0
    for i, ((cmd, exp_resp, exp_frame), line) in enumerate(zip(SCENARIO, lines), 1):
        resp, _, frame = line.partition("\t")
        if exp_resp.startswith("~"):
            resp_ok = exp_resp[1:] in resp
        else:
            resp_ok = resp == exp_resp
        ok = resp_ok and frame == exp_frame
        if not ok:
            failed += 1
        if not ok or args.verbose:
            print(f"[{'OK ' if ok else 'BŁĄD'}] {i:2}: {cmd!r}")
            if not resp_ok:
                print(f"      odpowiedź oczekiwana: {exp_resp}\n      jest:                 {resp}")
            if frame != exp_frame:
                print(f"      ramka oczekiwana: {exp_frame}\n      jest:             {frame}")

    print(f"\n{len(SCENARIO) - failed}/{len(SCENARIO)} kroków zgodnych")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
