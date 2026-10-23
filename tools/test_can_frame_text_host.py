#!/usr/bin/env python3
"""
Test hostowy parsera / formattera can_frame_text.c względem can_support.parse_can_frame().

Kompiluje tools/host/can_frame_text_cli.c razem z app/can_bus/can_uart_bridge/can_frame_text.c
(lokalny gcc albo obraz Dockera vbox-builder), przepuszcza przez niego wektory z can_uart_vectors.py
i porównuje: ID, extended, FD, BRS, RTR, długość, dane i tekst to_display() albo treść błędu.

Użycie:
    python tools/test_can_frame_text_host.py [--serial-commander DIR] [--docker-image vbox-builder:latest]
Kod wyjścia 0 = 100% zgodności.
"""

import argparse
import os
import shutil
import subprocess
import sys

import can_uart_vectors as cv

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["tools/host/can_frame_text_cli.c", "app/can_bus/can_uart_bridge/can_frame_text.c"]
INCLUDES = ["app/can_bus", "app/can_bus/can_uart_bridge"]
CFLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1"]


def gcc_command(root, output):
    cmd = ["gcc", *CFLAGS]
    cmd += [f"-I{root}/{inc}" for inc in INCLUDES]
    cmd += [f"{root}/{src}" for src in SOURCES]
    return cmd + ["-o", output]


def run_harness(stdin_text, docker_image):
    """Kompiluje i uruchamia CLI. Zwraca listę linii wyjścia."""
    if shutil.which("gcc") and not docker_image:
        exe = os.path.join(REPO, "build", "can_frame_text_cli")
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


def actual_from_line(line):
    parts = line.split("\t")
    if parts[0] == "ERR":
        return {"ok": False, "error": parts[1] if len(parts) > 1 else ""}
    _, bus, ext, fd, brs, rtr, length, data, display = parts
    return {"ok": True, "bus": int(bus), "ext": ext == "1", "fd": fd == "1", "brs": brs == "1",
            "rtr": rtr == "1", "len": int(length), "data": data, "display": display}


def compare(exp, act):
    """Zwraca listę różnic (pusta = zgodne)."""
    if exp["ok"] != act["ok"]:
        return [f"ok: oczekiwano {exp['ok']}, jest {act['ok']} "
                f"({exp.get('error') or exp.get('display')!r} vs {act.get('error') or act.get('display')!r})"]
    if not exp["ok"]:
        return [] if exp["error"] == act["error"] else [f"błąd:\n      oczekiwano: {exp['error']}\n      jest:       {act['error']}"]
    diffs = []
    for key in ("bus", "ext", "fd", "brs", "rtr", "len", "data", "display"):
        if exp[key] != act[key]:
            diffs.append(f"{key}:\n      oczekiwano: {exp[key]!r}\n      jest:       {act[key]!r}")
    return diffs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial-commander", default=cv.DEFAULT_SERIAL_COMMANDER, help="katalog z can_support.py")
    ap.add_argument("--docker-image", default=None, help="wymusza kompilację w Dockerze (domyślnie gdy brak gcc)")
    ap.add_argument("-v", "--verbose", action="store_true", help="wypisuje także wektory zgodne")
    args = ap.parse_args()

    vectors = cv.build_expected(cv.load_can_support(args.serial_commander))
    stdin_text = "".join(f"{v['mode']}\t{v['line']}\n" for v in vectors)
    lines = run_harness(stdin_text, args.docker_image)
    if len(lines) != len(vectors):
        raise SystemExit(f"harness zwrócił {len(lines)} linii dla {len(vectors)} wektorów")

    failed = 0
    deviations = 0
    for i, (exp, line) in enumerate(zip(vectors, lines), 1):
        diffs = compare(exp, actual_from_line(line))
        if "deviation" in exp:
            deviations += 1
        if diffs:
            failed += 1
            print(f"FAIL #{i} [{exp['mode']}] {exp['line']!r}")
            for d in diffs:
                print(f"    {d}")
        elif args.verbose:
            print(f"PASS #{i} [{exp['mode']}] {exp['line']!r}")

    print(f"\n{len(vectors) - failed}/{len(vectors)} zgodnych, {failed} FAIL, "
          f"{deviations} z celowym odstępstwem (porównane z wartością skorygowaną)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
