#!/usr/bin/env python3
"""
STM32 CAN Bus Firmware Updater & Diagnostic Tool (v-box ecosystem).
Supports:
  - Bus Scanning (Discovery by 96-bit MCU UID)
  - ISO 15765-2 (ISO-TP) multi-frame transport layer over Classic CAN (8-byte frames)
  - Dummy Firmware Update with live progress bar and CRC32 verification
  - Real Firmware Flashing (.bin files)

Usage Examples:
  # Scan bus for connected STM32 devices:
  python tools/can_updater.py --port COM5 --scan

  # Run Dummy Update (32 KB test stream) to verify bus and ISO-TP stack:
  python tools/can_updater.py --port COM5 --dummy --size 32768

  # Run Dummy Update targeting a specific device by UID:
  python tools/can_updater.py --port COM5 --uid 0038002A-3132510F-39383531 --dummy

  # Flash real firmware binary:
  python tools/can_updater.py --port COM5 --file build/debug_GCC_NUCLEO-C542RC/v_box.bin
"""

import sys
import os
import time
import zlib
import struct
import argparse
from typing import Optional, List, Tuple, Dict, Callable

# On Windows, raise timer resolution to 1ms so time.sleep(0.001..0.002) is accurate
if sys.platform == "win32":
    try:
        import ctypes
        ctypes.windll.winmm.timeBeginPeriod(1)
    except Exception:
        pass

# ==============================================================================
# Protocol Callbacks for GUI & Automation Integration
# ==============================================================================
# progress_cb(current_bytes: int, total_bytes: int, speed_kbps: float, eta_sec: float, retries: int)
ProgressCallback = Optional[Callable[[int, int, float, float, int], None]]
# step_cb(step_index: int, total_steps: int, step_title: str)
StepCallback = Optional[Callable[[int, int, str], None]]
# log_cb(level: str, message: str)  # level in {"INFO", "OK", "WARN", "ERROR", "RECOVERY"}
LogCallback = Optional[Callable[[str, str], None]]
# cancel_cb() -> bool  # returns True if user requested abort
CancelCallback = Optional[Callable[[], bool]]

try:
    import can
except ImportError:
    print("[ERROR] python-can is not installed. Run: pip install -r tools/requirements_ota.txt", file=sys.stderr)
    sys.exit(1)

try:
    import candle_api
    CANDLE_API_AVAILABLE = True
except Exception:
    candle_api = None
    CANDLE_API_AVAILABLE = False

try:
    from candle import CandleBus
    CANDLE_BUS_AVAILABLE = True
except Exception:
    CandleBus = None
    CANDLE_BUS_AVAILABLE = False


# ==============================================================================
# CAN Adapter Abstraction (candleLight WinUSB / gs_usb / SLCAN / SocketCAN)
# ==============================================================================
SUPPORTED_CAN_INTERFACES = ("candle", "slcan", "gs_usb", "socketcan")


def _init_gs_usb_backend() -> bool:
    """Initializes libusb-1.0 backend via libusb_package and patches GsUsbBus.__str__ on Windows."""
    try:
        import libusb_package
        import usb.backend.libusb1
        _backend = libusb_package.get_libusb1_backend()
        if _backend is not None:
            usb.backend.libusb1.get_backend = lambda **kwargs: _backend
        from can.interfaces.gs_usb import GsUsbBus
        GsUsbBus.__str__ = lambda self: str(getattr(self, "channel_info", "gs_usb"))
        return True
    except Exception:
        return False


def list_candle_devices() -> List[Tuple[str, str]]:
    """
    Enumerates connected candleLight USB CAN adapters.
    Returns a list of (channel_id, display_label) tuples, e.g.:
      [('candle:0:0', 'candle:0:0 — Candlelight 2.5 - Multiboard (SN: 2074326D554850142)')]
    """
    if CANDLE_API_AVAILABLE and candle_api is not None:
        try:
            devices = list(candle_api.list_device())
            result: List[Tuple[str, str]] = []
            for dev_idx, dev in enumerate(devices):
                sn = getattr(dev, "serial_number", "") or f"DEV{dev_idx}"
                prod = getattr(dev, "product", "") or "candleLight"
                ch_count = getattr(dev, "channel_count", 1) or 1
                for ch_idx in range(ch_count):
                    ch_id = f"candle:{dev_idx}:{ch_idx}"
                    label = f"{ch_id} — {prod} (SN: {sn})"
                    result.append((ch_id, label))
            if result:
                return result
        except Exception:
            pass

    # Fallback USB check via pyusb + libusb_package for gs_usb (VID=0x1D50, PID=0x606F)
    try:
        import usb.core
        _init_gs_usb_backend()
        usb_devs = list(usb.core.find(find_all=True, idVendor=0x1D50, idProduct=0x606F) or [])
        result_usb: List[Tuple[str, str]] = []
        for idx, udev in enumerate(usb_devs):
            try:
                prod = udev.product or "candleLight (gs_usb)"
            except Exception:
                prod = "candleLight (gs_usb)"
            try:
                sn = udev.serial_number or f"USB{idx}"
            except Exception:
                sn = f"USB{idx}"
            ch_id = f"candle:{idx}:0"
            result_usb.append((ch_id, f"{ch_id} — {prod} (SN: {sn})"))
        return result_usb
    except Exception:
        return []


def parse_candle_channel_index(channel: Optional[str]) -> int:
    """Extracts integer channel index (default 0) from 'candle:0:0' or '0'."""
    if channel is None:
        return 0
    s = str(channel).strip()
    if s.startswith("candle:"):
        parts = s.split(":")
        if len(parts) >= 3 and parts[2].isdigit():
            return int(parts[2])
    elif s.isdigit():
        return int(s)
    return 0


def resolve_can_interface_and_channel(interface: Optional[str], channel: Optional[str]) -> Tuple[str, str]:
    """
    Resolves (interface, channel) with smart auto-detection:
      - If interface is 'auto' or None:
          * If channel starts with 'COM' or '/dev/tty' -> ('slcan', channel)
          * If channel starts with 'can' or 'vcan' (and not 'candle') -> ('socketcan', channel)
          * If candleLight USB device is detected -> ('candle', 'candle:0:0')
          * Otherwise defaults to ('candle', 'candle:0:0') if candle_api is available, else ('slcan', channel or '')
      - Normalizes 'candlelight' alias to 'candle'.
    """
    iface = (interface or "auto").strip().lower()
    if iface == "candlelight":
        iface = "candle"

    ch = (channel or "").strip()

    if iface == "auto":
        if ch:
            ch_upper = ch.upper()
            if ch_upper.startswith("COM") or ch.startswith("/dev/tty"):
                return "slcan", ch
            if ch_upper.startswith("CANDLE") or ch.isdigit():
                return "candle", (ch if ch_upper.startswith("CANDLE:") else f"candle:0:{ch}")
            if ch.startswith("can") or ch.startswith("vcan"):
                return "socketcan", ch
        candle_devs = list_candle_devices()
        if candle_devs:
            return "candle", candle_devs[0][0]
        if CANDLE_BUS_AVAILABLE:
            return "candle", "candle:0:0"
        return "slcan", ch

    if iface in ("candle", "gs_usb"):
        if not ch:
            candle_devs = list_candle_devices()
            ch = candle_devs[0][0] if candle_devs else "candle:0:0"
        elif ch.isdigit():
            ch = f"candle:0:{ch}"
        return iface, ch

    if iface == "socketcan" and not ch:
        return "socketcan", "can0"

    return iface, ch


def _wrap_candle_bus_safe_shutdown(bus: can.BusABC) -> can.BusABC:
    """
    Wraps CandleBus/gs_usb shutdown to make it idempotent and exception-safe.
    Prevents 'CandleBus was not properly shut down' and 'Exception ignored in BusABC.__del__'
    during Python interpreter teardown or garbage collection.
    """
    if bus is None:
        return bus

    orig_shutdown = getattr(bus, "shutdown", None)
    if not orig_shutdown or getattr(bus, "_safe_shutdown_wrapped", False):
        return bus

    def safe_shutdown():
        if getattr(bus, "_is_shutdown", False):
            return
        bus._is_shutdown = True

        channels = getattr(bus, "_channels", None)
        if isinstance(channels, dict):
            for ch in list(channels.values()):
                try:
                    ch.reset()
                except Exception:
                    pass
            # Clear _channels before calling orig_shutdown so CandleBus.shutdown's
            # unshielded [ch.reset()] list comprehension does not re-raise during exit
            bus._channels = {}

        try:
            orig_shutdown()
        except Exception:
            pass

    bus.shutdown = safe_shutdown
    bus._safe_shutdown_wrapped = True

    # Also patch __del__ on the bus class if not already patched
    bus_cls = type(bus)
    if not getattr(bus_cls, "_safe_del_patched", False):
        def safe_del(self):
            try:
                if not getattr(self, "_is_shutdown", False):
                    self.shutdown()
            except Exception:
                pass

        bus_cls.__del__ = safe_del
        bus_cls._safe_del_patched = True

    return bus


def open_can_bus(interface: str,
                 channel: Optional[str] = None,
                 bitrate: int = 500000,
                 listen_only: bool = False) -> can.BusABC:
    """
    Unified factory for opening CAN bus adapters across candleLight (WinUSB / gs_usb), SLCAN, and SocketCAN.
    """
    iface, resolved_ch = resolve_can_interface_and_channel(interface, channel)

    if iface == "candle":
        ch_idx = parse_candle_channel_index(resolved_ch)
        last_err = None
        if CANDLE_BUS_AVAILABLE:
            try:
                bus = can.Bus(
                    interface="candle",
                    channel=ch_idx,
                    bitrate=int(bitrate),
                    sample_point=80.0,
                    fd=False,
                    listen_only=bool(listen_only),
                )
                return _wrap_candle_bus_safe_shutdown(bus)
            except Exception as e:
                last_err = e
        # Fallback to gs_usb if python-can-candle is not installed or failed
        try:
            _init_gs_usb_backend()
            bus = can.Bus(
                interface="gs_usb",
                channel=ch_idx,
                bitrate=int(bitrate),
            )
            return _wrap_candle_bus_safe_shutdown(bus)
        except Exception as e_gs:
            err_msg = str(last_err or e_gs)
            raise RuntimeError(
                f"{err_msg} (Upewnij się, że adapter candleLight nie jest zajęty przez inny program, np. Serial Commander)"
            ) from (last_err or e_gs)

    if iface == "gs_usb":
        ch_idx = parse_candle_channel_index(resolved_ch)
        _init_gs_usb_backend()
        try:
            bus = can.Bus(
                interface="gs_usb",
                channel=ch_idx,
                bitrate=int(bitrate),
            )
            return _wrap_candle_bus_safe_shutdown(bus)
        except Exception as e_gs:
            raise RuntimeError(
                f"{e_gs} (Upewnij się, że adapter candleLight nie jest zajęty przez inny program, np. Serial Commander)"
            ) from e_gs

    if iface == "slcan":
        if not resolved_ch:
            raise ValueError("Port COM jest wymagany dla interfejsu 'slcan' (np. --port COM5)")
        return can.interface.Bus(
            interface="slcan",
            channel=resolved_ch,
            bitrate=int(bitrate),
            listen_only=bool(listen_only),
        )

    return can.interface.Bus(
        interface=iface,
        channel=resolved_ch,
        bitrate=int(bitrate),
    )

# ==============================================================================
# Protocol Constants (aligned with app/ota_update/ota_protocol.h)
# 29-bit Extended CAN IDs in proprietary V-Box namespace (0x01F2xxxx)
# ==============================================================================
CAN_ID_OTA_BROADCAST           = 0x01F2F3DF  # PC -> All Nodes (29-bit Extended Broadcast)
CAN_ID_OTA_DISCOVERY_RESP_BASE = 0x01F200E9  # Base Extended ID for node-addressed discovery response
CAN_ID_OTA_DISCOVERY_RESP_MASK = 0x1FFF00FF  # Discovery Response Match Mask
CAN_ID_OTA_DISCOVERY_RESP      = 0x01F2F3E9  # Legacy Discovery Response (hash=0xF3)
CAN_ID_ISOTP_REQ               = 0x01F2F3E0  # PC -> Selected Node (29-bit Extended ISO-TP Requests)
CAN_ID_ISOTP_RESP              = 0x01F2F3E8  # Selected Node -> PC (29-bit Extended ISO-TP Responses)

OTA_MAGIC_TOKEN             = [0x56, 0x42, 0x4F, 0x58]  # ASCII "VBOX" authorization token

CMD_DISCOVER_NODES          = 0xA0
CMD_SELECT_TARGET           = 0xA1
CMD_ENTER_BOOTLOADER        = 0xA2
CMD_VERIFY_FIRMWARE         = 0xA3
CMD_UPDATE_BOOTLOADER       = 0xA4
CMD_GO_EXECUTE              = 0x21
CMD_WRITE_MEMORY            = 0x31
CMD_ERASE_APP               = 0x44

BOOTLOADER_STAGING_BASE     = 0x08038000
BOOTLOADER_MAX_SIZE         = 32768

OTA_RESP_ACK                = 0x79
OTA_RESP_NACK               = 0x1F

OTA_MODE_BOOTLOADER         = 0x00
OTA_MODE_APPLICATION        = 0x01
OTA_MODE_RECOVERY           = 0x02

OTA_MODE_NAMES = {
    OTA_MODE_BOOTLOADER: "BOOTLOADER",
    OTA_MODE_APPLICATION: "APPLICATION",
    OTA_MODE_RECOVERY: "RECOVERY_MODE"
}

OTA_ERR_NAMES = {
    0x00: "No Error / Brak błędu",
    0x01: "CRC32 Checksum Mismatch / Błąd sumy kontrolnej",
    0x02: "Flash Address Out of Range / Adres poza zakresem partycji",
    0x03: "16-Byte Alignment Error / Błąd wyrównania 16-bajtowego",
    0x04: "Flash Hardware / Vector Table Error / Błąd sprzętowy pamięci Flash lub wektorów",
    0x05: "Protocol Sequence Error / Błąd sekwencji protokołu",
    0x06: "Command Disallowed in Current Mode / Polecenie niedozwolone w tym trybie"
}

def decode_nack(resp: Optional[bytes]) -> str:
    """Decodes OTA NACK error code into human-readable description."""
    if not resp or len(resp) == 0:
        return "Timeout (Brak odpowiedzi)"
    if resp[0] == OTA_RESP_NACK:
        code = resp[1] if len(resp) > 1 else 0x00
        desc = OTA_ERR_NAMES.get(code, f"Nieznany błąd (0x{code:02X})")
        return f"NACK (0x{code:02X}: {desc})"
    return f"Nieoczekiwana odpowiedź (0x{resp.hex()})"


# ==============================================================================
# ISO 15765-2 (ISO-TP) Transport Layer Client
# ==============================================================================

# Maximum OTA write block size accepted by the firmware (OTA_MAX_BLOCK_SIZE in app/ota_update/ota_protocol.h)
MAX_BLOCK_SIZE = 1024
DEFAULT_BLOCK_SIZE = MAX_BLOCK_SIZE

# Below this separation time a busy-wait is used instead of time.sleep() (Windows sleep granularity ~1-15 ms)
_BUSY_WAIT_THRESHOLD_S = 0.002

# How long a single frame transmission may be retried when the USB-CAN adapter TX buffer is full
_TX_RETRY_WINDOW_S = 0.1


def stmin_to_seconds(st_min: int) -> float:
    """Decodes an ISO 15765-2 STmin byte: 0x00-0x7F = 0-127 ms, 0xF1-0xF9 = 100-900 us, reserved = 127 ms."""
    if st_min <= 0x7F:
        return st_min / 1000.0
    if 0xF1 <= st_min <= 0xF9:
        return (st_min - 0xF0) * 100e-6
    return 0.127


def _wait_until(t_target: float) -> None:
    """Waits until the perf_counter reaches t_target (sleeps for long waits, busy-waits for sub-2 ms precision)."""
    remaining = t_target - time.perf_counter()
    if remaining > _BUSY_WAIT_THRESHOLD_S:
        time.sleep(remaining - _BUSY_WAIT_THRESHOLD_S)
    while time.perf_counter() < t_target:
        pass


class IsoTpClient:
    """
    Lightweight, pure-python ISO 15765-2 endpoint operating over standard 8-byte CAN frames.
    Handles Single Frame (SF), First Frame (FF), Consecutive Frame (CF), and Flow Control (FC).
    """
    def __init__(self, bus: can.BusABC, tx_id: int = CAN_ID_ISOTP_REQ, rx_id: int = CAN_ID_ISOTP_RESP, verbose: bool = False):
        self.bus = bus
        self.tx_id = tx_id
        self.rx_id = rx_id
        self.verbose = verbose

    def _send_frame(self, msg: can.Message) -> None:
        """Sends one CAN frame, retrying briefly if the adapter TX buffer is temporarily full."""
        t_deadline = time.perf_counter() + _TX_RETRY_WINDOW_S
        while True:
            try:
                self.bus.send(msg)
                return
            except can.CanError:
                if time.perf_counter() >= t_deadline:
                    raise
                time.sleep(0.0005)

    def _wait_flow_control(self, timeout: float) -> Optional[Tuple[int, int]]:
        """Waits for a Flow Control CTS frame and returns (block_size, st_min), or None on timeout/overflow."""
        t_deadline = time.time() + timeout
        while time.time() < t_deadline:
            rx_msg = self.bus.recv(timeout=0.05)
            if not rx_msg:
                continue
            if rx_msg.is_extended_id and rx_msg.arbitration_id == self.rx_id and len(rx_msg.data) >= 3:
                if (rx_msg.data[0] & 0xF0) != 0x30:
                    continue
                fs = rx_msg.data[0] & 0x0F
                block_size = rx_msg.data[1]
                st_min = rx_msg.data[2]
                if self.verbose:
                    print(f"  [ISO-TP RX FC] FS={fs}, BS={block_size}, STmin=0x{st_min:02X} "
                          f"({stmin_to_seconds(st_min) * 1e6:.0f} us)")
                if fs == 0x00:  # Clear To Send (CTS)
                    return block_size, st_min
                if fs == 0x02:  # Overflow
                    print("  [ISO-TP ERROR] Receiver reported buffer overflow (FC Overflow)", file=sys.stderr)
                    return None
                # fs == 0x01 (Wait): keep waiting for the next FC within the same deadline
        return None

    def send_pdu(self, pdu: bytes, timeout: float = 1.0) -> bool:
        """Transmits an ISO-TP PDU (Single Frame if <= 7 bytes, Multi-Frame otherwise)."""
        length = len(pdu)
        if length == 0:
            return False

        if length <= 7:
            # Single Frame (SF)
            frame_data = [length] + list(pdu)
            frame_data += [0x00] * (8 - len(frame_data))
            msg = can.Message(arbitration_id=self.tx_id, is_extended_id=True, data=frame_data)
            if self.verbose:
                print(f"  [ISO-TP TX SF] {bytes(frame_data).hex()}")
            self._send_frame(msg)
            return True

        # Multi-Frame: First Frame (FF)
        ff_header = [0x10 | ((length >> 8) & 0x0F), length & 0xFF]
        frame_data = ff_header + list(pdu[:6])
        msg = can.Message(arbitration_id=self.tx_id, is_extended_id=True, data=frame_data)
        if self.verbose:
            print(f"  [ISO-TP TX FF] {bytes(frame_data).hex()} (len={length})")
        self._send_frame(msg)

        # Wait for Flow Control (FC) frame from receiver
        fc = self._wait_flow_control(timeout)
        if fc is None:
            if self.verbose:
                print("  [ISO-TP ERROR] No Flow Control (FC) CTS frame received", file=sys.stderr)
            return False
        block_size, st_min = fc
        st_delay = stmin_to_seconds(st_min)

        # Send Consecutive Frames (CF), honouring the receiver's STmin and Block Size
        offset = 6
        sn = 1
        cf_sent_in_block = 0
        t_next_tx = time.perf_counter()

        while offset < length:
            chunk = pdu[offset : offset + 7]
            cf_header = [0x20 | (sn & 0x0F)]
            cf_data = cf_header + list(chunk)
            cf_data += [0x00] * (8 - len(cf_data))

            msg = can.Message(arbitration_id=self.tx_id, is_extended_id=True, data=cf_data)
            if self.verbose:
                print(f"  [ISO-TP TX CF #{cf_sent_in_block+1} sn={sn}] {bytes(cf_data).hex()}")
            if st_delay > 0:
                _wait_until(t_next_tx)
            self._send_frame(msg)
            t_next_tx = time.perf_counter() + st_delay

            offset += len(chunk)
            sn = (sn + 1) & 0x0F
            cf_sent_in_block += 1

            # If receiver requested a limited Block Size (BS > 0), wait for the next Flow Control frame
            if block_size > 0 and cf_sent_in_block >= block_size and offset < length:
                cf_sent_in_block = 0
                fc = self._wait_flow_control(timeout)
                if fc is None:
                    print("  [ISO-TP ERROR] Timeout waiting for subsequent Flow Control (FC) frame", file=sys.stderr)
                    return False
                block_size, st_min = fc
                st_delay = stmin_to_seconds(st_min)

        return True

    def recv_pdu(self, timeout: float = 1.0) -> Optional[bytes]:
        """Receives an ISO-TP PDU from the bus."""
        t_deadline = time.time() + timeout

        while time.time() < t_deadline:
            rx_msg = self.bus.recv(timeout=0.05)
            if not rx_msg:
                continue
            if not rx_msg.is_extended_id or rx_msg.arbitration_id != self.rx_id or len(rx_msg.data) == 0:
                continue

            pci_type = rx_msg.data[0] & 0xF0

            if pci_type == 0x00:
                # Single Frame (SF)
                dl = rx_msg.data[0] & 0x0F
                if 1 <= dl <= 7 and len(rx_msg.data) >= (1 + dl):
                    pdu = bytes(rx_msg.data[1 : 1 + dl])
                    if self.verbose:
                        print(f"  [ISO-TP RX SF] {pdu.hex()}")
                    return pdu

            elif pci_type == 0x10:
                # First Frame (FF)
                total_len = ((rx_msg.data[0] & 0x0F) << 8) | rx_msg.data[1]
                buf = bytearray(rx_msg.data[2:8])
                if self.verbose:
                    print(f"  [ISO-TP RX FF] Total={total_len} bytes")

                # Send Flow Control (CTS, BS=0, STmin=1ms)
                fc_data = [0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]
                fc_msg = can.Message(arbitration_id=self.tx_id, is_extended_id=True, data=fc_data)
                self.bus.send(fc_msg)

                # Receive Consecutive Frames (CF)
                expected_sn = 1
                cf_deadline = time.time() + timeout
                while len(buf) < total_len and time.time() < cf_deadline:
                    cf_msg = self.bus.recv(timeout=0.05)
                    if not cf_msg:
                        continue
                    if cf_msg.is_extended_id and cf_msg.arbitration_id == self.rx_id and len(cf_msg.data) > 1:
                        if (cf_msg.data[0] & 0xF0) == 0x20:
                            sn = cf_msg.data[0] & 0x0F
                            if sn != expected_sn:
                                print(f"  [ISO-TP ERROR] Sequence mismatch (got {sn}, exp {expected_sn})", file=sys.stderr)
                                return None
                            rem = total_len - len(buf)
                            take = min(7, rem)
                            buf.extend(cf_msg.data[1 : 1 + take])
                            expected_sn = (expected_sn + 1) & 0x0F

                if len(buf) >= total_len:
                    return bytes(buf[:total_len])

        return None

# ==============================================================================
# High-Level OTA Operations
# ==============================================================================

def format_uid(uid_bytes: bytes) -> str:
    """Formats 12 bytes of raw hardware UID into human-readable 32-bit hex groups."""
    if len(uid_bytes) != 12:
        return uid_bytes.hex().upper()
    g1 = uid_bytes[0:4].hex().upper()
    g2 = uid_bytes[4:8].hex().upper()
    g3 = uid_bytes[8:12].hex().upper()
    return f"{g1}-{g2}-{g3}"

def compute_mcu_uid_hash(uid_bytes: bytes) -> int:
    """Computes XOR hash matching STM32 openbl_core_send_recovery_beacon (byte 3)."""
    h = 0
    for b in uid_bytes:
        h ^= b
    return h & 0xFF

def parse_uid(raw_uid: str) -> bytes:
    """Parses user-entered UID string into 12 raw bytes."""
    clean = raw_uid.replace("-", "").replace(" ", "").strip()
    if len(clean) != 24:
        raise ValueError(f"Invalid UID length: expected 24 hex characters (12 bytes), got '{raw_uid}'")
    return bytes.fromhex(clean)

def scan_nodes(bus: can.BusABC,
               timeout: float = 0.8,
               verbose: bool = False,
               log_cb: LogCallback = None,
               beacons_out: Optional[List[Dict]] = None) -> List[Dict]:
    """
    Broadcasts CMD_DISCOVER_NODES (0xA0) with "VBOX" magic token on 0x01F2F3DF and collects announcements.
    Matches node discovery responses using mask 0x1FFF00FF == 0x01F200E9 with non-destructive arbitration IDs.
    Computes 8-bit UID hash (XOR & 0xFF matching firmware beacon) and captures on-demand 0xEE CRASH_RECOVERY_BEACON frames.
    """
    scan_msg = "Scanning CAN bus (500 kbps) for STM32 devices..."
    print(f"\n[INFO] {scan_msg}")
    if log_cb:
        log_cb("INFO", scan_msg)

    # Clear pending RX queue (up to 50 frames to avoid getting stuck if bus has high traffic)
    for _ in range(50):
        if not bus.recv(timeout=0.005):
            break

    # Broadcast discovery frame: [0x06, 0xA0, 'V', 'B', 'O', 'X', 0x00, 0x00]
    disc_frame = [0x06, CMD_DISCOVER_NODES] + OTA_MAGIC_TOKEN + [0x00, 0x00]
    msg = can.Message(arbitration_id=CAN_ID_OTA_BROADCAST, is_extended_id=True, data=disc_frame)
    bus.send(msg)

    t_end = time.time() + timeout
    pending_f1: Dict[int, bytes] = {}
    devices = []
    seen_uids = set()

    while time.time() < t_end:
        rx = bus.recv(timeout=0.02)
        if not rx or rx.arbitration_id == CAN_ID_OTA_BROADCAST:
            continue
        if rx.is_extended_id and ((rx.arbitration_id & CAN_ID_OTA_DISCOVERY_RESP_MASK) == CAN_ID_OTA_DISCOVERY_RESP_BASE) and len(rx.data) >= 8:
            sub_idx = rx.data[0]
            node_can_id = rx.arbitration_id
            if verbose:
                print(f"  [RAW 0x{node_can_id:08X}] sub={sub_idx} payload={rx.data.hex()}")

            if sub_idx == 0x01:
                pending_f1[node_can_id] = bytes(rx.data[1:8])
            elif sub_idx == 0x02 and node_can_id in pending_f1:
                f1 = pending_f1.pop(node_can_id)
                f2 = bytes(rx.data[1:8])
                uid_bytes = f1[:7] + f2[:5]
                mode_byte = f2[5]
                ver_byte = f2[6]
                v_major = (ver_byte >> 4) & 0x0F
                v_minor = ver_byte & 0x0F

                if uid_bytes not in seen_uids:
                    seen_uids.add(uid_bytes)
                    mode_str = OTA_MODE_NAMES.get(mode_byte, f"UNKNOWN(0x{mode_byte:02X})")
                    uid_hash = compute_mcu_uid_hash(uid_bytes)
                    uid_crc = zlib.crc32(uid_bytes) & 0xFF
                    dev_entry = {
                        "uid": uid_bytes,
                        "uid_str": format_uid(uid_bytes),
                        "uid_hash": uid_hash,
                        "uid_crc_hash": uid_crc,
                        "mode": mode_str,
                        "mode_byte": mode_byte,
                        "version": f"v{v_major}.{v_minor}",
                        "can_id": node_can_id
                    }
                    devices.append(dev_entry)
                    if log_cb:
                        log_cb("INFO", f"Discovered node: UID={dev_entry['uid_str']} (Hash=0x{uid_hash:02X}) Mode={mode_str} FW={dev_entry['version']}")
            elif sub_idx == 0x03:
                # Frame 3: full 8-bit versions [AppMaj, AppMin, AppPatch, BootMaj, BootMin, BootPatch] (0xFF = unknown)
                dev_entry = next((d for d in reversed(devices) if d["can_id"] == node_can_id), None)
                if dev_entry is not None:
                    app_ver = tuple(rx.data[1:4])
                    boot_ver = tuple(rx.data[4:7])
                    if boot_ver != (0xFF, 0xFF, 0xFF):
                        dev_entry["boot_version"] = "v{}.{}.{}".format(*boot_ver)
                    if app_ver != (0xFF, 0xFF, 0xFF):
                        dev_entry["version"] = "v{}.{}.{}".format(*app_ver)
                    elif "boot_version" in dev_entry:
                        dev_entry["version"] = dev_entry["boot_version"]
            elif sub_idx == 0xEE:
                reason_code = rx.data[1]
                fail_cnt = rx.data[2]
                uid_hash = rx.data[3]
                pc = rx.data[4] | (rx.data[5] << 8) | (rx.data[6] << 16) | (rx.data[7] << 24)
                reason_map = {
                    0x00: "NONE",
                    0x01: "HARDFAULT",
                    0x02: "RTOS_ASSERT",
                    0x03: "WATCHDOG",
                    0x04: "NO_APP (Blank/Missing Flash)"
                }
                reason_str = reason_map.get(reason_code, f"UNKNOWN(0x{reason_code:02X})")
                beacon_dict = {
                    "reason_code": reason_code,
                    "reason_str": reason_str,
                    "fail_cnt": fail_cnt,
                    "uid_hash": uid_hash,
                    "pc": pc,
                    "timestamp": time.time(),
                    "can_id": node_can_id
                }
                if beacons_out is not None:
                    beacons_out.append(beacon_dict)
                rec_log = f"Device in RECOVERY mode: Reason={reason_str}, Crashes={fail_cnt}, PC=0x{pc:08X}, UID_hash=0x{uid_hash:02X}"
                print(f"  [RECOVERY BEACON 0xEE] {rec_log}")
                if log_cb:
                    log_cb("RECOVERY", rec_log)

    if not devices:
        print("  --> No STM32 devices responded on CAN bus.")
        if log_cb:
            log_cb("WARN", "No STM32 devices responded on CAN bus.")
    else:
        print(f"\n[OK] Found {len(devices)} device(s) on CAN bus:")
        for idx, dev in enumerate(devices, 1):
            print(f"  [{idx}] UID: {dev['uid_str']} (Hash: 0x{dev['uid_hash']:02X}) | Mode: {dev['mode']:<12} | "
                  f"FW: {dev['version']} | BL: {dev.get('boot_version', '?')}")
        print()
        if log_cb:
            log_cb("OK", f"Discovery finished: {len(devices)} device(s) active on CAN bus.")

    return devices

def print_progress_bar(current: int, total: int, start_time: float, prefix: str = ""):
    """Renders a dynamic live progress bar with percentage, speed, and ETA."""
    elapsed = max(0.001, time.time() - start_time)
    speed_kb = (current / 1024.0) / elapsed
    pct = (current / total) * 100.0 if total > 0 else 100.0
    bar_len = 30
    filled = int(round(bar_len * (current / float(total)))) if total > 0 else bar_len
    bar = "=" * filled + "-" * (bar_len - filled)

    rem_bytes = max(0, total - current)
    eta_sec = (rem_bytes / (speed_kb * 1024.0)) if speed_kb > 0 else 0.0

    sys.stdout.write(f"\r  {prefix} [{bar}] {pct:5.1f}% | {current}/{total} B | {speed_kb:5.1f} KB/s | ETA: {eta_sec:4.1f}s")
    sys.stdout.flush()

# ==============================================================================
# Firmware / Dummy Update Engine
# ==============================================================================

def run_update_session(bus: can.BusABC,
                       target_uid: bytes,
                       firmware_data: bytes,
                       app_address: int = 0x08008000,
                       block_size: int = DEFAULT_BLOCK_SIZE,
                       is_dummy: bool = True,
                       verbose: bool = False,
                       abort_after_blocks: Optional[int] = None,
                       force_crc: Optional[int] = None,
                       progress_cb: ProgressCallback = None,
                       step_cb: StepCallback = None,
                       log_cb: LogCallback = None,
                       cancel_cb: CancelCallback = None) -> bool:
    """
    Executes a complete update sequence (Target Select -> Erase -> Stream Blocks -> Verify CRC32 -> Execute).
    Supports fault-injection via abort_after_blocks and force_crc.
    Notifies caller via progress_cb, step_cb, log_cb, and supports early cancellation via cancel_cb.
    """
    def _log(level: str, msg: str):
        if log_cb:
            log_cb(level, msg)

    client = IsoTpClient(bus, tx_id=CAN_ID_ISOTP_REQ, rx_id=CAN_ID_ISOTP_RESP, verbose=verbose)
    uid_str = format_uid(target_uid)
    mode_label = "DUMMY UPDATE (Simulation)" if is_dummy else "FIRMWARE FLASH (Hardware Flash)"

    print("=" * 76)
    print(f"  Starting {mode_label}")
    print(f"  Target UID      : {uid_str}")
    print(f"  Flash Target    : 0x{app_address:08X}")
    print(f"  Payload Size    : {len(firmware_data)} bytes ({len(firmware_data)/1024.0:.1f} KB)")
    print(f"  Block Size      : {block_size} bytes")
    full_crc32 = force_crc if force_crc is not None else (zlib.crc32(firmware_data) & 0xFFFFFFFF)
    print(f"  Full CRC32      : 0x{full_crc32:08X} (IEEE 802.3)")
    print("=" * 76)
    _log("INFO", f"Starting {mode_label} for {uid_str} ({len(firmware_data)} B, CRC32=0x{full_crc32:08X})")

    # --------------------------------------------------------------------------
    # Step 1: Select Target by UID
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(1, 5, "Selecting Target Device by 96-bit UID")
    step1_msg = f"[Step 1/5] Selecting Target Device by 96-bit UID ({uid_str})..."
    print(f"\n{step1_msg}")
    _log("INFO", step1_msg)

    select_pdu = bytes([CMD_SELECT_TARGET]) + target_uid
    if not client.send_pdu(select_pdu, timeout=1.0):
        fail_msg = "Failed to transmit CMD_SELECT_TARGET frame"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    resp = client.recv_pdu(timeout=1.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        fail_msg = f"Target did not acknowledge selection: {decode_nack(resp)}"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    mode = resp[1] if len(resp) > 1 else 0x01
    ok_msg = f"Device acknowledged selection! Active mode: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}"
    print(f"  [OK] {ok_msg}")
    _log("OK", ok_msg)

    # --------------------------------------------------------------------------
    # Step 2: Confirm Bootloader / Switch from Application
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(2, 5, "Connecting / Resetting to Bootloader")
    step2_msg = "[Step 2/5] Preparing Bootloader session..."
    print(f"\n{step2_msg}")
    _log("INFO", step2_msg)

    if not is_dummy and mode == 0x01:  # APPLICATION mode -> request switch to Bootloader
        print("  --> Device is currently running APPLICATION. Requesting switch to BOOTLOADER...")
        enter_pdu = bytes([CMD_ENTER_BOOTLOADER])
        if not client.send_pdu(enter_pdu, timeout=1.5):
            fail_msg = "Failed to transmit CMD_ENTER_BOOTLOADER frame"
            print(f"  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False

        resp_boot = client.recv_pdu(timeout=1.5)
        if not resp_boot or len(resp_boot) < 1 or resp_boot[0] != OTA_RESP_ACK:
            fail_msg = f"Device rejected CMD_ENTER_BOOTLOADER: {decode_nack(resp_boot)}"
            print(f"  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False

        reboot_msg = "Device acknowledged reboot to Bootloader! Waiting for MCU reset (~150 ms)..."
        print(f"  [OK] {reboot_msg}")
        _log("INFO", reboot_msg)
        time.sleep(0.2)

        # Re-select target in Bootloader to verify session is active
        print("  --> Re-selecting target in Bootloader...")
        if not client.send_pdu(select_pdu, timeout=1.5):
            fail_msg = "Failed to transmit CMD_SELECT_TARGET after reboot"
            print(f"  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False

        resp = client.recv_pdu(timeout=1.5)
        if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
            fail_msg = f"Bootloader did not acknowledge selection after reboot: {decode_nack(resp)}"
            print(f"  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False

        mode = resp[1] if len(resp) > 1 else 0x00
        boot_conn_msg = f"Connected to Bootloader! Mode: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}"
        print(f"  [OK] {boot_conn_msg}")
        _log("OK", boot_conn_msg)
    else:
        print(f"  [OK] Target is in Bootloader (Mode: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}).")
        _log("OK", f"Target is in Bootloader ({OTA_MODE_NAMES.get(mode, 'UNKNOWN')}).")

    # --------------------------------------------------------------------------
    # Step 3: Erase Application Flash Area
    # --------------------------------------------------------------------------
    total_size = len(firmware_data)
    if step_cb:
        step_cb(3, 5, f"Erasing Flash Area (0x{app_address:08X})")
    step3_msg = f"[Step 3/5] Requesting Flash Erase (0x{app_address:08X}, size={total_size} B)..."
    print(f"\n{step3_msg}")
    _log("INFO", step3_msg)

    erase_pdu = bytes([CMD_ERASE_APP]) + struct.pack('<II', app_address, total_size)
    if not client.send_pdu(erase_pdu, timeout=2.0):
        fail_msg = "Failed to send CMD_ERASE_APP command"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    resp = client.recv_pdu(timeout=12.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        fail_msg = f"Erase command failed or timeout: {decode_nack(resp)}"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    print("  [OK] Erase confirmed by device!")
    _log("OK", "Flash erase confirmed by device!")

    # --------------------------------------------------------------------------
    # Step 4: Stream Firmware Blocks with Block-level CRC32 Verification
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(4, 5, f"Streaming {total_size} B in blocks of {block_size} B")
    step4_msg = f"[Step 4/5] Streaming {total_size} bytes in blocks of {block_size} B..."
    print(f"\n{step4_msg}")
    _log("INFO", step4_msg)

    t_stream_start = time.time()
    offset = 0
    block_index = 0
    total_retries = 0

    while offset < total_size:
        if cancel_cb and cancel_cb():
            abort_msg = "Update session cancelled by user!"
            print(f"\n  [ABORT] {abort_msg}", file=sys.stderr)
            _log("WARN", abort_msg)
            return False

        if abort_after_blocks is not None and block_index >= abort_after_blocks:
            abort_msg = f"Fault-injection: Aborting update after {block_index} blocks as requested!"
            print(f"\n  [ABORT] {abort_msg}", file=sys.stderr)
            _log("WARN", abort_msg)
            return False

        chunk = firmware_data[offset : offset + block_size]
        curr_addr = app_address + offset
        chunk_len = len(chunk)

        # Build block payload for CRC calculation: addr(4B) + len(2B) + data
        payload_for_crc = struct.pack('<IH', curr_addr, chunk_len) + chunk
        block_crc32 = zlib.crc32(payload_for_crc) & 0xFFFFFFFF

        write_pdu = bytes([CMD_WRITE_MEMORY]) + payload_for_crc + struct.pack('<I', block_crc32)

        # Transmit with retry logic (up to 3 retries)
        success = False
        for retry in range(3):
            if cancel_cb and cancel_cb():
                _log("WARN", "Update session cancelled by user during block transfer!")
                return False

            if not client.send_pdu(write_pdu, timeout=2.5):
                total_retries += 1
                continue

            resp = client.recv_pdu(timeout=2.5)
            if resp and len(resp) >= 1 and resp[0] == OTA_RESP_ACK:
                success = True
                break
            else:
                total_retries += 1
                if verbose or log_cb:
                    warn_msg = f"Block #{block_index} write retry ({retry+1}/3) - resp: {decode_nack(resp)}"
                    if verbose:
                        print(f"\n  [RETRY] {warn_msg}")
                    _log("WARN", warn_msg)

        if not success:
            fail_msg = f"Error writing block #{block_index} at offset 0x{offset:X} after 3 retries!"
            print(f"\n  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False

        offset += chunk_len
        block_index += 1
        elapsed = max(0.001, time.time() - t_stream_start)
        speed_kb = (offset / 1024.0) / elapsed
        rem_bytes = max(0, total_size - offset)
        eta_sec = (rem_bytes / (speed_kb * 1024.0)) if speed_kb > 0 else 0.0

        print_progress_bar(offset, total_size, t_stream_start, prefix="Flashing:")
        if progress_cb:
            progress_cb(offset, total_size, speed_kb, eta_sec, total_retries)

    print("\n  [OK] All blocks successfully transmitted and verified!")
    _log("OK", f"All {block_index} blocks transmitted successfully! Retries: {total_retries}")

    # --------------------------------------------------------------------------
    # Step 5: Verify Full Image CRC32 & Launch Application
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(5, 5, f"Verifying Checksum (0x{full_crc32:08X}) & Rebooting")
    step5_msg = f"[Step 5/5] Verifying entire firmware checksum (CRC32: 0x{full_crc32:08X})..."
    print(f"\n{step5_msg}")
    _log("INFO", step5_msg)

    verify_pdu = bytes([CMD_VERIFY_FIRMWARE]) + struct.pack('<II', total_size, full_crc32)
    if not client.send_pdu(verify_pdu, timeout=2.0):
        fail_msg = "Failed to send CMD_VERIFY_FIRMWARE command"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    resp = client.recv_pdu(timeout=2.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        fail_msg = f"Full image CRC32 verification rejected: {decode_nack(resp)}"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    print("  [OK] Full firmware integrity confirmed by target hardware!")
    _log("OK", "Full firmware integrity confirmed by target hardware!")

    # Request execution / reboot
    print("  --> Requesting execution / restart (CMD_GO_EXECUTE)...")
    _log("INFO", "Requesting execution / restart (CMD_GO_EXECUTE)...")
    go_pdu = bytes([CMD_GO_EXECUTE])
    client.send_pdu(go_pdu, timeout=1.0)
    resp = client.recv_pdu(timeout=1.0)
    if resp and resp[0] == OTA_RESP_ACK:
        print("  [OK] Device acknowledged jump command!")
        _log("OK", "Device acknowledged jump command!")
    else:
        print("  [INFO] Device rebooted directly into new firmware.")
        _log("INFO", "Device rebooted directly into new firmware.")

    total_time = max(0.001, time.time() - t_stream_start)
    avg_speed = (total_size / 1024.0) / total_time
    if progress_cb:
        progress_cb(total_size, total_size, avg_speed, 0.0, total_retries)

    print("\n" + "=" * 76)
    print(f"  [SUCCESS] {mode_label} COMPLETED SUCCESSFULLY!")
    print(f"  Total Data      : {total_size} bytes ({total_size/1024.0:.2f} KB)")
    print(f"  Transfer Time   : {total_time:.2f} seconds")
    print(f"  Effective Speed : {avg_speed:.2f} KB/s")
    print("=" * 76)
    _log("OK", f"{mode_label} COMPLETED SUCCESSFULLY in {total_time:.2f}s ({avg_speed:.2f} KB/s)")
    return True

def run_bootloader_update_session(bus: can.BusABC,
                                  target_uid: bytes,
                                  bootloader_data: bytes,
                                  block_size: int = DEFAULT_BLOCK_SIZE,
                                  verbose: bool = False,
                                  abort_after_blocks: Optional[int] = None,
                                  force_crc: Optional[int] = None,
                                  progress_cb: ProgressCallback = None,
                                  step_cb: StepCallback = None,
                                  log_cb: LogCallback = None,
                                  cancel_cb: CancelCallback = None) -> bool:
    """
    Executes a complete Bootloader update sequence via the Flash Staging Slot:
    1. Select Target & verify/switch to APPLICATION mode.
    2. Erase Staging Slot (0x08038000).
    3. Stream Bootloader blocks to 0x08038000 with block-level CRC32.
    4. Verify Full Staging Image CRC32.
    5. Issue CMD_UPDATE_BOOTLOADER (0xA4) to validate Cortex-M33 vector table and install to 0x08000000.
    6. Issue CMD_GO_EXECUTE to reboot MCU into the new Bootloader.
    Supports fault-injection via abort_after_blocks and force_crc.
    Notifies caller via progress_cb, step_cb, log_cb, and supports early cancellation via cancel_cb.
    """
    def _log(level: str, msg: str):
        if log_cb:
            log_cb(level, msg)

    client = IsoTpClient(bus, tx_id=CAN_ID_ISOTP_REQ, rx_id=CAN_ID_ISOTP_RESP, verbose=verbose)
    uid_str = format_uid(target_uid)
    staging_address = BOOTLOADER_STAGING_BASE
    total_size = len(bootloader_data)
    full_crc32 = force_crc if force_crc is not None else (zlib.crc32(bootloader_data) & 0xFFFFFFFF)

    print("=" * 76)
    print("  Starting BOOTLOADER FIRMWARE UPDATE (via Staging Slot)")
    print(f"  Target UID      : {uid_str}")
    print(f"  Staging Address : 0x{staging_address:08X} (Top 32 KB Flash)")
    print(f"  Target Flash    : 0x08000000 - 0x08007FFF (Bootloader Sector)")
    print(f"  Payload Size    : {total_size} bytes ({total_size/1024.0:.1f} KB)")
    print(f"  Block Size      : {block_size} bytes")
    print(f"  Full CRC32      : 0x{full_crc32:08X} (IEEE 802.3)")
    print("=" * 76)
    _log("INFO", f"Starting BOOTLOADER UPDATE for {uid_str} ({total_size} B, CRC32=0x{full_crc32:08X})")

    # --------------------------------------------------------------------------
    # Step 1: Select Target by UID & Ensure APPLICATION mode
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(1, 6, "Selecting Target Device & Checking Mode")
    step1_msg = f"[Step 1/6] Selecting Target Device by 96-bit UID ({uid_str})..."
    print(f"\n{step1_msg}")
    _log("INFO", step1_msg)

    select_pdu = bytes([CMD_SELECT_TARGET]) + target_uid
    if not client.send_pdu(select_pdu, timeout=1.0):
        fail_msg = "Failed to transmit CMD_SELECT_TARGET frame"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    resp = client.recv_pdu(timeout=1.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        fail_msg = f"Target did not acknowledge selection: {decode_nack(resp)}"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    mode = resp[1] if len(resp) > 1 else 0x01
    ok_msg = f"Device acknowledged selection! Active mode: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}"
    print(f"  [OK] {ok_msg}")
    _log("OK", ok_msg)

    if mode in (0x00, 0x02):  # BOOTLOADER or RECOVERY mode -> Bootloader update must be done from APPLICATION
        step1b_msg = "[Step 1b] Device is currently in BOOTLOADER mode. Requesting jump to APPLICATION..."
        print(f"\n{step1b_msg}")
        _log("INFO", step1b_msg)
        go_pdu = bytes([CMD_GO_EXECUTE])
        client.send_pdu(go_pdu, timeout=1.0)
        time.sleep(0.5)

        # Re-select target in Application
        print("  --> Re-connecting to device in Application...")
        if not client.send_pdu(select_pdu, timeout=1.5):
            fail_msg = "Failed to re-select target after jump"
            print(f"  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False

        resp = client.recv_pdu(timeout=1.5)
        if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
            fail_msg = f"Device did not respond after booting Application: {decode_nack(resp)}"
            print(f"  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False

        mode = resp[1] if len(resp) > 1 else 0x01
        if mode != 0x01:
            fail_msg = f"Expected APPLICATION mode, got: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}"
            print(f"  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False
        app_sw_msg = "Successfully switched to APPLICATION mode!"
        print(f"  [OK] {app_sw_msg}")
        _log("OK", app_sw_msg)

    # --------------------------------------------------------------------------
    # Step 2: Erase Staging Slot (0x08038000)
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(2, 6, "Erasing Staging Slot (0x08038000)")
    step2_msg = f"[Step 2/6] Erasing Staging Slot (0x{staging_address:08X}, size={total_size} B)..."
    print(f"\n{step2_msg}")
    _log("INFO", step2_msg)

    erase_pdu = bytes([CMD_ERASE_APP]) + struct.pack('<II', staging_address, total_size)
    if not client.send_pdu(erase_pdu, timeout=2.0):
        fail_msg = "Failed to send erase staging command"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    resp = client.recv_pdu(timeout=12.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        fail_msg = f"Staging erase rejected or timeout: {decode_nack(resp)}"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    print("  [OK] Staging Slot erased successfully!")
    _log("OK", "Staging Slot erased successfully!")

    # --------------------------------------------------------------------------
    # Step 3: Stream Bootloader Blocks to Staging Slot
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(3, 6, f"Streaming {total_size} B to Staging Slot")
    step3_msg = f"[Step 3/6] Streaming {total_size} bytes to Staging Slot in blocks of {block_size} B..."
    print(f"\n{step3_msg}")
    _log("INFO", step3_msg)

    t_stream_start = time.time()
    offset = 0
    block_index = 0
    total_retries = 0

    while offset < total_size:
        if cancel_cb and cancel_cb():
            abort_msg = "Bootloader update session cancelled by user!"
            print(f"\n  [ABORT] {abort_msg}", file=sys.stderr)
            _log("WARN", abort_msg)
            return False

        if abort_after_blocks is not None and block_index >= abort_after_blocks:
            abort_msg = f"Fault-injection: Aborting staging upload after {block_index} blocks as requested!"
            print(f"\n  [ABORT] {abort_msg}", file=sys.stderr)
            _log("WARN", abort_msg)
            return False

        chunk = bootloader_data[offset : offset + block_size]
        curr_addr = staging_address + offset
        chunk_len = len(chunk)

        payload_for_crc = struct.pack('<IH', curr_addr, chunk_len) + chunk
        block_crc32 = zlib.crc32(payload_for_crc) & 0xFFFFFFFF
        write_pdu = bytes([CMD_WRITE_MEMORY]) + payload_for_crc + struct.pack('<I', block_crc32)

        success = False
        for retry in range(3):
            if cancel_cb and cancel_cb():
                _log("WARN", "Cancelled by user during staging write!")
                return False

            if not client.send_pdu(write_pdu, timeout=2.5):
                total_retries += 1
                continue
            resp = client.recv_pdu(timeout=2.5)
            if resp and len(resp) >= 1 and resp[0] == OTA_RESP_ACK:
                success = True
                break
            else:
                total_retries += 1
                if verbose or log_cb:
                    warn_msg = f"Staging block #{block_index} retry ({retry+1}/3) - resp: {decode_nack(resp)}"
                    if verbose:
                        print(f"\n  [RETRY] {warn_msg}")
                    _log("WARN", warn_msg)

        if not success:
            fail_msg = f"Error writing staging block at offset 0x{offset:X} after 3 retries!"
            print(f"\n  [FAILED] {fail_msg}", file=sys.stderr)
            _log("ERROR", fail_msg)
            return False

        offset += chunk_len
        block_index += 1
        elapsed = max(0.001, time.time() - t_stream_start)
        speed_kb = (offset / 1024.0) / elapsed
        rem_bytes = max(0, total_size - offset)
        eta_sec = (rem_bytes / (speed_kb * 1024.0)) if speed_kb > 0 else 0.0

        print_progress_bar(offset, total_size, t_stream_start, prefix="Staging: ")
        if progress_cb:
            progress_cb(offset, total_size, speed_kb, eta_sec, total_retries)

    print("\n  [OK] All Bootloader blocks successfully uploaded to Staging Slot!")
    _log("OK", f"All {block_index} Bootloader blocks uploaded to Staging Slot! Retries: {total_retries}")

    # --------------------------------------------------------------------------
    # Step 4: Verify Full Staging CRC32
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(4, 6, f"Verifying Staged Checksum (0x{full_crc32:08X})")
    step4_msg = f"[Step 4/6] Verifying Staged Image Checksum (CRC32: 0x{full_crc32:08X})..."
    print(f"\n{step4_msg}")
    _log("INFO", step4_msg)

    verify_pdu = bytes([CMD_VERIFY_FIRMWARE]) + struct.pack('<II', total_size, full_crc32)
    if not client.send_pdu(verify_pdu, timeout=2.0):
        fail_msg = "Failed to send CMD_VERIFY_FIRMWARE command"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    resp = client.recv_pdu(timeout=2.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        fail_msg = f"Staged image checksum verification rejected: {decode_nack(resp)}"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    print("  [OK] Staged Bootloader integrity confirmed by target hardware!")
    _log("OK", "Staged Bootloader integrity confirmed by target hardware!")

    # --------------------------------------------------------------------------
    # Step 5: Install & Commit Bootloader to 0x08000000 (CMD_UPDATE_BOOTLOADER)
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(5, 6, "Installing Bootloader to 0x08000000")
    step5_msg = "[Step 5/6] Committing & Installing Bootloader into Flash (0x08000000)..."
    print(f"\n{step5_msg}")
    print("  --> Target will validate Cortex-M33 vector table and flash 0x08000000...")
    _log("INFO", step5_msg)

    install_pdu = bytes([CMD_UPDATE_BOOTLOADER]) + struct.pack('<II', total_size, full_crc32)
    if not client.send_pdu(install_pdu, timeout=3.0):
        fail_msg = "Failed to send CMD_UPDATE_BOOTLOADER command"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    resp = client.recv_pdu(timeout=12.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        fail_msg = f"Bootloader installation failed on device: {decode_nack(resp)}"
        print(f"  [FAILED] {fail_msg}", file=sys.stderr)
        _log("ERROR", fail_msg)
        return False

    print("  [OK] NEW BOOTLOADER SUCCESSFULLY INSTALLED AND VERIFIED AT 0x08000000!")
    _log("OK", "NEW BOOTLOADER SUCCESSFULLY INSTALLED AND VERIFIED AT 0x08000000!")

    # --------------------------------------------------------------------------
    # Step 6: Reboot into New Bootloader
    # --------------------------------------------------------------------------
    if step_cb:
        step_cb(6, 6, "Rebooting into New Bootloader")
    step6_msg = "[Step 6/6] Requesting system reset to launch new Bootloader..."
    print(f"\n{step6_msg}")
    _log("INFO", step6_msg)

    go_pdu = bytes([CMD_GO_EXECUTE])
    client.send_pdu(go_pdu, timeout=1.0)
    time.sleep(0.3)
    print("  [OK] Reset command sent! Target is booting new Bootloader...")
    _log("OK", "Reset command sent! Target is booting new Bootloader...")

    total_time = max(0.001, time.time() - t_stream_start)
    avg_speed = (total_size / 1024.0) / total_time
    if progress_cb:
        progress_cb(total_size, total_size, avg_speed, 0.0, total_retries)

    print("\n" + "=" * 76)
    print("  [SUCCESS] BOOTLOADER UPDATE COMPLETED SUCCESSFULLY!")
    print(f"  Total Data      : {total_size} bytes ({total_size/1024.0:.2f} KB)")
    print(f"  Transfer Time   : {total_time:.2f} seconds")
    print(f"  Effective Speed : {avg_speed:.2f} KB/s")
    print("=" * 76)
    _log("OK", f"BOOTLOADER UPDATE COMPLETED SUCCESSFULLY in {total_time:.2f}s ({avg_speed:.2f} KB/s)")
    return True

# ==============================================================================
# Main Entry Point
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="STM32 CAN Bus Firmware Updater & Diagnostic Tool (v-box)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Examples:\n"
               "  python tools/can_updater.py --scan                          (auto-detects candleLight USB)\n"
               "  python tools/can_updater.py --interface candle --scan\n"
               "  python tools/can_updater.py --port COM5 --interface slcan --scan\n"
               "  python tools/can_updater.py --dummy --size 32768\n"
               "  python tools/can_updater.py --file build/debug_GCC_NUCLEO-C542RC/v_box.bin\n"
               "  python tools/can_updater.py --update-bootloader build/debug_GCC_NUCLEO-C542RC/v_box_boot.bin\n"
    )

    parser.add_argument("--port", type=str, default=None,
                        help="CAN channel or serial port (e.g. 'candle:0:0', '0', 'COM5', '/dev/ttyACM0')")
    parser.add_argument("--interface", type=str, default="auto",
                        help="python-can interface: auto, candle (candleLight WinUSB), gs_usb (libusb), slcan (COM), socketcan (default: auto)")
    parser.add_argument("--bitrate", type=int, default=500000, help="CAN bus bitrate in bps (default: 500000)")
    parser.add_argument("--scan", action="store_true", help="Scan CAN bus for all active STM32 devices and display their UIDs")
    parser.add_argument("--uid", type=str, default=None, help="Target 96-bit MCU UID in hex (e.g. 0038002A-3132510F-39383531)")
    parser.add_argument("--dummy", action="store_true", help="Run Dummy Update test session (simulates erase and flash with CRC32)")
    parser.add_argument("--size", type=int, default=32768, help="Payload size for Dummy Update in bytes (default: 32768 = 32 KB)")
    parser.add_argument("--file", type=str, default=None, help="Path to binary firmware file (.bin) to flash (Main Application)")
    parser.add_argument("--update-bootloader", type=str, default=None, metavar="BOOT_BIN",
                        help="Update Bootloader firmware using binary file via Staging Slot")
    parser.add_argument("--block-size", type=int, default=DEFAULT_BLOCK_SIZE,
                        help=f"Block size per write transaction (8..{MAX_BLOCK_SIZE} bytes, default: {DEFAULT_BLOCK_SIZE})")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose debug logging for all ISO-TP frames")

    args = parser.parse_args()

    if not 8 <= args.block_size <= MAX_BLOCK_SIZE:
        parser.error(f"--block-size must be between 8 and {MAX_BLOCK_SIZE} bytes")

    resolved_iface, resolved_channel = resolve_can_interface_and_channel(args.interface, args.port)

    if resolved_iface == "slcan" and not resolved_channel:
        print("[ERROR] Please specify serial port using --port (e.g. --port COM5) or connect a candleLight USB adapter (--interface candle)", file=sys.stderr)
        sys.exit(1)

    print(f"[INFO] Opening CAN interface '{resolved_iface}' on channel '{resolved_channel}' @ {args.bitrate} bps...")
    try:
        bus = open_can_bus(interface=resolved_iface, channel=resolved_channel, bitrate=args.bitrate)
    except Exception as e:
        print(f"[ERROR] Failed to open CAN bus ({resolved_iface} on {resolved_channel}): {e}", file=sys.stderr)
        sys.exit(1)

    try:
        # Action 1: Bus Scan
        if args.scan:
            scan_nodes(bus, verbose=args.verbose)
            return

        # Target UID resolution
        target_uid_bytes: Optional[bytes] = None
        if args.uid:
            try:
                target_uid_bytes = parse_uid(args.uid)
            except ValueError as e:
                print(f"[ERROR] {e}", file=sys.stderr)
                sys.exit(1)
        else:
            # Auto-discover target if single device present
            found = scan_nodes(bus, verbose=args.verbose)
            if len(found) == 1:
                target_uid_bytes = found[0]["uid"]
                print(f"[INFO] Auto-selected single discovered device: {found[0]['uid_str']}")
            elif len(found) == 0:
                print("[ERROR] No devices found on bus. Specify target UID explicitly using --uid", file=sys.stderr)
                sys.exit(1)
            else:
                print("[ERROR] Multiple devices found on bus. Please specify which device to target using --uid <HEX>", file=sys.stderr)
                sys.exit(1)

        # Action 2: Dummy Update, Real Firmware Flash, or Bootloader Update
        if args.dummy:
            # Generate deterministic synthetic test payload
            print(f"[INFO] Generating {args.size} bytes of test data for Dummy Update...")
            test_data = bytes([(i & 0xFF) for i in range(args.size)])
            success = run_update_session(bus,
                                         target_uid=target_uid_bytes,
                                         firmware_data=test_data,
                                         block_size=args.block_size,
                                         is_dummy=True,
                                         verbose=args.verbose)
            sys.exit(0 if success else 1)

        elif args.update_bootloader:
            boot_path = args.update_bootloader
            if not os.path.exists(boot_path):
                print(f"[ERROR] Bootloader file '{boot_path}' does not exist!", file=sys.stderr)
                sys.exit(1)

            with open(boot_path, "rb") as f:
                boot_data = f.read()

            if len(boot_data) > BOOTLOADER_MAX_SIZE:
                print(f"[ERROR] Bootloader size ({len(boot_data)} B) exceeds 32 KB limit!", file=sys.stderr)
                sys.exit(1)

            if len(boot_data) < 512:
                print(f"[ERROR] Bootloader size ({len(boot_data)} B) is too small to be a valid image!", file=sys.stderr)
                sys.exit(1)

            if (len(boot_data) % 16) != 0:
                pad_len = 16 - (len(boot_data) % 16)
                boot_data = boot_data + (b'\xFF' * pad_len)
                print(f"[INFO] Padded bootloader to 16-byte boundary (+{pad_len} B 0xFF, total: {len(boot_data)} B)")

            print(f"[INFO] Loaded bootloader file '{boot_path}' ({len(boot_data)} bytes)")
            success = run_bootloader_update_session(bus,
                                                    target_uid=target_uid_bytes,
                                                    bootloader_data=boot_data,
                                                    block_size=args.block_size,
                                                    verbose=args.verbose)
            sys.exit(0 if success else 1)

        elif args.file:
            if not os.path.exists(args.file):
                print(f"[ERROR] Firmware file '{args.file}' does not exist!", file=sys.stderr)
                sys.exit(1)

            with open(args.file, "rb") as f:
                fw_data = f.read()

            if (len(fw_data) % 16) != 0:
                pad_len = 16 - (len(fw_data) % 16)
                fw_data = fw_data + (b'\xFF' * pad_len)
                print(f"[INFO] Padded firmware to 16-byte boundary (+{pad_len} B 0xFF, total: {len(fw_data)} B)")

            print(f"[INFO] Loaded firmware file '{args.file}' ({len(fw_data)} bytes)")
            success = run_update_session(bus,
                                         target_uid=target_uid_bytes,
                                         firmware_data=fw_data,
                                         block_size=args.block_size,
                                         is_dummy=False,
                                         verbose=args.verbose)
            sys.exit(0 if success else 1)

        else:
            print("[INFO] No action specified. Use --scan, --dummy, --file <firmware.bin>, or --update-bootloader <boot.bin>. See --help for usage.")

    finally:
        bus.shutdown()

if __name__ == "__main__":
    main()
