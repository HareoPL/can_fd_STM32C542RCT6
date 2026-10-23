#!/usr/bin/env python3
"""
Wektory zgodności parsera ramek tekstowych (can_frame_text.c / can_uart_bridge) z Serial Commanderem.

Wynik oczekiwany liczy can_support.parse_can_frame() + CanFrame.to_display(). Moduł jest
współdzielony przez test hostowy (tools/test_can_frame_text_host.py) i test sprzętowy
(tools/test_can_uart_bridge.py).

Użycie samodzielne (zapis wektorów do JSON):
    python tools/can_uart_vectors.py --serial-commander C:/WORK/01_tools/serial_commander
"""

import argparse
import json
import os
import sys

DEFAULT_SERIAL_COMMANDER = r"C:\WORK\01_tools\serial_commander"
DEFAULT_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "can_uart_vectors.json")

MODES = ("auto", "classic", "fd_brs", "fd_no_brs", "tlv")

# Celowe odstępstwa od can_support.py (opisane w raporcie testu)
DEVIATION_TLV_TOO_LONG = "TLV > 61 B: Python tworzy ramkę > 64 B, węzeł zwraca błąd"
DEVIATION_NON_PRINTABLE = "znaki TLV spoza ASCII 32-126: '?' zamiast U+FFFD / surowego znaku sterującego"

TLV_MAX_VALUE = 61


def load_can_support(path):
    """Importuje can_support z katalogu Serial Commandera."""
    if path not in sys.path:
        sys.path.insert(0, path)
    import can_support  # noqa: E402
    return can_support


def _hex_bytes(n, start=0x11):
    return " ".join(f"{(start + i) & 0xFF:02X}" for i in range(n))


def build_vectors():
    """Zwraca listę (tryb, linia). Linie bez znaków sterujących (UART odrzuca je wcześniej)."""
    v = []

    def add(text, mode="auto"):
        v.append((mode, text))

    # --- Identyfikatory ---
    for text in ("0#", "7FF#01", "800#01", "00000123#01", "1FFFFFFF#01", "20000000#01", "FFFFFFFF#01",
                 "0x1A#01", "0X7ff#AA", "G1#01", "123456789#01", "0x#01", "#01", " 123 # 01 02 ",
                 "000#", "0800#", "1 23#01", "0x0000007FF#01", "18DAF110#01 02 03"):
        add(text)

    # --- Dane ---
    for n in range(0, 9):
        add(f"187#{_hex_bytes(n)}")
    for n in (9, 12, 16, 20, 24, 32, 48, 64, 65):
        add(f"321#{_hex_bytes(n)}")
    for text in ("123#112", "123#0G", "123#01.02:03_04-05 06  07", "3D0#", "123#  ", "123#1", "123#01 0",
                 "123#0x01", "123#01#02", "123#-", "123#aa bb cc"):
        add(text)

    # --- CAN FD '##' ---
    for text in ("321##0#01 02 03", "321##1#01 02 03", "321##1", "321##0", "321##", "321##01 02", "321##1010203",
                 "321##1##01", "321##2#01", "321## 1 # 01", "321##1#", f"321##0#{_hex_bytes(64)}",
                 f"321##1#{_hex_bytes(65)}", "18DAF110##0#01 02 03", "321#01##02"):
        add(text)

    # --- RTR ---
    for text in ("65A#R", "65A#r", "65A#R0", "65A#R8", "65A#R9", "65A#R 4", "65A#RX", "65A#R08", "65A#R-1",
                 "321##1#R", "321##R", "18DAF110#R3"):
        add(text)

    # --- TLV ---
    for text in ("TLV#321#0xA5#Hello", "TLV#321#165#hex:01 02 03", "tlv#321#0xa5#Hi", "TLV#321#256#x",
                 "TLV#321#255#x", "TLV#321#-1#x", "TLV#321#-0#x", "TLV#321#+5#x", "TLV#321#1_0#x", "TLV#321#1__0#x",
                 "TLV#321#0x_A5#x", "TLV#321#0x#x", "TLV#321#abc#x", "TLV#321#007#x", "TLV#321#0xA5",
                 "TLV#321", "TLV#321#0xA5#", "TLV#321#0xA5#a#b#c", "TLV#G#1#x", "TLV#321#0xA5#hex:0102 0",
                 "TLV#321#0xA5#hex:zz", "TLV#321#0xA5#hex:", "TLV#321#0xA5#HEX:00 01 FF",
                 "TLV#18DAF110#7#Hello world", "TLV # 321 # 0xA5 #  spaced  ",
                 "TLV#321#0xA5#" + "A" * TLV_MAX_VALUE, "TLV#321#0xA5#" + "B" * (TLV_MAX_VALUE + 1),
                 "TLV#321#0xA5#hex:" + _hex_bytes(TLV_MAX_VALUE), "TLV#321#0xA5#hex:" + _hex_bytes(TLV_MAX_VALUE + 1)):
        add(text)

    # --- Prefiksy magistrali ---
    for text in ("CAN1:123#01", "can2:123#01", "CAN2: TLV#321#1#x", "Can1:65A#R2", "CAN3:123#01", "CAN1:",
                 "CAN1 :123#01", "  CAN2:18DAF110##0#01 02 03  "):
        add(text)

    # --- Różne ---
    for text in ("", "   ", "123", "hello", "#", "##"):
        add(text)

    # --- Wszystkie tryby wymuszone ---
    for mode in MODES:
        for text in ("123#01 02", f"123#{_hex_bytes(9)}", "321##0#01", "321##1", "65A#R2", "TLV#321#1#ab",
                     f"123#{_hex_bytes(61)}", f"123#{_hex_bytes(62)}", f"123#{_hex_bytes(65)}", "123#"):
            add(text, mode)

    return v


def _printable(text):
    return "".join(c if 32 <= ord(c) <= 126 else "?" for c in text)


def split_bus_prefix(text):
    """Odpowiednik prefiksu CAN1:/CAN2: z can_frame_text.c. Zwraca (bus, reszta)."""
    s = text.strip()
    if s[:5].upper() in ("CAN1:", "CAN2:"):
        return int(s[3]), s[5:]
    return 0, s


def expected(can_support, mode, text):
    """Wynik oczekiwany dla jednego wektora."""
    bus, rest = split_bus_prefix(text)
    rec = {"mode": mode, "line": text}
    try:
        frame = can_support.parse_can_frame(rest, mode)
    except Exception as exc:  # CanFrameError oraz ValueError z bytes.fromhex()
        rec.update(ok=False, error=str(exc))
        return rec

    if frame.is_fd and len(frame.data) > 64:
        value_len = len(frame.data) - 3
        rec.update(ok=False, error=f"za dużo danych TLV: {value_len} B (maks. {TLV_MAX_VALUE} B)",
                   deviation=DEVIATION_TLV_TOO_LONG)
        return rec

    display = frame.to_display(show_tlv=True)
    if _printable(display) != display:
        rec["deviation"] = DEVIATION_NON_PRINTABLE
    rec.update(ok=True, bus=bus, ext=frame.is_extended, fd=frame.is_fd, brs=frame.bitrate_switch,
               rtr=frame.is_remote, id=frame.arbitration_id, len=frame.dlc, data=frame.data.hex().upper(),
               display=_printable(display))
    return rec


def build_expected(can_support):
    return [expected(can_support, mode, text) for mode, text in build_vectors()]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial-commander", default=DEFAULT_SERIAL_COMMANDER, help="katalog z can_support.py")
    ap.add_argument("--out", default=DEFAULT_JSON, help="plik wyjściowy JSON")
    args = ap.parse_args()

    vectors = build_expected(load_can_support(args.serial_commander))
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(vectors, f, ensure_ascii=False, indent=1)
    print(f"{len(vectors)} wektorów -> {args.out}")


if __name__ == "__main__":
    main()
