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

# ZRODLO? po przejęciu przez UART bez przełączników (SW4 = 1 fizycznie)
STATUS_UART = ("[IO] ZRODLO=UART STACYJKA=0 DRZWI_INNE=0 DRZWI_KIER=0 SWIATLA=0 ZAMKI=1(fiz) AWARYJNE=0 MASKA=0 "
               "HAMULEC=0 OBROTY={rpm} PALIWO={fuel} AWARIA_ZAMKA=0")
STATUS_IO = STATUS_UART.replace("ZRODLO=UART", "ZRODLO=IO")
# Druga linia ZRODLO? dla symulatora auta
VEHICLE = " | [IO] PREDKOSC={speed} PRZEBIEG={odo} DROGOWE={high} HAMULEC_NOZNY={brake} TEMP_SILNIKA={temp}"


def help_row(cmd, values, desc):
    """Oczekiwany wiersz tabeli IO_HELP (zawieranie)."""
    return f"~| {cmd:<18} | {values:<9} | {desc:<46} |"


# (komenda, oczekiwana odpowiedź, oczekiwane data[0] data[1] data[2])
# Odpowiedź zaczynająca się od "~" = wystarczy, że wyjście zawiera ten tekst, od "!" = wyjście go nie zawiera.
# "=HELP_TABLE" = tylko znaki ASCII, wiersze tabeli (| / +) równej długości, wszystkie komendy w tabeli.
# "=HELP_TABLE_CAR" = jak wyżej, a w tabeli także komendy sygnałów auta.
# Linie harnessu: LIMITS <max> <krok> <SW8> = limity obrotów symulatora, VSIG <0|1> = symulator używa
# sygnałów auta, ODO+ <m> = przyrost przebiegu z car_sim_task.
SCENARIO = [
    # --- Źródło IO (stan po resecie): ramka = wejścia fizyczne ---
    ("PHYS 1A9 42", "PHYS", "A9 2A FA"),
    ("ZRODLO?", "[IO] ZRODLO=IO STACYJKA=1 DRZWI_INNE=0 DRZWI_KIER=0 SWIATLA=1 ZAMKI=0(fiz) AWARYJNE=1 "
                "MASKA=0 HAMULEC=1 OBROTY=750 PALIWO=42 AWARIA_ZAMKA=0", "A9 2A FA"),
    ("DRZWI_KIER#1", SRC_IO, "A9 2A FA"),
    ("OBROTY#300", SRC_IO, "A9 2A FA"),
    ("PALIWO#10", SRC_IO, "A9 2A FA"),
    ("OBROTY#751", "[IO] ERR: OBROTY poza zakresem 0..750", "A9 2A FA"),
    ("AWARIA_ZAMKA#1", SRC_IO, "A9 2A FA"),
    ("IO_RESET", SRC_IO, "A9 2A FA"),
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
                "MASKA=0 HAMULEC=1 OBROTY=750 PALIWO=0 AWARIA_ZAMKA=0", "B9 00 FA"),

    # --- Uszkodzony zamek: tylko flaga, ramka telemetrii bez zmian ---
    ("AWARIA_ZAMKA#2", "[IO] ERR: AWARIA_ZAMKA: wartość musi być 0 lub 1", "B9 00 FA"),
    ("AWARIA_ZAMKA", "[IO] ERR: składnia: AWARIA_ZAMKA#<0|1>", "B9 00 FA"),
    ("awaria_zamka#1", "[IO] OK AWARIA_ZAMKA=1 (przekaźnik nie reaguje na ramki CAN)", "B9 00 FA"),
    ("ZRODLO?", "[IO] ZRODLO=UART STACYJKA=1 DRZWI_INNE=0 DRZWI_KIER=0 SWIATLA=1 ZAMKI=1(fiz) AWARYJNE=1 "
                "MASKA=0 HAMULEC=1 OBROTY=750 PALIWO=0 AWARIA_ZAMKA=1", "B9 00 FA"),
    ("AWARIA_ZAMKA#0", "[IO] OK AWARIA_ZAMKA=0", "B9 00 FA"),
    ("AWARIA_ZAMKA#1", "[IO] OK AWARIA_ZAMKA=1 (przekaźnik nie reaguje na ramki CAN)", "B9 00 FA"),

    # --- Ponowne ZRODLO#UART nie robi nowego snapshotu, powrót do IO = wejścia fizyczne ---
    ("ZRODLO#UART", "[IO] OK ZRODLO=UART", "B9 00 FA"),
    ("ZRODLO#XYZ", "[IO] ERR: składnia: ZRODLO#<UART|IO>", "B9 00 FA"),
    ("ZRODLO", "[IO] ERR: składnia: ZRODLO#<UART|IO>", "B9 00 FA"),
    ("ZRODLO#IO", "[IO] OK ZRODLO=IO", "10 00 00"),
    ("ZRODLO#UART", "[IO] OK ZRODLO=UART", "10 00 00"),
    ("ZRODLO?", "[IO] ZRODLO=UART STACYJKA=0 DRZWI_INNE=0 DRZWI_KIER=0 SWIATLA=0 ZAMKI=1(fiz) AWARYJNE=0 "
                "MASKA=0 HAMULEC=0 OBROTY=0 PALIWO=0 AWARIA_ZAMKA=0", "10 00 00"),   # ZRODLO#IO kasuje awarię

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
    ("io_help", "=HELP_TABLE", "EE 00 00"),
    ("IO_HELP", "~| OBROTY#<v>         | 0..750    | obroty silnika [RPM], wielokrotnosc 3          |", "EE 00 00"),
    ("IO_HELP", "~| AWARIA_ZAMKA#<v>   | 0 / 1     | 1 = uszkodzony zamek, przekaznik nie reaguje   |", "EE 00 00"),
    ("IO_HELP", "~OBROTY: w ramce data[2] = RPM/3, odbiornik mnozy x3.", "EE 00 00"),
    ("IO_HELP", "~ZAMKI (SW4) = sprzezenie przekaznika, tylko odczyt.", "EE 00 00"),

    # --- IO_RESET: wszystko na 0, źródło zostaje UART ---
    ("OBROTY#300", "[IO] OK OBROTY=300 RPM (data[2]=0x64)", "EE 00 64"),
    ("PALIWO#55", "[IO] OK PALIWO=55%", "EE 37 64"),
    ("AWARIA_ZAMKA#1", "[IO] OK AWARIA_ZAMKA=1 (przekaźnik nie reaguje na ramki CAN)", "EE 37 64"),
    ("IO_RESET", "[IO] OK IO_RESET: wejścia=0, OBROTY=0, PALIWO=0, AWARIA_ZAMKA=0", "00 00 00"),
    ("ZRODLO?", "[IO] ZRODLO=UART STACYJKA=0 DRZWI_INNE=0 DRZWI_KIER=0 SWIATLA=0 ZAMKI=0(fiz) AWARYJNE=0 "
                "MASKA=0 HAMULEC=0 OBROTY=0 PALIWO=0 AWARIA_ZAMKA=0", "00 00 00"),
    ("PHYS 010 9", "PHYS", "10 00 00"),                        # SW4 nadal fizyczne
    ("io_reset", "[IO] OK IO_RESET: wejścia=0, OBROTY=0, PALIWO=0, AWARIA_ZAMKA=0", "10 00 00"),
    ("ZRODLO#IO", "[IO] OK ZRODLO=IO", "10 09 00"),
    ("IO_RESET", SRC_IO, "10 09 00"),
] + [
    # === Limity obrotów symulatora auta i sygnały auta (T1); kroki powyżej bez zmian ===
    # --- V_BOX: nowe komendy działają, komunikaty ZRODLO? / IO_RESET bez zmian ---
    ("PREDKOSC#50", SRC_IO, "10 09 00"),
    ("DROGOWE#1", SRC_IO, "10 09 00"),
    ("ZRODLO#UART", "[IO] OK ZRODLO=UART", "10 09 00"),
    ("PREDKOSC#50", "[IO] OK PREDKOSC=50 km/h", "10 09 00"),
    ("ZRODLO?", STATUS_UART.format(rpm=0, fuel=9), "10 09 00"),

    # --- Auto (np. Arkana): 0..8000 co 1, SW8 = 800, ZRODLO? z sygnałami auta ---
    ("LIMITS 8000 1 800", "LIMITS", "10 09 00"),
    ("VSIG 1", "VSIG", "10 09 00"),
    ("ZRODLO?", STATUS_UART.format(rpm=0, fuel=9) + VEHICLE.format(speed=50, odo=10000, high=0, brake=0, temp=90),
     "10 09 00"),
    ("OBROTY#8001", "[IO] ERR: OBROTY poza zakresem 0..8000", "10 09 00"),
    ("OBROTY#-1", "[IO] ERR: składnia: OBROTY#<0..8000>", "10 09 00"),
    ("OBROTY#8000", "[IO] OK OBROTY=8000 RPM", "10 09 FF"),
    ("OBROTY#801", "[IO] OK OBROTY=801 RPM", "10 09 FF"),           # krok 1: brak błędu wielokrotności
    ("PREDKOSC#251", "[IO] ERR: PREDKOSC poza zakresem 0..250", "10 09 FF"),
    ("PREDKOSC#x", "[IO] ERR: składnia: PREDKOSC#<0..250>", "10 09 FF"),
    ("predkosc#250", "[IO] OK PREDKOSC=250 km/h", "10 09 FF"),
    ("PRZEBIEG#1000000", "[IO] ERR: PRZEBIEG poza zakresem 0..999999", "10 09 FF"),
    ("PRZEBIEG#", "[IO] ERR: składnia: PRZEBIEG#<0..999999>", "10 09 FF"),
    ("PRZEBIEG#123456", "[IO] OK PRZEBIEG=123456 km", "10 09 FF"),
    ("ODO+ 1500", "ODO+", "10 09 FF"),                                # car_sim_task: przebieg rośnie z prędkością
    ("DROGOWE#2", "[IO] ERR: DROGOWE: wartość musi być 0 lub 1", "10 09 FF"),
    ("DROGOWE", "[IO] ERR: składnia: DROGOWE#<0|1>", "10 09 FF"),
    ("drogowe#1", "[IO] OK DROGOWE=1", "10 09 FF"),
    ("HAMULEC_NOZNY#1", "[IO] OK HAMULEC_NOZNY=1", "10 09 FF"),
    ("TEMP_SILNIKA#-41", "[IO] ERR: TEMP_SILNIKA poza zakresem -40..150", "10 09 FF"),
    ("TEMP_SILNIKA#151", "[IO] ERR: TEMP_SILNIKA poza zakresem -40..150", "10 09 FF"),
    ("TEMP_SILNIKA#--5", "[IO] ERR: składnia: TEMP_SILNIKA#<-40..150>", "10 09 FF"),
    ("TEMP_SILNIKA#-", "[IO] ERR: składnia: TEMP_SILNIKA#<-40..150>", "10 09 FF"),
    ("TEMP_SILNIKA#-40", "[IO] OK TEMP_SILNIKA=-40 C", "10 09 FF"),
    ("TEMP_SILNIKA#105", "[IO] OK TEMP_SILNIKA=105 C", "10 09 FF"),
    ("ZRODLO?", STATUS_UART.format(rpm=801, fuel=9) + VEHICLE.format(speed=250, odo=123457, high=1, brake=1, temp=105),
     "10 09 FF"),
    ("ODO+ 4000000000", "ODO+", "10 09 FF"),                          # nasycenie na 999999 km
    ("ZRODLO?", "~PRZEBIEG=999999 ", "10 09 FF"),
    ("IO_HELP", "=HELP_TABLE_CAR", "10 09 FF"),
    ("IO_HELP", help_row("OBROTY#<v>", "0..8000", "obroty silnika [RPM]"), "10 09 FF"),
    ("IO_HELP", help_row("PREDKOSC#<v>", "0..250", "predkosc [km/h], przebieg rosnie z predkoscia"), "10 09 FF"),
    ("IO_HELP", help_row("TEMP_SILNIKA#<v>", "-40..150", "temperatura silnika [C], domyslnie 90"), "10 09 FF"),
    ("IO_HELP", "~OBROTY: SW8 = 800 RPM, przy STACYJKA=0 auto nadaje 0 RPM.", "10 09 FF"),
    ("IO_HELP", "!OBROTY: w ramce data[2]", "10 09 FF"),
    ("IO_RESET", "[IO] OK IO_RESET: wejścia=0, OBROTY=0, PALIWO=0, AWARIA_ZAMKA=0, PREDKOSC=0, PRZEBIEG=10000, "
                 "DROGOWE=0, HAMULEC_NOZNY=0, TEMP_SILNIKA=90", "10 00 00"),
    ("ZRODLO?", STATUS_UART.format(rpm=0, fuel=0) + VEHICLE.format(speed=0, odo=10000, high=0, brake=0, temp=90),
     "10 00 00"),

    # --- Auto, źródło IO: SW8 = obroty biegu jałowego z profilu, sygnały auta domyślne ---
    ("PREDKOSC#30", "[IO] OK PREDKOSC=30 km/h", "10 00 00"),
    ("ZRODLO#IO", "[IO] OK ZRODLO=IO", "10 09 00"),
    ("PHYS 110 9", "PHYS", "10 09 FF"),
    ("ZRODLO?", STATUS_IO.format(rpm=800, fuel=9) + VEHICLE.format(speed=0, odo=10000, high=0, brake=0, temp=90),
     "10 09 FF"),
    ("ODO+ 5000", "ODO+", "10 09 FF"),                                # IO: bez efektu
    ("ZRODLO?", "~PRZEBIEG=10000 ", "10 09 FF"),
    ("ZRODLO#UART", "[IO] OK ZRODLO=UART", "10 09 FF"),              # snapshot: SW8 -> 800, sygnały auta domyślne
    ("ZRODLO?", "~OBROTY=800 PALIWO=9 AWARIA_ZAMKA=0 | [IO] PREDKOSC=0 PRZEBIEG=10000 ", "10 09 FF"),
    ("OBROTY#749", "[IO] OK OBROTY=749 RPM", "10 09 F9"),

    # --- Powrót do V_BOX: obroty zaokrąglone w dół do kroku / przycięte, komunikaty V_BOX ---
    ("LIMITS 750 3 750", "LIMITS", "10 09 F9"),
    ("VSIG 0", "VSIG", "10 09 F9"),
    ("ZRODLO?", STATUS_UART.format(rpm=747, fuel=9), "10 09 F9"),
    ("LIMITS 8000 1 800", "LIMITS", "10 09 F9"),
    ("OBROTY#8000", "[IO] OK OBROTY=8000 RPM (data[2]=0xFF)", "10 09 FF"),
    ("LIMITS 750 3 750", "LIMITS", "10 09 FA"),
    ("ZRODLO?", STATUS_UART.format(rpm=750, fuel=9), "10 09 FA"),
    ("OBROTY#8000", "[IO] ERR: OBROTY poza zakresem 0..750", "10 09 FA"),
    ("OBROTY#100", "[IO] ERR: OBROTY musi być wielokrotnością 3 (np. 99 lub 102)", "10 09 FA"),
    ("OBROTY#300", "[IO] OK OBROTY=300 RPM (data[2]=0x64)", "10 09 64"),
    ("IO_HELP", "=HELP_TABLE", "10 09 64"),
    ("IO_HELP", "!PREDKOSC", "10 09 64"),
    ("IO_HELP", help_row("SWIATLA#<v>", "0 / 1", "swiatla (SW3), 1 = wlaczone"), "10 09 64"),
    ("IO_RESET", "[IO] OK IO_RESET: wejścia=0, OBROTY=0, PALIWO=0, AWARIA_ZAMKA=0", "10 00 00"),
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


HELP_COMMANDS = ["ZRODLO#<UART|IO>", "ZRODLO?", "IO_RESET", "STACYJKA#", "DRZWI_INNE#", "DRZWI_KIER#",
                 "SWIATLA#", "AWARYJNE#", "MASKA#", "HAMULEC#", "OBROTY#", "PALIWO#", "AWARIA_ZAMKA#"]
HELP_COMMANDS_CAR = ["PREDKOSC#", "PRZEBIEG#", "DROGOWE#", "HAMULEC_NOZNY#", "TEMP_SILNIKA#"]


def check_help_table(resp, commands=HELP_COMMANDS):
    """Sprawdza wyjście IO_HELP: ASCII, równa szerokość tabeli, obecność wszystkich komend."""
    lines = resp.split(" | [IO] ")
    if not resp.isascii():
        return False
    table = [ln.replace("[IO] ", "", 1) for ln in lines if ln.lstrip("[IO] ").startswith(("|", "+"))]
    if len(table) < 10 or len({len(ln) for ln in table}) != 1:
        return False
    rows = " ".join(table)
    return all(f"| {cmd}" in rows for cmd in commands)


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
        if exp_resp == "=HELP_TABLE":
            resp_ok = check_help_table(resp)
        elif exp_resp == "=HELP_TABLE_CAR":
            resp_ok = check_help_table(resp, HELP_COMMANDS + HELP_COMMANDS_CAR)
        elif exp_resp.startswith("~"):
            resp_ok = exp_resp[1:] in resp
        elif exp_resp.startswith("!"):
            resp_ok = exp_resp[1:] not in resp
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
