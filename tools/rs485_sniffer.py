#!/usr/bin/env python3
"""
Podsłuch magistrali RS485 urządzenia telematycznego (LX, LX_micro, Roma, Viale) przez konwerter USB-RS485.

Dekoduje ramki AA 55 (CRC-16/MODBUS), odpowiedzi superStatus (parser urządzenia CAN1 / CAN2, IMEI, czas),
wgrywanie parsera urządzenia (start, bloki, koniec) i tekstowy log urządzenia. Zgłasza zmianę parsera
urządzenia. Opcjonalnie sam wysyła zapytanie superStatus (--poll), tak jak robi to v-box, co pozwala
sprawdzić odpowiedź urządzenia bez v-box. Protokół: docs/rs485/rs485_protocol.md.

Użycie:
    python tools/rs485_sniffer.py --port COM20                  # tylko nasłuch
    python tools/rs485_sniffer.py --port COM20 --poll 5         # + superStatus co 5 s
    python tools/rs485_sniffer.py --port COM20 --hide-cycle --log rs485.log
Wymaga: pyserial.
"""

import argparse
import datetime
import sys
import time

try:
    import serial
except ImportError:
    raise SystemExit("brak modułu pyserial: pip install pyserial")

SRC_PC = 0x90
ADDR_SUPERSTATUS = 0x50
ADDR_FILE_WRITE = 0x24
ADDR_FILE_CTRL = 0x26
CYCLE_CMDS = {0x1A, 0x1E, 0xD6, 0x11, 0x28, 0x37, 0x3B}
EPOCH_2000 = datetime.datetime(2000, 1, 1)


def crc16_modbus(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_frame(payload):
    body = bytes([len(payload)]) + bytes(payload)
    return b"\xAA\x55" + crc16_modbus(body).to_bytes(2, "little") + body


def superstatus_request(src, tool_version, password):
    pwd = password.encode("ascii")
    if len(pwd) > 9:
        raise SystemExit("hasło dłuższe niż 9 znaków")
    return build_frame(bytes([ADDR_SUPERSTATUS, src]) + tool_version.to_bytes(2, "little") + b"\x01" + pwd.ljust(9, b"\0"))


def text(field):
    return field.split(b"\0")[0].decode("ascii", errors="replace")


def decode_superstatus(p):
    """Pola odpowiedzi superStatus (payload[0] = 0x90, payload[1] = 0x50)."""
    u16 = lambda o: int.from_bytes(p[o:o + 2], "little")
    u32 = lambda o: int.from_bytes(p[o:o + 4], "little")
    return {
        "fw": u16(2), "time": EPOCH_2000 + datetime.timedelta(seconds=u32(5)), "inputs": p[9], "outputs": p[10],
        "power": int.from_bytes(p[11:13], "little", signed=True), "temp": int.from_bytes(p[14:15], "little", signed=True),
        "sat": p[20], "hdop": u16(22), "gsm": p[24], "operator": text(p[25:31]), "sim": text(p[31:51]),
        "modem": f"{p[91]}.{p[92]}.{p[93]}", "imei": text(p[94:110]), "can1": u16(118), "can2": u16(120),
    }


class Decoder:
    """Dekoder strumieniowy z resynchronizacją (odpowiednik app/rs485/rs485_frame.c)."""

    def __init__(self):
        self.buf = bytearray()
        self.text = bytearray()

    def push(self, data, idle=False):
        self.buf += data
        frames = []
        while True:
            i = self.buf.find(b"\xAA\x55")
            if i < 0:
                keep = 1 if (self.buf.endswith(b"\xAA") and not idle) else 0
                self.text += self.buf[:len(self.buf) - keep]
                del self.buf[:len(self.buf) - keep]
                break
            self.text += self.buf[:i]
            del self.buf[:i]
            if len(self.buf) >= 5 and self.buf[4] < 2:
                self.text += self.buf[:1]
                del self.buf[:1]
                continue
            if len(self.buf) < 5 or len(self.buf) < 5 + self.buf[4]:
                if not idle:
                    break
                self.text += self.buf[:1]
                del self.buf[:1]
                continue
            n = self.buf[4]
            if crc16_modbus(self.buf[4:5 + n]) == int.from_bytes(self.buf[2:4], "little"):
                frames.append(bytes(self.buf[5:5 + n]))
                del self.buf[:5 + n]
            else:
                self.text += self.buf[:1]
                del self.buf[:1]
        return frames

    def text_lines(self):
        lines = []
        while b"\n" in self.text:
            line, _, rest = self.text.partition(b"\n")
            self.text = bytearray(rest)
            printable = bytes(c for c in line if 32 <= c < 127).decode()
            letters = sum(ch.isalpha() for ch in printable)
            if len(printable) >= 4 and letters >= len(printable) // 2:
                lines.append(printable)
        if len(self.text) > 4096:
            del self.text[:-256]
        return lines


class Sniffer:
    def __init__(self, args):
        self.args = args
        self.log = open(args.log, "a", encoding="utf-8") if args.log else None
        self.parser_no = [None, None]
        self.blocks = 0
        self.own_tx = None

    def out(self, msg):
        line = f"{datetime.datetime.now():%H:%M:%S.%f}"[:-3] + " " + msg
        print(line, flush=True)
        if self.log:
            self.log.write(line + "\n")
            self.log.flush()

    def frame(self, p):
        dst, src = p[0], p[1]
        if self.own_tx is not None and p == self.own_tx[5:]:
            self.out("TX (echo)  superStatus 50 90")
            return
        if dst == SRC_PC and src == ADDR_SUPERSTATUS and len(p) >= 122:
            s = decode_superstatus(p)
            self.out(f"<- superStatus fw={s['fw']} czas={s['time']} UTC zasilanie={s['power']} temp={s['temp']}C "
                     f"sat={s['sat']} op={s['operator']} IMEI={s['imei']} parser urządzenia CAN1={s['can1']} CAN2={s['can2']}")
            for i, key in enumerate(("can1", "can2")):
                if self.parser_no[i] is not None and self.parser_no[i] != s[key]:
                    self.out(f"*** ZMIANA parsera urządzenia CAN{i + 1}: {self.parser_no[i]} -> {s[key]}")
                self.parser_no[i] = s[key]
        elif dst == SRC_PC and src == 0xD5 and p[2:] == b"\x0C\x06\x02":
            self.out("<- superStatus: BŁĘDNE HASŁO")
        elif dst == ADDR_SUPERSTATUS and src == SRC_PC:
            self.out("-> superStatus (zapytanie)")
        elif dst == ADDR_FILE_CTRL and src == SRC_PC and p[2:4] == b"\x00\x00" and len(p) > 21:
            self.blocks = 0
            self.out(f"-> WGRYWANIE start: {text(p[21:])} ({int.from_bytes(p[17:21], 'little')} B)")
        elif dst == ADDR_FILE_CTRL and src == SRC_PC and len(p) >= 24 and p[4] == 0x02:
            self.out(f"-> WGRYWANIE koniec: adres 0x{int.from_bytes(p[14:18], 'little'):08X} "
                     f"rozmiar {int.from_bytes(p[18:22], 'little')} B suma {p[22:24].hex(' ')} ({self.blocks} bloków)")
        elif dst == ADDR_FILE_WRITE and src == SRC_PC:
            self.blocks += 1
            if self.args.verbose:
                self.out(f"-> blok 0x{int.from_bytes(p[2:6], 'little'):08X}")
        elif dst == SRC_PC and src == ADDR_FILE_CTRL and len(p) >= 6:
            if p[4:6] == b"\x04\x02":
                self.out("<- WGRYWANIE potwierdzone przez urządzenie")
            elif p[4:6] != b"\x04\x08" or self.args.verbose:
                self.out(f"<- ACK wgrywania {p[4:].hex(' ')}")
        elif dst == SRC_PC and src == 0x16:
            self.out(f"<- LOG (ramka): {text(p[2:])}")
        elif (dst in CYCLE_CMDS and src == SRC_PC) or (dst == SRC_PC and src in CYCLE_CMDS) or (dst, src) == (0xA1, 0xD5):
            if not self.args.hide_cycle:
                self.out(f"   {p[:2].hex(' ')} len={len(p)}")
        else:
            self.out(f"?  {p.hex(' ')}")

    def run(self):
        port = serial.Serial(self.args.port, self.args.baud, timeout=0.02)
        dec = Decoder()
        request = superstatus_request(self.args.src, self.args.tool_version, self.args.password)
        self.out(f"nasłuch {self.args.port} {self.args.baud} 8N1" +
                 (f", superStatus co {self.args.poll} s: {request.hex(' ').upper()}" if self.args.poll else ""))
        last_rx = time.monotonic()
        next_poll = time.monotonic() + 1.0
        end = time.monotonic() + self.args.duration if self.args.duration else None
        try:
            while end is None or time.monotonic() < end:
                data = port.read(4096)
                now = time.monotonic()
                if data:
                    last_rx = now
                idle = not data and now - last_rx > 0.05
                for p in dec.push(data, idle):
                    self.frame(p)
                for line in dec.text_lines():
                    self.out(f"<- LOG: {line}")
                if self.args.poll and now >= next_poll and now - last_rx > 0.02:
                    port.write(request)
                    self.own_tx = request
                    self.out("TX superStatus")
                    next_poll = now + self.args.poll
        except KeyboardInterrupt:
            pass
        finally:
            port.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="port konwertera USB-RS485, np. COM20")
    ap.add_argument("--baud", type=int, default=57600)
    ap.add_argument("--poll", type=float, default=0, help="wysyłaj superStatus co N sekund (0 = tylko nasłuch)")
    ap.add_argument("--password", default="9999")
    ap.add_argument("--src", type=lambda v: int(v, 0), default=SRC_PC, help="adres nadawcy zapytania (domyślnie 0x90)")
    ap.add_argument("--tool-version", type=lambda v: int(v, 0), default=0x0F45)
    ap.add_argument("--duration", type=float, default=0, help="czas nasłuchu w sekundach (0 = do Ctrl+C)")
    ap.add_argument("--hide-cycle", action="store_true", help="ukryj cykliczne ramki programu serwisowego")
    ap.add_argument("--log", help="dopisuj wynik do pliku")
    ap.add_argument("-v", "--verbose", action="store_true", help="pokaż każdy blok wgrywania i ACK")
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    Sniffer(args).run()


if __name__ == "__main__":
    main()
