#!/usr/bin/env python3
"""
Generator szkieletu symulatora auta (docs/car_sim/adding_a_car.md).

Czyta parser urządzenia LX (plik .c z tablicą ThisCar) tylko w czasie uruchomienia i tworzy
app/car_sim/sims/sim_<klucz>.c/.h:
- ramki cykliczne (100 ms, DLC 8, puste listy sygnałów) dla wpisów ThisCar na CAN1
  (wpis starszego szkieletu LX bez pola CAN_x należy do CAN1),
- nad każdą ramką ciało handlera LX jako komentarz i podpowiedzi sygnałów
  (Msg[N] & 0xMM -> { SIG_?, SIM_ENC_BIT, .byte = N, .mask = 0xMM }),
- pilota z PIN-em, jeśli handler korzysta z PinCode (pin_byte z ramek CAN_TransmitACK),
- listę kandydatów na komendy RX (CAN_TransmitACK) i kandydatów diagnostycznych (etap 2),
- pominięte wpisy CAN locker i CAN2.
Rejestruje symulator: car_sim_registry.h/.c, CMakeLists.txt, tools/test_car_sim_host.py.

Wygenerowany plik zawiera fragmenty parsera LX w komentarzach. Przed commitem zastąp je opisem
sygnałów (ID, maski, wzory): źródło parsera urządzenia nie trafia do repozytorium.

Użycie:
    python tools/create_car_sim.py <KLUCZ> <parser_LX.c> [--name "Renault Arkana"] [--dev-parser 196] [--dry-run]
"""

import argparse
import re
import sys
from datetime import date
from pathlib import Path

KEY_MAX = 8          # kolumna "Klucz" w SIM_LIST
NAME_MAX = 20        # kolumna "Nazwa" w SIM_LIST
BAUDRATES = {"BTR_125K": "CAN_BAUDRATE_125K", "BTR_250K": "CAN_BAUDRATE_250K",
             "BTR_500K": "CAN_BAUDRATE_500K", "BTR_1000K": "CAN_BAUDRATE_1000K", "BTR_1M": "CAN_BAUDRATE_1000K"}
LOCKER_RE = re.compile(r"LOCKER|LockAnswer|Lincor", re.IGNORECASE)
DIAG_RE = re.compile(r"Response|Diag|UDS|OBD", re.IGNORECASE)


def print_err(msg):
    print(f"[BŁĄD] {msg}", file=sys.stderr)


def print_info(msg):
    print(f"[INFO] {msg}")


def write_text(path, content):
    """Zapis UTF-8 z końcami linii LF (jak pliki repozytorium)."""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)


def find_project_root():
    current = Path(__file__).resolve().parent
    while current != current.parent:
        if (current / "CMakeLists.txt").exists() and (current / "app").exists():
            return current
        current = current.parent
    raise RuntimeError("nie znaleziono katalogu projektu (CMakeLists.txt + app/)")


# --- Analiza parsera LX ---------------------------------------------------------------------------

def read_lx(path):
    """Parsery LX mieszają kodowania: każda linia UTF-8, a gdy się nie da, cp1250."""
    lines = []
    for raw in path.read_bytes().split(b"\n"):
        try:
            lines.append(raw.decode("utf-8"))
        except UnicodeDecodeError:
            lines.append(raw.decode("cp1250", errors="replace"))
    return "\n".join(lines).replace("\r", "")


def blank_comments(text):
    """Zastępuje komentarze i literały spacjami (te same pozycje i znaki nowej linii)."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
        else:
            i += 1
            continue
        for k in range(i, j):
            if out[k] != "\n":
                out[k] = " "
        i = j
    return "".join(out)


def block_end(code, open_pos):
    """Pozycja za klamrą zamykającą blok, który otwiera code[open_pos] == '{'."""
    depth = 0
    for i in range(open_pos, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError("niezamknięty blok")


def parse_int(token, defines):
    token = defines.get(token, token).strip()
    try:
        return int(token, 0)
    except ValueError:
        return None


def parse_lx(text):
    code = blank_comments(text)
    defines = {m.group(1): m.group(2) for m in re.finditer(r"^\s*#define\s+(\w+)\s+(\S+)", code, re.MULTILINE)}

    m = re.search(r"\bThisCar\b[^=;]*=\s*\{", code)
    if not m:
        raise ValueError("brak tablicy ThisCar")
    table = code[m.end() - 1:block_end(code, m.end() - 1)]
    btr = re.search(r"\bBTR_\w+", table, re.IGNORECASE)

    entries = []
    # Older LX skeletons (single bus) have no CAN_x field: such entries belong to CAN_1.
    for e in re.finditer(r"\{\s*(?:(CAN_\d)\s*,\s*)?(MCR_\w+)\s*,\s*(\w+)\s*,\s*([^,{}]+?)\s*,\s*(\w+)\s*\}", table):
        bus, mode, ident, flags, handler = e.groups()
        bus = bus or "CAN_1"
        if mode == "MCR_CLRALL" or handler == "NULL":
            continue
        entries.append({"bus": bus, "id_token": ident, "id": parse_int(ident, defines),
                        "ext": "bt_CAN_B" in flags, "handler": handler})

    handlers = {}
    for h in {e["handler"] for e in entries}:
        hm = re.search(r"\b(?:short|void|int|char|unsigned\s+\w+)\s+" + re.escape(h) + r"\s*\([^;{)]*\)\s*\{", code)
        if hm:
            start = code.rfind("\n", 0, hm.start()) + 1
            end = block_end(code, hm.end() - 1)
            handlers[h] = {"text": text[start:end], "code": code[start:end]}

    calls = []
    prev = 0
    last_data = ["??"] * 8
    for c in re.finditer(r"CAN_TransmitACK\s*\(\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*(\w+)", code):
        first, ident = c.group(1), c.group(2)
        segment = code[max(prev, c.start() - 1500):c.start()]
        prev = c.end()
        data = ["??"] * 8
        assigned = False
        for a in re.finditer(r"\b" + re.escape(c.group(3)) + r"\[(\d)\]\s*=\s*([^;]+);", segment):
            idx, expr = int(a.group(1)), a.group(2)
            if idx >= 8:
                continue
            assigned = True
            if "PinCode" in expr:
                data[idx] = "PIN_H" if ">>" in expr else "PIN_L"
            else:
                v = parse_int(expr.strip().strip("()"), defines)
                data[idx] = f"{v:02X}" if v is not None and 0 <= v <= 0xFF else "??"
        if not assigned:
            data = list(last_data)
        last_data = data
        calls.append({"first": first, "id": parse_int(ident, defines), "data": data,
                      "line": code.count("\n", 0, c.start()) + 1})

    return {"btr": btr.group(0).upper() if btr else None, "entries": entries, "handlers": handlers, "calls": calls}


def classify(entry, handlers):
    h = handlers.get(entry["handler"], {"code": ""})
    if entry["bus"] != "CAN_1":
        return "can2"
    if entry["id"] is None or LOCKER_RE.search(entry["handler"]) or LOCKER_RE.search(entry["id_token"]):
        return "locker"
    if entry["id"] >= 0x700 or DIAG_RE.search(entry["handler"]):
        return "diag"
    if "PinCode" in h["code"]:
        return "fob"
    return "tx"


def signal_hints(code):
    hints, seen = [], set()
    for m in re.finditer(r"Msg\[(\d)\]\s*&\s*\(?\s*(0[xX][0-9A-Fa-f]+|\d+)", code):
        key = (int(m.group(1)), int(m.group(2), 0))
        if key not in seen and key[1] <= 0xFF:
            seen.add(key)
            hints.append(f"TODO: {{ .sig = SIG_?, .enc = SIM_ENC_BIT, .byte = {key[0]}, .mask = 0x{key[1]:02X} }}")
    masked = {b for b, _ in seen}
    raw = sorted({int(m.group(1)) for m in re.finditer(r"Msg\[(\d)\]", code)} - masked)
    if raw:
        hints.append("TODO: UINT field from bytes " + ", ".join(f"[{b}]" for b in raw) +
                     ": { .sig = SIG_?, .enc = SIM_ENC_UINT_BE, .byte = ?, .len = ?, .mul = ?, .div = ? }")
    return hints


# --- Generowanie ----------------------------------------------------------------------------------

def c_comment(text, indent=" * "):
    """Tekst jako linie komentarza C (bez zamykania komentarza w środku)."""
    lines = text.replace("\t", "    ").replace("*/", "* /").replace("/*", "/ *").splitlines()
    return "\n".join((indent + line).rstrip() for line in lines)


def format_id(entry):
    return f"0x{entry['id']:08X}U" if entry["ext"] else f"0x{entry['id']:03X}U"


def generate(key, name, dev_parser, lx_path, lx, today):
    lower = key.lower()
    handlers = lx["handlers"]
    groups = {"tx": [], "fob": [], "diag": [], "locker": [], "can2": []}
    for e in lx["entries"]:
        groups[classify(e, handlers)].append(e)

    fob_entry = groups["fob"][0] if groups["fob"] else None
    pin_byte = None
    for c in lx["calls"]:
        if "PIN_H" in c["data"]:
            pin_byte = c["data"].index("PIN_H")
            break

    cand = []
    seen = set()
    locker_ids = {e["id"] for e in groups["locker"] if e["id"] is not None}
    for c in lx["calls"]:
        if c["id"] is None or c["first"].strip() != "0" or c["id"] in locker_ids:
            continue
        k = (c["id"], tuple(c["data"]))
        if k in seen:
            continue
        seen.add(k)
        kind = "diagnostics (stage 2)" if c["id"] >= 0x700 else \
            ("command with PIN" if "PIN_H" in c["data"] else "command")
        cand.append(f"0x{c['id']:03X}  {' '.join(c['data'])}  ({kind}, line {c['line']})")

    baud = BAUDRATES.get(lx["btr"] or "", "CAN_BAUDRATE_500K")
    frames = []
    for e in groups["tx"]:
        h = handlers.get(e["handler"])
        parts = [f" * {format_id(e)[:-1]} <- {e['handler']} (device parser handler, remove before commit):"]
        if h:
            parts.append(c_comment(h["text"], " *   "))
            parts += [" * " + hint for hint in signal_hints(h["code"])]
        else:
            parts.append(" *   (handler definition not found)")
        frames.append("    /*\n" + "\n".join("   " + p for p in parts) + "\n     */\n"
                      f"    {{ .id = {format_id(e)}, .ext = {'true' if e['ext'] else 'false'}, .dlc = 8U, "
                      f".period_ms = 100U }},")

    skipped = [f"{e['id_token']} <- {e['handler']} (CAN locker, not the car)" for e in groups["locker"]] + \
              [f"{e['id_token']} <- {e['handler']} ({e['bus']}, simulators run on CAN1 only)" for e in groups["can2"]]
    diag = [f"0x{e['id']:03X} <- {e['handler']}" for e in groups["diag"]]

    fob_code = ""
    fob_ref = "NULL"
    if fob_entry is not None:
        h = handlers.get(fob_entry["handler"])
        fob_code = (
            "/*\n"
            f" * Pilot: {format_id(fob_entry)[:-1]} <- {fob_entry['handler']} (handler uses PinCode, remove before commit):\n"
            + (c_comment(h["text"], " *   ") + "\n" if h else "")
            + " * TODO: unlock / lock bytes from the command candidates with PIN (file header).\n"
            " */\n"
            "static const sim_fob_t s_fob = {\n"
            f"    .id          = {format_id(fob_entry)},\n"
            f"    .ext         = {'true' if fob_entry['ext'] else 'false'},\n"
            "    .dlc         = 8U,\n"
            "    .unlock      = { 0 },\n"
            "    .lock        = { 0 },\n"
            f"    .pin_byte    = {pin_byte if pin_byte is not None else 0}U,{'' if pin_byte is not None else ' /* TODO: PIN byte */'}\n"
            "    .default_pin = 0x1234U,\n"
            "};\n\n")
        fob_ref = "&s_fob"

    fob_label = format_id(fob_entry)[:-1] if fob_entry else "none"

    def bullet(items, empty="none"):
        return "\n".join(f" *   - {i}" for i in items) if items else f" *   - {empty}"

    source = f"""/**
 * @file sim_{lower}.c
 * @brief Car simulator {name} (device parser {dev_parser}): SKELETON generated by create_car_sim.py.
 *
 * WARNING: this file contains fragments of the device parser source in comments
 * ({lx_path.name}). Replace them with the description of the signals (IDs, masks, formulas)
 * before committing: the device parser source must not enter the repository.
 *
 * Generated from ThisCar: {len(lx['entries'])} entries, {len(groups['tx'])} cyclic frames, key fob: {fob_label}.
 * Command candidates (CAN_TransmitACK of the device parser, add to s_rx as SIM_ACT_UNLOCK / LOCK / LOG):
{bullet(cand)}
 * Diagnostic responses expected by the device parser (stage 2):
{bullet(diag)}
 * Skipped entries:
{bullet(skipped)}
 *
 * TODO: signals of every frame (docs/car_sim/adding_a_car.md), rx commands, key fob bytes, RPM limits,
 *       the "no data" codes the car never sends, rules like D13 (no RPM with the ignition off).
 *
 * @author Auto-generated by create_car_sim.py
 * @date {today}
 */

#include "sim_{lower}.h"
#include <stddef.h>

/* --- Frames ---------------------------------------------------------------- */

/** @brief Cyclic frames (TODO: signal tables, init bytes, real periods). */
static const sim_frame_t s_tx[] = {{
{chr(10).join(frames)}
}};

/* --- Key fob --------------------------------------------------------------- */

{fob_code}/* --- Profile --------------------------------------------------------------- */

/** @brief Device parser numbers. */
static const uint16_t s_dev_parsers[] = {{ {dev_parser}U }};

/** @brief Bus configuration: CAN1 Classic ({lx['btr'] or 'BTR_? -> 500 kbps'}). */
static const can_config_t s_bus_config = {{
    .mode            = CAN_MODE_CLASSIC,
    .baudrate_normal = {baud},
    .baudrate_FD     = CAN_BAUDRATE_CUSTOM
}};

/** @brief Immutable {name} profile. */
static const car_sim_profile_t s_profile = {{
    .key             = "{key}",
    .name            = "{name}",
    .dev_parsers     = s_dev_parsers,
    .num_dev_parsers = (uint8_t)(sizeof(s_dev_parsers) / sizeof(s_dev_parsers[0])),
    .bus_config      = &s_bus_config,
    .tx              = s_tx,
    .num_tx          = (uint8_t)(sizeof(s_tx) / sizeof(s_tx[0])),
    .rx              = NULL, /* TODO: commands from the candidates above */
    .num_rx          = 0U,
    .rpm_max         = 8000U, /* TODO */
    .rpm_step        = 1U,
    .idle_rpm        = 800U,
    .fob             = {fob_ref},
    .on_select       = NULL
}};

const car_sim_profile_t *sim_{lower}_profile_get(void)
{{
    return &s_profile;
}}
"""

    header = f"""/**
 * @file sim_{lower}.h
 * @brief Car simulator {name} (device parser {dev_parser}).
 *
 * @author Auto-generated by create_car_sim.py
 * @date {today}
 */

#ifndef SIM_{key}_H
#define SIM_{key}_H

#include "car_sim_types.h"

/**
 * @brief Returns the {name} profile.
 * @return Immutable profile descriptor.
 */
const car_sim_profile_t *sim_{lower}_profile_get(void);

#endif /* SIM_{key}_H */
"""
    summary = {"entries": len(lx["entries"]), "tx": len(groups["tx"]), "fob": format_id(fob_entry)[:-1] if fob_entry else None,
               "pin_byte": pin_byte, "diag": diag, "skipped": skipped, "candidates": cand}
    return header, source, summary


# --- Rejestracja ----------------------------------------------------------------------------------

def insert_after_last(content, pattern, line, what):
    matches = list(re.finditer(pattern, content, re.MULTILINE))
    if not matches:
        raise RuntimeError(f"nie znaleziono miejsca na {what}")
    pos = matches[-1].end()
    return content[:pos] + line + content[pos:]


def register(root, key, name, dev_parser, dry_run):
    lower = key.lower()
    edits = []

    reg_h = root / "app/car_sim/car_sim_registry.h"
    s = reg_h.read_text(encoding="utf-8")
    anchor = "    CAR_SIM_ID_NONE,"
    if anchor not in s:
        raise RuntimeError(f"brak {anchor.strip()} w {reg_h.name}")
    entry = f"    CAR_SIM_ID_{key},"
    s = s.replace(anchor, f"{entry:<26}/**< {key}: {name}, device parser {dev_parser} */\n{anchor}", 1)
    edits.append((reg_h, s))

    reg_c = root / "app/car_sim/car_sim_registry.c"
    s = reg_c.read_text(encoding="utf-8")
    s = insert_after_last(s, r'^#include "sim_\w+\.h"\n', f'#include "sim_{lower}.h"\n', "#include")
    anchor = re.search(r"^    \[CAR_SIM_ID_NONE\]\s*=", s, re.MULTILINE)
    if not anchor:
        raise RuntimeError(f"brak [CAR_SIM_ID_NONE] w {reg_c.name}")
    s = s[:anchor.start()] + f"    [CAR_SIM_ID_{key}] = sim_{lower}_profile_get,\n" + s[anchor.start():]
    edits.append((reg_c, s))

    cmake = root / "CMakeLists.txt"
    s = cmake.read_text(encoding="utf-8")
    s = insert_after_last(s, r"^  app/car_sim/sims/sim_\w+\.c\n", f"  app/car_sim/sims/sim_{lower}.c\n", "źródło w CMake")
    edits.append((cmake, s))

    test = root / "tools/test_car_sim_host.py"
    s = test.read_text(encoding="utf-8")
    if '"app/io/io_sim.c"]' not in s:
        raise RuntimeError(f"brak listy SOURCES w {test.name}")
    s = s.replace('"app/io/io_sim.c"]', f'"app/car_sim/sims/sim_{lower}.c",\n           "app/io/io_sim.c"]', 1)
    edits.append((test, s))

    if not dry_run:
        for path, content in edits:
            write_text(path, content)
    return [p for p, _ in edits]


def used_dev_parsers(sims_dir):
    used = {}
    for f in sims_dir.glob("sim_*.c"):
        m = re.search(r"s_dev_parsers\[\]\s*=\s*\{([^}]*)\}", f.read_text(encoding="utf-8", errors="replace"))
        if m:
            for no in re.findall(r"\d+", m.group(1)):
                used[int(no)] = f.name
    return used


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("key", help=f"klucz symulatora (SIM#<KLUCZ>), A-Z 0-9 _, maks. {KEY_MAX} znaków")
    ap.add_argument("lx_parser", type=Path, help="plik .c parsera urządzenia LX (tylko do odczytu)")
    ap.add_argument("--name", help=f"nazwa auta w SIM_LIST (maks. {NAME_MAX} znaków), domyślnie klucz")
    ap.add_argument("--dev-parser", type=int, help="numer parsera urządzenia (domyślnie z nazwy pliku, np. 196_...)")
    ap.add_argument("--dry-run", action="store_true", help="tylko analiza, bez zapisu plików")
    args = ap.parse_args()

    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")

    key = args.key.strip().upper()
    name = args.name or key
    if not re.fullmatch(r"[A-Z][A-Z0-9_]*", key) or len(key) > KEY_MAX or key in ("AUTO", "NONE", "BRAK"):
        print_err(f"zły klucz '{args.key}': A-Z 0-9 _, maks. {KEY_MAX} znaków, nie AUTO / BRAK")
        return 1
    if len(name) > NAME_MAX or '"' in name or "\\" in name:
        print_err(f"zła nazwa '{name}': maks. {NAME_MAX} znaków, bez cudzysłowów")
        return 1
    dev_parser = args.dev_parser
    if dev_parser is None:
        m = re.match(r"(\d+)", args.lx_parser.name)
        if not m:
            print_err("podaj --dev-parser (brak numeru na początku nazwy pliku)")
            return 1
        dev_parser = int(m.group(1))
    if not 1 <= dev_parser <= 0xFFFE:
        print_err("numer parsera urządzenia poza zakresem 1..65534 (0 = BRAK)")
        return 1

    root = find_project_root()
    sims_dir = root / "app/car_sim/sims"
    lower = key.lower()
    target_c, target_h = sims_dir / f"sim_{lower}.c", sims_dir / f"sim_{lower}.h"
    if target_c.exists() or target_h.exists():
        print_err(f"symulator już istnieje: {target_c.relative_to(root)}")
        return 1
    used = used_dev_parsers(sims_dir)
    if dev_parser in used:
        print_err(f"parser urządzenia {dev_parser} ma już symulator ({used[dev_parser]})")
        return 1
    registry = (root / "app/car_sim/car_sim_registry.h").read_text(encoding="utf-8")
    if f"CAR_SIM_ID_{key}," in registry:
        print_err(f"CAR_SIM_ID_{key} już jest w rejestrze")
        return 1

    try:
        lx = parse_lx(read_lx(args.lx_parser))
        header, source, summary = generate(key, name, dev_parser, args.lx_parser, lx, date.today().isoformat())
        if summary["tx"] == 0:
            raise ValueError("brak ramek cyklicznych w ThisCar")
        if summary["tx"] > 32:
            raise ValueError(f"{summary['tx']} ramek cyklicznych > CAR_SIM_TX_MAX (32)")
        updated = register(root, key, name, dev_parser, args.dry_run)
        if not args.dry_run:
            write_text(target_h, header)
            write_text(target_c, source)
    except (OSError, ValueError, RuntimeError) as e:
        print_err(str(e))
        return 1

    print(f"\n{'[DRY-RUN] ' if args.dry_run else ''}Szkielet symulatora {key} \"{name}\" (parser urządzenia {dev_parser}):")
    print(f"  ThisCar: {summary['entries']} wpisów, ramki cykliczne: {summary['tx']}, "
          f"pilot: {summary['fob'] or 'brak'}" + (f" (pin_byte {summary['pin_byte']})" if summary["fob"] else ""))
    print(f"  diagnostyka (etap 2): {', '.join(summary['diag']) or 'brak'}")
    print(f"  pominięte: {', '.join(summary['skipped']) or 'brak'}")
    print(f"  kandydaci komend RX: {len(summary['candidates'])}")
    for c in summary["candidates"]:
        print(f"    {c}")
    print(f"  + {target_c.relative_to(root)}\n  + {target_h.relative_to(root)}")
    for p in updated:
        print(f"  * {p.relative_to(root)}")
    print("\nDalej (docs/car_sim/adding_a_car.md):")
    print("  1. Sygnały ramek, komendy RX, bajty pilota, limity obrotów.")
    print("  2. Zastąp wklejony kod parsera LX opisem sygnałów (źródło parsera nie trafia do repo).")
    print("  3. python tools/test_car_sim_host.py: dopisz kroki z dekoderem referencyjnym.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
