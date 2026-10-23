#!/usr/bin/env python3
"""
CAN FD bus test runner for the V-Box node (firmware built with CMake option CAN_FD_TEST=ON).

The node runs the CP_FD_TEST parser (app/can_bus/can_fd_test/), which switches bus mode and bit
rates on request through the regular parser -> manager -> port layers. This tool walks a test
matrix: for every case it reconfigures the node, reopens the candleLight adapter with the same
settings, runs an echo test over all DLC lengths, a node TX burst, an OTA discovery (Classic frames
on an FD bus) and reads bus health (TEC/REC, LEC/DLEC, TDC value). Node UART logs are captured
in parallel. Results are written as a Markdown report.

Usage:
    python tools/can_fd_test.py --uart COM22 --report docs/can_bus/can_fd_test_report.md
    python tools/can_fd_test.py --cases "fd_brs_500k_2m,fd_brs_500k_4m" --reps 20
    python tools/can_fd_test.py --uart COM22 --bus 2     # CAN2 under test (firmware with CAN_FD_TEST_LOOP=ON)

--bus selects the node bus under test (SELECT_BUS command): it serves echo, burst and TX probe, the other
bus is the loop partner and follows every configuration. OTA discovery (answered by CAN1) runs for --bus 1 only.
--skip-loop skips the loop when CAN1 and CAN2 are not on the same physical bus.

Protocol constants mirror app/can_bus/can_fd_test/can_fd_test.h.
"""

import argparse
import datetime
import os
import random
import sys
import threading
import time
from dataclasses import dataclass, field
from types import SimpleNamespace
from typing import Dict, List, Optional, Tuple

import can

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import can_updater  # noqa: E402  (shared adapter factory and OTA discovery)

# ==============================================================================
# Protocol (app/can_bus/can_fd_test/can_fd_test.h)
# ==============================================================================
ID_CMD = 0x01F2F400
ID_RESP = 0x01F2F401
ID_ECHO_EXT = 0x01F2F410
ID_ECHO_STD = 0x410
ID_BURST = 0x01F2F412

CMD_PING = 0x01
CMD_SET_CONFIG = 0x02
CMD_GET_STATS = 0x03
CMD_RESET_STATS = 0x04
CMD_BURST = 0x05
CMD_TX_PROBE = 0x06
CMD_LOOP = 0x07
CMD_SELECT_BUS = 0x08
RESP_FLAG = 0x80

STATUS_OK = 0x00
STATUS_BAD_ARG = 0x01
STATUS_CONFIG_REJECTED = 0x02
STATUS_TX_FAILED = 0x03

MODE_CLASSIC = 0
MODE_FD_NO_BRS = 1
MODE_FD_BRS = 2
MODE_NAMES = {MODE_CLASSIC: "Classic", MODE_FD_NO_BRS: "FD", MODE_FD_BRS: "FD+BRS"}

LOOP_FRAMES = 200                # bus under test -> partner -> bus under test frames per case
CONFIRM_TIMEOUT_S = 5.0          # CAN_FD_TEST_CONFIRM_TIMEOUT_MS
SWITCH_WAIT_S = 0.3              # > CAN_FD_TEST_SWITCH_DELAY_MS (200 ms)
NODE_SAMPLE_POINT = 87.5         # STM32C5_NOMINAL_SAMPLE_POINT
NODE_DATA_SAMPLE_POINT = 75.0    # STM32C5_DATA_SAMPLE_POINT

HIST_NAMES = ("STUFF", "FORM", "ACK", "BIT1", "BIT0", "CRC")

# FDCAN register limits (prescaler, TSEG1, TSEG2); identical for STM32C5 and the candleLight FDCAN
NOMINAL_LIMITS = (512, 256, 128)
DATA_LIMITS = (32, 32, 16)

CLASSIC_LENGTHS = list(range(0, 9))
FD_LENGTHS = list(range(0, 9)) + [12, 16, 20, 24, 32, 48, 64]


def calc_bit_timing(clock: int, bitrate: int, sample_point: int, pmin: int, pmax: int,
                    limits: Tuple[int, int, int]) -> Optional[Tuple[int, int, int]]:
    """
    Python mirror of stm32c5_calc_bit_timing(): smallest prescaler giving an exact bit rate
    with the sample point (per mille) within 3%. Returns (prescaler, tseg1, tseg2) or None.
    """
    for p in range(pmin, min(pmax, limits[0]) + 1):
        if clock % (p * bitrate):
            continue
        tq = clock // (p * bitrate)
        if tq < 8 or tq > 1 + limits[1] + limits[2]:
            continue
        t2 = max(1, min((tq * (1000 - sample_point) + 500) // 1000, limits[2]))
        t1 = tq - 1 - t2
        if t1 > limits[1]:
            t1, t2 = limits[1], tq - 1 - limits[1]
        if t1 < 1 or t2 > limits[2] or abs((1 + t1) * 1000 // tq - sample_point) > 30:
            continue
        return p, t1, t2
    return None


def calc_config_timing(clock: int, nominal: int, data: Optional[int]):
    """Nominal (and data) timing like the node: data first, nominal preferring the same prescaler."""
    d = calc_bit_timing(clock, data, 750, 1, 32, DATA_LIMITS) if data else None
    if data and d is None:
        return None, None
    n = (calc_bit_timing(clock, nominal, 875, d[0], d[0], NOMINAL_LIMITS) if d else None) or         calc_bit_timing(clock, nominal, 875, 1, 512, NOMINAL_LIMITS)
    return n, d


class explicit_adapter_timing:
    """
    Context manager replacing python-can's sample point search (used internally by CandleBus)
    with calc_config_timing(), so the adapter uses the same timing rules as the node
    (python-can finds no 125 kbps timing at 160 MHz and picks coarse data prescalers).
    """

    def __enter__(self):
        self._bt = can.BitTiming.__dict__["from_sample_point"]
        self._btfd = can.BitTimingFd.__dict__["from_sample_point"]

        def bt(cls, f_clock, bitrate, sample_point=69.0):
            n, _ = calc_config_timing(f_clock, bitrate, None)
            if n is None:
                raise ValueError(f"no exact timing for {bitrate} bit/s at {f_clock} Hz")
            # CandleBus reads only these fields; can.BitTiming would reject FDCAN ranges (SJA1000 limits)
            return SimpleNamespace(brp=n[0], tseg1=n[1], tseg2=n[2], sjw=n[2])

        def btfd(cls, f_clock, nom_bitrate, nom_sample_point, data_bitrate, data_sample_point):
            n, d = calc_config_timing(f_clock, nom_bitrate, data_bitrate)
            if n is None or d is None:
                raise ValueError(f"no exact timing for {nom_bitrate}/{data_bitrate} bit/s at {f_clock} Hz")
            return SimpleNamespace(nom_brp=n[0], nom_tseg1=n[1], nom_tseg2=n[2], nom_sjw=n[2],
                                   data_brp=d[0], data_tseg1=d[1], data_tseg2=d[2], data_sjw=min(d[2], 16))

        can.BitTiming.from_sample_point = classmethod(bt)
        can.BitTimingFd.from_sample_point = classmethod(btfd)
        return self

    def __exit__(self, *exc):
        can.BitTiming.from_sample_point = self._bt
        can.BitTimingFd.from_sample_point = self._btfd
        return False


@dataclass
class BusConfig:
    mode: int
    nominal: int          # bit/s
    data: int = 0         # bit/s, CAN_MODE_FD_BRS only

    @property
    def is_fd(self) -> bool:
        return self.mode != MODE_CLASSIC

    @property
    def brs(self) -> bool:
        return self.mode == MODE_FD_BRS

    def label(self) -> str:
        if self.mode == MODE_FD_BRS:
            return f"{MODE_NAMES[self.mode]} {self.nominal // 1000}k/{self.data // 1000}k"
        return f"{MODE_NAMES[self.mode]} {self.nominal // 1000}k"


DEFAULT_CONFIG = BusConfig(MODE_CLASSIC, 500_000)


@dataclass
class TestCase:
    name: str
    config: BusConfig
    expect_reject: bool = False


def build_matrix() -> List[TestCase]:
    cases = []
    for nominal in (125_000, 250_000, 500_000, 1_000_000):
        cases.append(TestCase(f"classic_{nominal // 1000}k", BusConfig(MODE_CLASSIC, nominal)))
    for nominal in (500_000, 1_000_000):
        cases.append(TestCase(f"fd_{nominal // 1000}k", BusConfig(MODE_FD_NO_BRS, nominal)))
    for nominal, datas in ((500_000, (1_000_000, 2_000_000, 4_000_000, 8_000_000)),
                           (1_000_000, (2_000_000, 4_000_000, 8_000_000))):
        for data in datas:
            cases.append(TestCase(f"fd_brs_{nominal // 1000}k_{data // 1_000_000}m",
                                  BusConfig(MODE_FD_BRS, nominal, data)))
    return cases


@dataclass
class CaseResult:
    case: TestCase
    passed: bool = False
    switched: bool = False
    echo_sent: int = 0
    echo_ok: int = 0
    echo_lost: int = 0
    echo_bad: int = 0
    burst_expected: int = 0
    burst_ok: int = 0
    discovery: Optional[bool] = None
    loop_sent: int = 0
    loop_ok: int = 0
    loop_bad: int = 0
    loop_stats: Dict = field(default_factory=dict)
    loop_available: bool = True
    node_passed: Optional[bool] = None
    pc_passed: bool = False
    pc_counters: Optional[Tuple[int, int]] = None
    stats_before: Dict = field(default_factory=dict)
    stats_after: Dict = field(default_factory=dict)
    duration_s: float = 0.0
    notes: List[str] = field(default_factory=list)


# ==============================================================================
# UART log capture
# ==============================================================================
class UartLogger:
    """Captures node UART logs (safe_printf) in a background thread."""

    def __init__(self, port: Optional[str], baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.lines: List[Tuple[float, str]] = []
        self._stop = threading.Event()
        self._thread = None
        self._serial = None

    def start(self):
        if not self.port:
            return
        import serial
        self._serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        buf = b""
        while not self._stop.is_set():
            buf += self._serial.read(1024)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").rstrip("\r")
                if text:
                    self.lines.append((time.time(), text))

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1.0)
        if self._serial:
            self._serial.close()

    def since(self, t0: float, exclude=("[IO_MGR]", "[MAIN] main task")) -> List[str]:
        return [text for ts, text in self.lines if ts >= t0 and not any(x in text for x in exclude)]


# ==============================================================================
# Test runner
# ==============================================================================
class FdTester:
    def __init__(self, interface: str, channel: Optional[str], reps: int, verbose: bool, skip_loop: bool = False):
        self.interface = interface
        self.channel = channel
        self.reps = reps
        self.verbose = verbose
        self.skip_loop = skip_loop
        self.bus: Optional[can.BusABC] = None
        self.current = DEFAULT_CONFIG
        self.node_bus = 1     # bus under test reported by PING (1 = CAN1, 2 = CAN2)
        self.rng = random.Random(0xCAFD)

    # --- adapter -------------------------------------------------------------
    def open(self, cfg: BusConfig):
        self.close()
        with explicit_adapter_timing():
            self.bus = can_updater.open_can_bus(
                self.interface, self.channel, bitrate=cfg.nominal,
                fd=cfg.is_fd, data_bitrate=(cfg.data if cfg.brs else cfg.nominal),
                sample_point=NODE_SAMPLE_POINT, data_sample_point=NODE_DATA_SAMPLE_POINT)
        time.sleep(0.05)

    def close(self):
        if self.bus is not None:
            try:
                self.bus.shutdown()
            except Exception:
                pass
            # CandleBus.shutdown() only resets the channels; the USB device must be closed
            # explicitly, otherwise reopening with a new bit timing fails ("Cannot open device")
            device = getattr(self.bus, "_device", None)
            if device is not None:
                try:
                    device.close()
                except Exception:
                    pass
            self.bus = None

    def adapter_counters(self) -> Optional[Tuple[int, int]]:
        """candleLight TEC/REC (diagnostics only, they decrease with successful frames)."""
        try:
            state = list(self.bus._channels.values())[0].state
            return state.tx_error_count, state.rx_error_count
        except Exception:
            return None

    def flush(self):
        while self.bus.recv(timeout=0.002) is not None:
            pass

    def log(self, text: str):
        if self.verbose:
            print(f"    {text}")

    # --- protocol --------------------------------------------------------------
    def send_cmd(self, payload: List[int]):
        msg = can.Message(arbitration_id=ID_CMD, is_extended_id=True, data=bytes(payload))
        self.bus.send(msg, timeout=0.2)

    def wait_resp(self, cmd: int, timeout: float, count: int = 1) -> List[bytes]:
        frames = []
        t_end = time.time() + timeout
        while time.time() < t_end and len(frames) < count:
            rx = self.bus.recv(timeout=0.02)
            if rx is None or not rx.is_extended_id or rx.arbitration_id != ID_RESP:
                continue
            if len(rx.data) >= 2 and rx.data[0] == (cmd | RESP_FLAG):
                frames.append(bytes(rx.data))
        return frames

    def ping(self, retries: int = 3) -> Optional[BusConfig]:
        for _ in range(retries):
            try:
                self.flush()
                self.send_cmd([CMD_PING])
            except can.CanError:
                time.sleep(0.1)
                continue
            resp = self.wait_resp(CMD_PING, 0.3)
            if resp and len(resp[0]) >= 7:
                r = resp[0]
                # Byte 7 (bus under test) is missing in firmware without SELECT_BUS: always CAN1
                self.node_bus = r[7] if len(r) >= 8 else 1
                return BusConfig(r[2], (r[3] | (r[4] << 8)) * 1000, (r[5] | (r[6] << 8)) * 1000)
        return None

    def select_bus(self, bus: int, retries: int = 3) -> Tuple[bool, str]:
        """
        Makes CAN1 or CAN2 of the node the bus under test and verifies it with PING. The selected bus answers
        itself, so this also works when the adapter is wired to that bus only.
        """
        resp = []
        for _ in range(retries):
            try:
                self.flush()
                self.send_cmd([CMD_SELECT_BUS, bus])
            except can.CanError:
                time.sleep(0.1)
                continue
            resp = self.wait_resp(CMD_SELECT_BUS, 0.5)
            if resp:
                break
        if not resp:
            return False, (f"no SELECT_BUS response: adapter not connected to CAN{bus}, CAN_FD_TEST firmware "
                           f"not flashed{'' if bus == 1 else ' or built without CAN_FD_TEST_LOOP'}")
        if resp[0][1] == STATUS_BAD_ARG:
            return False, f"CAN{bus} not available (build the test firmware with -DCAN_FD_TEST_LOOP=ON)"
        if resp[0][1] != STATUS_OK:
            return False, f"SELECT_BUS status {resp[0][1]}"
        if self.ping() is None or self.node_bus != bus:
            return False, f"node reports CAN{self.node_bus} under test"
        return True, ""

    def connect(self, bus: int) -> Tuple[Optional[BusConfig], str]:
        """Selects the bus under test and returns the active node configuration (None if unreachable)."""
        ok, note = self.select_bus(bus)
        if ok:
            return self.ping(), ""
        # Firmware without SELECT_BUS: only CAN1 is served
        if bus == 1:
            node = self.ping()
            if node is not None and self.node_bus == 1:
                return node, ""
        return None, note

    def reset_stats(self) -> bool:
        self.flush()
        self.send_cmd([CMD_RESET_STATS])
        return bool(self.wait_resp(CMD_RESET_STATS, 0.3))

    def get_stats(self) -> Dict:
        self.flush()
        self.send_cmd([CMD_GET_STATS])
        frames = self.wait_resp(CMD_GET_STATS, 0.5, count=7)
        stats = {}
        for f in frames:
            if f[1] == 0:
                stats.update(tec=f[2], rec=f[3], lec=f[4], dlec=f[5], tdcv=f[6],
                             error_passive=bool(f[7] & 0x01), bus_off=bool(f[7] & 0x02))
            elif f[1] == 1:
                stats.update(echo_rx=int.from_bytes(f[2:6], "little"), echo_drop=int.from_bytes(f[6:8], "little"))
            elif f[1] == 2:
                stats.update(tx_error=int.from_bytes(f[2:6], "little"), bus_off_cnt=int.from_bytes(f[6:8], "little"))
            elif f[1] == 5:
                stats.update(bus_errors=int.from_bytes(f[2:6], "little"))
            elif f[1] == 6:
                stats.update(loop_sent=int.from_bytes(f[2:4], "little"), loop_ok=int.from_bytes(f[4:6], "little"),
                             loop_bad=int.from_bytes(f[6:8], "little"))
            elif f[1] in (3, 4):
                key = "lec_hist" if f[1] == 3 else "dlec_hist"
                stats[key] = {name: f[2 + i] for i, name in enumerate(HIST_NAMES) if f[2 + i]}
        return stats

    def set_config(self, cfg: BusConfig) -> Tuple[bool, str]:
        """
        Requests a configuration change and closes the adapter right after the acknowledge, so the PC
        never talks with a different bit rate than the node (node switches CAN_FD_TEST_SWITCH_DELAY_MS later).
        """
        self.flush()
        payload = [CMD_SET_CONFIG, cfg.mode,
                   (cfg.nominal // 1000) & 0xFF, (cfg.nominal // 1000) >> 8,
                   (cfg.data // 1000) & 0xFF, (cfg.data // 1000) >> 8]
        self.send_cmd(payload)
        ack = self.wait_resp(CMD_SET_CONFIG, 0.15)
        self.close()
        if not ack or ack[0][1] != STATUS_OK:
            return False, "no SET_CONFIG acknowledge"
        time.sleep(SWITCH_WAIT_S)
        return True, ""

    @staticmethod
    def _same(a: Optional[BusConfig], b: BusConfig) -> bool:
        return (a is not None and a.mode == b.mode and a.nominal == b.nominal and
                (not b.brs or a.data == b.data))

    def switch_to(self, cfg: BusConfig) -> Tuple[bool, str]:
        previous = self.current
        accepted, note = self.set_config(cfg)
        if not accepted:
            self.open(previous)
            return False, note
        try:
            self.open(cfg)
            reported = self.ping()
        except Exception as e:
            reported, note = None, f"adapter: {e}"
        if self._same(reported, cfg):
            self.current = cfg
            return True, ""
        # Node may have rejected the configuration and kept the previous one (PING is a Classic frame)
        if reported is None:
            self.open(previous)
            reported = self.ping()
        if self._same(reported, previous):
            self.open(previous)
            return False, "rejected by node"
        return False, note or (f"node reports {reported.label()}" if reported else "no PING response in new configuration")

    def recover(self) -> bool:
        """Waits for the node fallback (Classic 500 kbps) and reconnects."""
        self.close()
        time.sleep(CONFIRM_TIMEOUT_S + 1.5)
        self.open(DEFAULT_CONFIG)
        reported = self.ping()
        self.current = DEFAULT_CONFIG
        return reported is not None and reported.mode == MODE_CLASSIC

    # --- traffic -----------------------------------------------------------------
    def _payload(self, pattern: str, length: int, seq: int) -> bytes:
        if pattern == "00":
            return bytes(length)
        if pattern == "FF":
            return bytes([0xFF] * length)
        if pattern == "55":
            return bytes([0x55] * length)
        if pattern == "AA":
            return bytes([0xAA] * length)
        if pattern == "cnt":
            return bytes([(i + seq) & 0xFF for i in range(length)])
        return bytes(self.rng.getrandbits(8) for _ in range(length))

    def echo_frames(self, cfg: BusConfig) -> List[can.Message]:
        frames = []
        seq = 0
        groups = [(False, CLASSIC_LENGTHS)]
        if cfg.is_fd:
            groups.append((True, FD_LENGTHS))
        for _ in range(self.reps):
            for is_fd, lengths in groups:
                for length in lengths:
                    for pattern in ("00", "FF", "55", "AA", "cnt", "rnd"):
                        for ext in (True, False):
                            frames.append(can.Message(
                                arbitration_id=ID_ECHO_EXT if ext else ID_ECHO_STD,
                                is_extended_id=ext, is_fd=is_fd,
                                bitrate_switch=(is_fd and cfg.brs),
                                data=self._payload(pattern, length, seq)))
                            seq += 1
        return frames

    def run_echo(self, cfg: BusConfig, result: CaseResult):
        self.flush()
        for req in self.echo_frames(cfg):
            result.echo_sent += 1
            try:
                self.bus.send(req, timeout=0.2)
            except can.CanError as e:
                result.echo_lost += 1
                if len(result.notes) < 5:
                    result.notes.append(f"send error: {e}")
                continue
            expected_id = req.arbitration_id + 1
            t_end = time.time() + 0.25
            got = None
            while time.time() < t_end:
                rx = self.bus.recv(timeout=0.02)
                if rx is not None and rx.arbitration_id == expected_id and rx.is_extended_id == req.is_extended_id:
                    got = rx
                    break
            if got is None:
                result.echo_lost += 1
                continue
            same = (bytes(got.data) == bytes(req.data) and got.is_fd == req.is_fd and
                    (not req.is_fd or got.bitrate_switch == req.bitrate_switch))
            if same:
                result.echo_ok += 1
            else:
                result.echo_bad += 1
                if len(result.notes) < 5:
                    result.notes.append(
                        f"echo mismatch id=0x{req.arbitration_id:X} fd={req.is_fd} brs={req.bitrate_switch} "
                        f"len={len(req.data)} -> fd={got.is_fd} brs={got.bitrate_switch} len={len(got.data)}")

    def run_burst(self, cfg: BusConfig, result: CaseResult, count: int = 100):
        self.flush()
        length = 64 if cfg.is_fd else 8
        fmt = 1 if cfg.is_fd else 0
        result.burst_expected = count
        self.send_cmd([CMD_BURST, count & 0xFF, count >> 8, length, fmt])
        seen = set()
        bad = 0
        done = None
        t_end = time.time() + 5.0
        while time.time() < t_end and done is None:
            rx = self.bus.recv(timeout=0.05)
            if rx is None:
                continue
            if rx.arbitration_id == ID_BURST and rx.is_extended_id:
                d = bytes(rx.data)
                seq = d[0] | (d[1] << 8)
                ok = (len(d) == length and rx.is_fd == cfg.is_fd and
                      all(d[i] == ((i ^ seq) & 0xFF) for i in range(2, length)))
                if ok:
                    seen.add(seq)
                else:
                    bad += 1
            elif rx.arbitration_id == ID_RESP and len(rx.data) >= 6 and rx.data[0] == (CMD_BURST | RESP_FLAG):
                done = bytes(rx.data)
        # Drain frames that may still follow the response
        t_end = time.time() + 0.1
        while time.time() < t_end:
            rx = self.bus.recv(timeout=0.02)
            if rx is not None and rx.arbitration_id == ID_BURST:
                seen.add(rx.data[0] | (rx.data[1] << 8))
        result.burst_ok = len(seen)
        if done is None:
            result.notes.append("burst: no completion response")
        elif bad:
            result.notes.append(f"burst: {bad} corrupted frames")

    def run_loop(self, cfg: BusConfig, result: CaseResult, count: int = LOOP_FRAMES):
        """Bus under test -> partner -> bus under test loop inside the node, PC adapter closed (node-only reference)."""
        length = 64 if cfg.is_fd else 8
        if self.skip_loop:
            result.loop_available = False
            return
        self.reset_stats()
        self.flush()
        self.send_cmd([CMD_LOOP, count & 0xFF, count >> 8, length, 1 if cfg.is_fd else 0])
        ack = self.wait_resp(CMD_LOOP, 0.15)
        self.close()
        if not ack or ack[0][1] != STATUS_OK:
            # Firmware built without CAN_FD_TEST_LOOP: no loop partner
            result.loop_available = False
            self.open(cfg)
            return
        # The test task serves commands sequentially, so the first GET_STATS answer arrives after the loop
        time.sleep(SWITCH_WAIT_S + count * 0.012)
        self.open(cfg)
        stats = {}
        t_end = time.time() + 10.0 + count * 0.02
        while not stats.get("loop_sent") and time.time() < t_end:
            stats = self.get_stats()
        result.loop_stats = stats
        result.loop_sent = stats.get("loop_sent", 0)
        result.loop_ok = stats.get("loop_ok", 0)
        result.loop_bad = stats.get("loop_bad", 0)

    def run_discovery(self) -> bool:
        try:
            devices = can_updater.scan_nodes(self.bus, timeout=0.8)
        except Exception as e:
            print(f"    discovery error: {e}")
            return False
        return len(devices) > 0

    # --- cases -------------------------------------------------------------------
    def run_case(self, case: TestCase) -> CaseResult:
        result = CaseResult(case)
        t0 = time.time()
        ok, note = self.switch_to(case.config)
        if not ok:
            result.notes.append(f"switch failed: {note}")
            if note not in ("rejected by node", "no SET_CONFIG acknowledge"):
                result.notes.append("recovered via fallback" if self.recover() else "RECOVERY FAILED")
            result.duration_s = time.time() - t0
            return result
        result.switched = True

        # 1) Node-only reference: loop between the node's buses, PC adapter closed
        self.run_loop(case.config, result)
        ls = result.loop_stats
        if result.loop_available:
            result.node_passed = (result.loop_sent == LOOP_FRAMES and result.loop_ok == LOOP_FRAMES and
                                  result.loop_bad == 0 and ls.get("bus_errors", 1) == 0)
        if result.node_passed is False:
            result.notes.append(f"loop: {result.loop_ok}/{result.loop_sent} ok, bus errors "
                                f"{ls.get('bus_errors', '?')}, arb {ls.get('lec_hist')}, data {ls.get('dlec_hist')}")
            if result.loop_ok == 0 and result.loop_bad == 0:
                result.notes.append("loop: no partner response - node buses on separate segments? (use --skip-loop)")

        # 2) Interoperability with the PC adapter: echo, node burst, OTA discovery
        self.reset_stats()
        result.stats_before = self.get_stats()
        self.run_echo(case.config, result)
        self.run_burst(case.config, result)
        result.stats_after = self.get_stats()
        # OTA discovery is answered by ota_manager on CAN1 only (mixed Classic/FD traffic is covered by echo)
        if case.config.is_fd and self.node_bus == 1:
            result.discovery = self.run_discovery()

        after = result.stats_after
        result.pc_counters = self.adapter_counters()
        # bus_errors counts every TEC/REC increment of the node (ECR.CEL), it never decreases, so
        # errors hidden by automatic retransmission are still visible
        health_ok = (bool(after) and after.get("bus_errors", 1) == 0 and not after.get("bus_off") and
                     after.get("tx_error", 1) == 0 and after.get("echo_drop", 1) == 0)
        if not health_ok:
            result.notes.append(f"PC traffic bus errors: {after.get('bus_errors', '?')}, arb {after.get('lec_hist')}, "
                                f"data {after.get('dlec_hist')}, TX rejected {after.get('tx_error', '?')}")
        result.pc_passed = (result.echo_ok == result.echo_sent and result.burst_ok == result.burst_expected and
                            health_ok and result.discovery in (None, True))
        result.passed = result.pc_passed and result.node_passed in (None, True)
        result.duration_s = time.time() - t0
        return result

    def run_negative(self) -> List[Tuple[str, bool, str]]:
        """Negative / robustness tests, all started and finished in Classic 500 kbps."""
        out = []

        # N1: data bit rate not reachable with the 144 MHz FDCAN clock (5 Mbps)
        ok, note = self.switch_to(DEFAULT_CONFIG)
        if not ok:
            out.append(("N0 return to Classic 500k", False, note))
            return out
        cfg = BusConfig(MODE_FD_BRS, 500_000, 5_000_000)
        switched, note = self.switch_to(cfg)
        still = self.ping()
        passed = (not switched and note == "rejected by node" and still is not None and still.mode == MODE_CLASSIC)
        out.append(("N1 FD+BRS 500k/5M rejected, previous config kept", passed,
                    f"switched={switched} ({note}); node after: {still.label() if still else 'no response'}"))

        # N2: FD frame transmit on a Classic CAN bus must be refused by the port, Classic still works
        self.flush()
        self.send_cmd([CMD_TX_PROBE, 1, 16])
        fd_probe = self.wait_resp(CMD_TX_PROBE, 0.5)
        self.send_cmd([CMD_TX_PROBE, 0, 8])
        cl_probe = self.wait_resp(CMD_TX_PROBE, 0.5)
        passed = (bool(fd_probe) and fd_probe[0][1] == STATUS_TX_FAILED and
                  bool(cl_probe) and cl_probe[0][1] == STATUS_OK)
        out.append(("N2 FD TX refused on Classic bus, Classic TX accepted", passed,
                    f"FD probe status={fd_probe[0][1] if fd_probe else 'none'}, "
                    f"Classic probe status={cl_probe[0][1] if cl_probe else 'none'}"))

        # N3: unconfirmed configuration falls back to Classic 500 kbps
        accepted, note = self.set_config(BusConfig(MODE_FD_BRS, 500_000, 2_000_000))
        time.sleep(CONFIRM_TIMEOUT_S + 1.5)
        self.open(DEFAULT_CONFIG)
        after = self.ping()
        passed = accepted and after is not None and after.mode == MODE_CLASSIC and after.nominal == 500_000
        out.append(("N3 unconfirmed switch falls back to Classic 500k", passed,
                    f"switch accepted={accepted}; node after {CONFIRM_TIMEOUT_S:.0f}s: "
                    f"{after.label() if after else 'no response'}"))
        self.current = DEFAULT_CONFIG
        return out


# ==============================================================================
# Report
# ==============================================================================
def node_timing(cfg: BusConfig, clock: int = 144_000_000) -> str:
    """Node bit timing (prescaler/TSEG1/TSEG2) as calculated by stm32c5_calc_bit_timing()."""
    n, d = calc_config_timing(clock, cfg.nominal, cfg.data if cfg.brs else None)
    if n is None:
        return "not reachable"
    text = f"N {n[0]}/{n[1]}/{n[2]}"
    if d:
        text += f", D {d[0]}/{d[1]}/{d[2]}"
        if d[0] <= 2:
            text += f", TDCO {d[0] * d[1]}"
    return text


def _hist(h: Optional[Dict]) -> str:
    return ", ".join(f"{k} {v}" for k, v in h.items()) if h else "–"


def write_report(path: str, results: List[CaseResult], negatives, uart: UartLogger, t_start: float,
                 args, adapter_info: str):
    bus, partner = args.bus, (2 if args.bus == 1 else 1)
    loop_label = f"CAN{bus}→CAN{partner}→CAN{bus}"
    passed = sum(r.passed for r in results) + sum(n[1] for n in negatives)
    functional = sum(1 for r in results if r.switched and r.echo_ok == r.echo_sent and
                     r.burst_ok == r.burst_expected and r.discovery in (None, True))
    total = len(results) + len(negatives)
    with_loop = any(r.loop_available and r.switched for r in results)
    lines = []
    lines.append("# Raport testu CAN FD")
    lines.append("")
    lines.append(f"- **Data:** {datetime.datetime.now().strftime('%Y-%m-%d %H:%M')}")
    lines.append(f"- **Wynik (bez błędów protokołu):** {passed}/{total} PASS")
    lines.append(f"- **Wynik funkcjonalny (wszystkie ramki dostarczone poprawnie):** {functional}/{len(results)}")
    lines.append(f"- **Węzeł:** NUCLEO-C542RC, zegar FDCAN 144 MHz (PSI z HSE 24 MHz)")
    lines.append(f"- **Magistrala testowana:** CAN{bus} (FDCAN{bus})"
                 + ("" if bus == 1 else "; OTA discovery pominięte (ota_manager działa na CAN1)"))
    lines.append(f"- **Adapter PC:** {adapter_info}")
    no_loop = "pominięta (--skip-loop)" if args.skip_loop else "nie (firmware bez CAN_FD_TEST_LOOP)"
    lines.append(f"- **Pętla {loop_label}:** {'tak' if with_loop else no_loop}")
    lines.append(f"- **Powtórzenia echo:** {args.reps} (każde: wszystkie długości DLC × 6 wzorców × ID std/ext)")
    lines.append(f"- **Czas testu:** {time.time() - t_start:.0f} s")
    lines.append("")
    lines.append("## Matryca prędkości")
    lines.append("")
    head = "| Przypadek | Konfiguracja | Timing węzła (presc/TSEG1/TSEG2) |"
    sep = "|---|---|---|"
    if with_loop:
        head += f" Pętla {loop_label} | Błędy pętli |"
        sep += "---|---|"
    head += " Echo PC OK/wysł. | Burst | OTA | Błędy (CEL) | Typy błędów arb. / dane | TDCV | Wynik |"
    sep += "---|---|---|---|---|---|---|"
    lines.append(head)
    lines.append(sep)
    for r in results:
        a = r.stats_after
        row = f"| `{r.case.name}` | {r.case.config.label()} | {node_timing(r.case.config)} |"
        if with_loop:
            ls = r.loop_stats
            row += (f" {r.loop_ok}/{r.loop_sent} | {ls.get('bus_errors', '–')} |" if r.loop_available and r.switched
                    else " – | – |")
        disc = "–" if r.discovery is None else ("OK" if r.discovery else "BRAK")
        echo = f"{r.echo_ok}/{r.echo_sent}" if r.switched else "–"
        burst = f"{r.burst_ok}/{r.burst_expected}" if r.switched else "–"
        errs = a.get("bus_errors", "–") if a else "–"
        types = f"{_hist(a.get('lec_hist'))} / {_hist(a.get('dlec_hist'))}" if a else "–"
        tdcv = str(a.get("tdcv", "–")) if (a and r.case.config.brs) else "–"
        row += (f" {echo} | {burst} | {disc} | {errs} | {types} | {tdcv} | "
                f"{'**PASS**' if r.passed else '**FAIL**'} |")
        lines.append(row)
    lines.append("")
    lines.append("## Testy negatywne i odporności")
    lines.append("")
    lines.append("| Test | Wynik | Szczegóły |")
    lines.append("|---|---|---|")
    for name, ok, details in negatives:
        lines.append(f"| {name} | {'**PASS**' if ok else '**FAIL**'} | {details} |")
    lines.append("")
    failed = [r for r in results if r.notes]
    if failed:
        lines.append("## Uwagi do przypadków")
        lines.append("")
        for r in failed:
            lines.append(f"- `{r.case.name}`: " + "; ".join(r.notes))
        lines.append("")
    lines.append("## Kryteria")
    lines.append("")
    lines.append("- **Funkcjonalnie:** echo – każda ramka wraca z ID+1, tym samym formatem (Classic/FD), flagą BRS,"
                 " długością i danymi (na magistrali FD wysyłane są też ramki Classic); burst – 100 ramek węzła"
                 " (64 B FD / 8 B Classic) bez strat; dla FD działa OTA discovery (ramki Classic na magistrali FD).")
    lines.append("- **PASS (bez błędów):** dodatkowo 0 błędów protokołu w węźle (licznik CEL – każdy wzrost TEC/REC,"
                 " nie maleje, więc widać też błędy ukryte przez automatyczną retransmisję), brak bus-off,"
                 f" brak odrzuconych ramek TX; z pętlą – również 200/200 ramek {loop_label} bez błędów.")
    lines.append("- **Typy błędów:** liczone w przerwaniu PEA/PED (STUFF, FORM, ACK, BIT1, BIT0, CRC)."
                 " TDCV: zmierzone położenie SSP (opóźnienie transceivera + TDCO) w okresach zegara FDCAN (6.94 ns).")
    lines.append("")
    if uart.port:
        lines.append("## Log UART węzła (bez cyklicznych logów IO/MAIN)")
        lines.append("")
        lines.append("```text")
        lines.extend([l for l in uart.since(t_start) if "ERROR sending" not in l][-120:])
        lines.append("```")
        lines.append("")
    lines.append("## Jak powtórzyć test")
    lines.append("")
    lines.append("Procedura opisana w [can_fd_test_guide.md](can_fd_test_guide.md).")
    lines.append("")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description="V-Box CAN FD bus test runner")
    parser.add_argument("--interface", default="candle", help="CAN adapter interface (FD requires 'candle')")
    parser.add_argument("--channel", default=None, help="Adapter channel, e.g. candle:0:0")
    parser.add_argument("--uart", default=None, help="Node UART port for log capture, e.g. COM22")
    parser.add_argument("--reps", type=int, default=10, help="Echo repetitions per length/pattern/ID set")
    parser.add_argument("--cases", default="all", help="Comma separated case names or 'all'")
    parser.add_argument("--skip-negative", action="store_true", help="Skip negative / robustness tests")
    parser.add_argument("--skip-loop", action="store_true",
                        help="Skip the loop between the node buses (CAN1 and CAN2 not on the same physical bus)")
    parser.add_argument("--bus", type=int, choices=(1, 2), default=1,
                        help="Node bus under test: 1 = CAN1, 2 = CAN2 (needs CAN_FD_TEST_LOOP=ON firmware)")
    parser.add_argument("--report", default=None,
                        help="Markdown report path (default docs/can_bus/can_fd_test_report[_can2].md)")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    if args.report is None:
        suffix = "" if args.bus == 1 else f"_can{args.bus}"
        args.report = f"docs/can_bus/can_fd_test_report{suffix}.md"

    matrix = build_matrix()
    if args.cases != "all":
        wanted = {c.strip() for c in args.cases.split(",")}
        matrix = [c for c in matrix if c.name in wanted]

    devices = can_updater.list_candle_devices()
    adapter_info = devices[0][1] if devices else args.interface

    uart = UartLogger(args.uart)
    uart.start()
    tester = FdTester(args.interface, args.channel, args.reps, args.verbose, args.skip_loop)
    t_start = time.time()
    results: List[CaseResult] = []
    negatives = []
    try:
        tester.open(DEFAULT_CONFIG)
        node, note = tester.connect(args.bus)
        if node is None:
            # The node may still run an unconfirmed configuration: wait for its Classic 500 kbps fallback
            print(f"[WARN] CAN{args.bus} does not answer on Classic 500 kbps, waiting for node fallback...")
            tester.close()
            time.sleep(CONFIRM_TIMEOUT_S + 1.5)
            tester.open(DEFAULT_CONFIG)
            node, note = tester.connect(args.bus)
        if node is None:
            print(f"[ERROR] CAN{args.bus}: {note}")
            return 2
        tester.current = node
        print(f"[INFO] Bus under test: CAN{tester.node_bus}, active configuration: {node.label()}")

        for case in matrix:
            print(f"[CASE] {case.name:<18} {case.config.label():<22}", end="", flush=True)
            r = tester.run_case(case)
            results.append(r)
            loop = (f"loop {r.loop_ok}/{r.loop_sent} err {r.loop_stats.get('bus_errors', '?')} | "
                    if r.loop_available else "")
            print(f" {loop}echo {r.echo_ok}/{r.echo_sent} burst {r.burst_ok}/{r.burst_expected} "
                  f"err {r.stats_after.get('bus_errors', '?')} "
                  f"{'PASS' if r.passed else 'FAIL'} ({r.duration_s:.1f}s) {'; '.join(r.notes)}")

        if not args.skip_negative:
            negatives = tester.run_negative()
            for name, ok, details in negatives:
                print(f"[NEG ] {name:<55} {'PASS' if ok else 'FAIL'}  {details}")

        ok, note = tester.switch_to(DEFAULT_CONFIG)
        print(f"[INFO] Node returned to Classic 500k: {'OK' if ok else note}")
        if ok and tester.node_bus != 1:
            ok, note = tester.select_bus(1)
            print(f"[INFO] Node bus under test back to CAN1: {'OK' if ok else note}")
    finally:
        tester.close()
        time.sleep(0.3)
        uart.stop()

    write_report(args.report, results, negatives, uart, t_start, args, adapter_info)
    total = len(results) + len(negatives)
    passed = sum(r.passed for r in results) + sum(n[1] for n in negatives)
    print(f"[DONE] {passed}/{total} PASS, report: {args.report}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
