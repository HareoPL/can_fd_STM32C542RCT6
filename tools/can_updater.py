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
from typing import Optional, List, Tuple, Dict

try:
    import can
except ImportError:
    print("[ERROR] python-can is not installed. Run: pip install -r tools/requirements_ota.txt", file=sys.stderr)
    sys.exit(1)

# ==============================================================================
# Protocol Constants (aligned with app/ota_update/ota_protocol.h)
# ==============================================================================
CAN_ID_OTA_BROADCAST        = 0x7DF  # PC -> All Nodes (Broadcast)
CAN_ID_OTA_DISCOVERY_RESP   = 0x7E9  # Node -> PC (Discovery Response)
CAN_ID_ISOTP_REQ            = 0x7E0  # PC -> Selected Node (ISO-TP Requests)
CAN_ID_ISOTP_RESP           = 0x7E8  # Selected Node -> PC (ISO-TP Responses)

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

OTA_MODE_NAMES = {
    0x00: "BOOTLOADER",
    0x01: "APPLICATION",
    0x02: "RECOVERY_MODE"
}

# ==============================================================================
# ISO 15765-2 (ISO-TP) Transport Layer Client
# ==============================================================================
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

    def send_pdu(self, pdu: bytes, timeout: float = 1.0) -> bool:
        """Transmits an ISO-TP PDU (Single Frame if <= 7 bytes, Multi-Frame otherwise)."""
        length = len(pdu)
        if length == 0:
            return False

        if length <= 7:
            # Single Frame (SF)
            frame_data = [length] + list(pdu)
            frame_data += [0x00] * (8 - len(frame_data))
            msg = can.Message(arbitration_id=self.tx_id, is_extended_id=False, data=frame_data)
            if self.verbose:
                print(f"  [ISO-TP TX SF] {bytes(frame_data).hex()}")
            self.bus.send(msg)
            return True

        # Multi-Frame: First Frame (FF)
        ff_header = [0x10 | ((length >> 8) & 0x0F), length & 0xFF]
        frame_data = ff_header + list(pdu[:6])
        msg = can.Message(arbitration_id=self.tx_id, is_extended_id=False, data=frame_data)
        if self.verbose:
            print(f"  [ISO-TP TX FF] {bytes(frame_data).hex()} (len={length})")
        self.bus.send(msg)

        # Wait for Flow Control (FC) frame from receiver
        t_deadline = time.time() + timeout
        fc_received = False
        block_size = 0
        st_min_ms = 1

        while time.time() < t_deadline:
            rx_msg = self.bus.recv(timeout=0.05)
            if rx_msg and rx_msg.arbitration_id == self.rx_id and len(rx_msg.data) >= 3:
                pci = rx_msg.data[0] & 0xF0
                if pci == 0x30:  # Flow Control
                    fs = rx_msg.data[0] & 0x0F
                    block_size = rx_msg.data[1]
                    st_min_ms = rx_msg.data[2]
                    if self.verbose:
                        print(f"  [ISO-TP RX FC] FS={fs}, BS={block_size}, STmin={st_min_ms}ms")
                    if fs == 0x00:  # Clear To Send (CTS)
                        fc_received = True
                        break
                    elif fs == 0x02:  # Overflow
                        print("  [ISO-TP ERROR] Receiver reported buffer overflow (FC Overflow)", file=sys.stderr)
                        return False

        if not fc_received:
            if self.verbose:
                print("  [ISO-TP ERROR] Timeout waiting for Flow Control (FC) frame", file=sys.stderr)
            return False

        # Send Consecutive Frames (CF)
        offset = 6
        sn = 1
        st_delay = (st_min_ms / 1000.0) if st_min_ms > 0 else 0.0005
        cf_sent_in_block = 0

        while offset < length:
            chunk = pdu[offset : offset + 7]
            cf_header = [0x20 | (sn & 0x0F)]
            cf_data = cf_header + list(chunk)
            cf_data += [0x00] * (8 - len(cf_data))

            msg = can.Message(arbitration_id=self.tx_id, is_extended_id=False, data=cf_data)
            self.bus.send(msg)

            offset += len(chunk)
            sn = (sn + 1) & 0x0F
            cf_sent_in_block += 1

            if st_delay > 0:
                time.sleep(st_delay)

            # If receiver requested a limited Block Size (BS > 0), wait for the next Flow Control frame
            if block_size > 0 and cf_sent_in_block >= block_size and offset < length:
                cf_sent_in_block = 0
                fc_next = False
                t_fc_deadline = time.time() + timeout
                while time.time() < t_fc_deadline:
                    rx_msg = self.bus.recv(timeout=0.05)
                    if rx_msg and rx_msg.arbitration_id == self.rx_id and len(rx_msg.data) >= 3:
                        if (rx_msg.data[0] & 0xF0) == 0x30 and (rx_msg.data[0] & 0x0F) == 0x00:
                            block_size = rx_msg.data[1]
                            st_min_ms = rx_msg.data[2]
                            st_delay = (st_min_ms / 1000.0) if st_min_ms > 0 else 0.0005
                            fc_next = True
                            break
                if not fc_next:
                    print("  [ISO-TP ERROR] Timeout waiting for subsequent Flow Control (FC) frame", file=sys.stderr)
                    return False

        return True

    def recv_pdu(self, timeout: float = 1.0) -> Optional[bytes]:
        """Receives an ISO-TP PDU from the bus."""
        t_deadline = time.time() + timeout

        while time.time() < t_deadline:
            rx_msg = self.bus.recv(timeout=0.05)
            if not rx_msg or rx_msg.arbitration_id != self.rx_id or len(rx_msg.data) == 0:
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
                fc_msg = can.Message(arbitration_id=self.tx_id, is_extended_id=False, data=fc_data)
                self.bus.send(fc_msg)

                # Receive Consecutive Frames (CF)
                expected_sn = 1
                cf_deadline = time.time() + timeout
                while len(buf) < total_len and time.time() < cf_deadline:
                    cf_msg = self.bus.recv(timeout=0.05)
                    if cf_msg and cf_msg.arbitration_id == self.rx_id and len(cf_msg.data) > 1:
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

def parse_uid(raw_uid: str) -> bytes:
    """Parses user-entered UID string into 12 raw bytes."""
    clean = raw_uid.replace("-", "").replace(" ", "").strip()
    if len(clean) != 24:
        raise ValueError(f"Invalid UID length: expected 24 hex characters (12 bytes), got '{raw_uid}'")
    return bytes.fromhex(clean)

def scan_nodes(bus: can.BusABC, timeout: float = 0.5, verbose: bool = False) -> List[Dict]:
    """
    Broadcasts CMD_DISCOVER_NODES (0xA0) on 0x7DF and collects announcements from 0x7E9.
    """
    print("\n[INFO] Scanning CAN bus (500 kbps) for STM32 devices...")

    # Clear pending RX queue
    while bus.recv(timeout=0.01):
        pass

    # Broadcast discovery frame: [0x02, 0xA0, 0x00, 0, 0, 0, 0, 0]
    disc_frame = [0x02, CMD_DISCOVER_NODES, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    msg = can.Message(arbitration_id=CAN_ID_OTA_BROADCAST, is_extended_id=False, data=disc_frame)
    bus.send(msg)

    t_end = time.time() + timeout
    pending_f1: Optional[bytes] = None
    devices = []
    seen_uids = set()

    while time.time() < t_end:
        rx = bus.recv(timeout=0.02)
        if rx and rx.arbitration_id == CAN_ID_OTA_DISCOVERY_RESP and len(rx.data) >= 8:
            sub_idx = rx.data[0]
            if verbose:
                print(f"  [RAW 0x7E9] sub={sub_idx} payload={rx.data.hex()}")

            if sub_idx == 0x01:
                pending_f1 = bytes(rx.data[1:8])
            elif sub_idx == 0x02 and pending_f1 is not None:
                f2 = bytes(rx.data[1:8])
                uid_bytes = pending_f1[:7] + f2[:5]
                mode_byte = f2[5]
                ver_byte = f2[6]
                v_major = (ver_byte >> 4) & 0x0F
                v_minor = ver_byte & 0x0F

                if uid_bytes not in seen_uids:
                    seen_uids.add(uid_bytes)
                    mode_str = OTA_MODE_NAMES.get(mode_byte, f"UNKNOWN(0x{mode_byte:02X})")
                    devices.append({
                        "uid": uid_bytes,
                        "uid_str": format_uid(uid_bytes),
                        "mode": mode_str,
                        "mode_byte": mode_byte,
                        "version": f"v{v_major}.{v_minor}"
                    })
                pending_f1 = None
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
                print(f"  [RECOVERY BEACON 0xEE] Device in RECOVERY mode: Reason={reason_str}, Crashes={fail_cnt}, PC=0x{pc:08X}, UID_hash=0x{uid_hash:02X}")

    if not devices:
        print("  --> No STM32 devices responded on CAN bus.")
    else:
        print(f"\n[OK] Found {len(devices)} device(s) on CAN bus:")
        for idx, dev in enumerate(devices, 1):
            print(f"  [{idx}] UID: {dev['uid_str']} | Mode: {dev['mode']:<12} | FW: {dev['version']}")
        print()

    return devices

def print_progress_bar(current: int, total: int, start_time: float, prefix: str = ""):
    """Renders a dynamic live progress bar with percentage, speed, and ETA."""
    elapsed = max(0.001, time.time() - start_time)
    speed_kb = (current / 1024.0) / elapsed
    pct = (current / total) * 100.0 if total > 0 else 100.0
    bar_len = 30
    filled = int(round(bar_len * (current / float(total)))) if total > 0 else bar_len
    bar = "█" * filled + "-" * (bar_len - filled)

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
                       block_size: int = 256,
                       is_dummy: bool = True,
                       verbose: bool = False) -> bool:
    """
    Executes a complete update sequence (Target Select -> Erase -> Stream Blocks -> Verify CRC32 -> Execute).
    """
    client = IsoTpClient(bus, tx_id=CAN_ID_ISOTP_REQ, rx_id=CAN_ID_ISOTP_RESP, verbose=verbose)
    uid_str = format_uid(target_uid)
    mode_label = "DUMMY UPDATE (Simulation)" if is_dummy else "FIRMWARE FLASH (Hardware Flash)"

    print("=" * 76)
    print(f"  Starting {mode_label}")
    print(f"  Target UID      : {uid_str}")
    print(f"  Flash Target    : 0x{app_address:08X}")
    print(f"  Payload Size    : {len(firmware_data)} bytes ({len(firmware_data)/1024.0:.1f} KB)")
    print(f"  Block Size      : {block_size} bytes")
    full_crc32 = zlib.crc32(firmware_data) & 0xFFFFFFFF
    print(f"  Full CRC32      : 0x{full_crc32:08X} (IEEE 802.3)")
    print("=" * 76)

    # --------------------------------------------------------------------------
    # Step 1: Select Target by UID
    # --------------------------------------------------------------------------
    print("\n[Step 1/5] Selecting Target Device by 96-bit UID...")
    select_pdu = bytes([CMD_SELECT_TARGET]) + target_uid
    if not client.send_pdu(select_pdu, timeout=1.0):
        print("  [FAILED] Failed to transmit CMD_SELECT_TARGET frame", file=sys.stderr)
        return False

    resp = client.recv_pdu(timeout=1.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        print(f"  [FAILED] Target did not acknowledge selection (resp={resp.hex() if resp else 'None'})", file=sys.stderr)
        return False

    mode = resp[1] if len(resp) > 1 else 0x01
    print(f"  [OK] Device acknowledged selection! Active mode: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}")

    if not is_dummy and mode == 0x01:  # APPLICATION mode -> request switch to Bootloader
        print("\n[Step 1b] Device is currently running APPLICATION. Requesting switch to BOOTLOADER...")
        enter_pdu = bytes([CMD_ENTER_BOOTLOADER])
        if not client.send_pdu(enter_pdu, timeout=1.5):
            print("  [FAILED] Failed to transmit CMD_ENTER_BOOTLOADER frame", file=sys.stderr)
            return False

        resp_boot = client.recv_pdu(timeout=1.5)
        if not resp_boot or len(resp_boot) < 1 or resp_boot[0] != OTA_RESP_ACK:
            print(f"  [FAILED] Device rejected CMD_ENTER_BOOTLOADER (resp={resp_boot.hex() if resp_boot else 'None'})", file=sys.stderr)
            return False

        print("  [OK] Device acknowledged reboot to Bootloader! Waiting for MCU reset (~150 ms)...")
        time.sleep(0.2)

        # Re-select target in Bootloader to verify session is active
        print("  --> Re-selecting target in Bootloader...")
        if not client.send_pdu(select_pdu, timeout=1.5):
            print("  [FAILED] Failed to transmit CMD_SELECT_TARGET after reboot", file=sys.stderr)
            return False

        resp = client.recv_pdu(timeout=1.5)
        if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
            print(f"  [FAILED] Bootloader did not acknowledge selection after reboot", file=sys.stderr)
            return False

        mode = resp[1] if len(resp) > 1 else 0x00
        print(f"  [OK] Connected to Bootloader! Mode: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}")

    # --------------------------------------------------------------------------
    # Step 2: Erase Application Flash Area
    # --------------------------------------------------------------------------
    total_size = len(firmware_data)
    print(f"\n[Step 2/5] Requesting Flash Erase (0x{app_address:08X}, size={total_size} B)...")
    erase_pdu = bytes([CMD_ERASE_APP]) + struct.pack('<II', app_address, total_size)
    if not client.send_pdu(erase_pdu, timeout=2.0):
        print("  [FAILED] Failed to send CMD_ERASE_APP command", file=sys.stderr)
        return False

    resp = client.recv_pdu(timeout=6.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        print(f"  [FAILED] Erase command failed or timeout (resp={resp.hex() if resp else 'None'})", file=sys.stderr)
        return False

    print("  [OK] Erase confirmed by device!")

    # --------------------------------------------------------------------------
    # Step 3: Stream Firmware Blocks with Block-level CRC32 Verification
    # --------------------------------------------------------------------------
    print(f"\n[Step 3/5] Streaming {total_size} bytes in blocks of {block_size} B...")
    t_stream_start = time.time()
    offset = 0
    block_index = 0

    while offset < total_size:
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
            if not client.send_pdu(write_pdu, timeout=1.0):
                continue

            resp = client.recv_pdu(timeout=1.0)
            if resp and len(resp) >= 1 and resp[0] == OTA_RESP_ACK:
                success = True
                break
            elif verbose:
                print(f"\n  [RETRY] Block #{block_index} failed, retrying ({retry+1}/3)...")

        if not success:
            print(f"\n  [FAILED] Error writing block at offset 0x{offset:X} after 3 retries!", file=sys.stderr)
            return False

        offset += chunk_len
        block_index += 1
        print_progress_bar(offset, total_size, t_stream_start, prefix="Flashing:")

    print("\n  [OK] All blocks successfully transmitted and verified!")

    # --------------------------------------------------------------------------
    # Step 4: Verify Full Image CRC32
    # --------------------------------------------------------------------------
    print(f"\n[Step 4/5] Verifying entire firmware checksum (CRC32: 0x{full_crc32:08X})...")
    verify_pdu = bytes([CMD_VERIFY_FIRMWARE]) + struct.pack('<II', total_size, full_crc32)
    if not client.send_pdu(verify_pdu, timeout=2.0):
        print("  [FAILED] Failed to send CMD_VERIFY_FIRMWARE command", file=sys.stderr)
        return False

    resp = client.recv_pdu(timeout=2.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        print(f"  [FAILED] Full image CRC32 verification rejected by device! (resp={resp.hex() if resp else 'None'})", file=sys.stderr)
        return False

    print("  [OK] Full firmware integrity confirmed by target hardware!")

    # --------------------------------------------------------------------------
    # Step 5: Execute / Jump to Application
    # --------------------------------------------------------------------------
    print("\n[Step 5/5] Requesting execution / restart (CMD_GO_EXECUTE)...")
    go_pdu = bytes([CMD_GO_EXECUTE])
    client.send_pdu(go_pdu, timeout=1.0)
    resp = client.recv_pdu(timeout=1.0)
    if resp and resp[0] == OTA_RESP_ACK:
        print("  [OK] Device acknowledged jump command!")
    else:
        print("  [INFO] Device rebooted directly into new firmware.")

    total_time = max(0.001, time.time() - t_stream_start)
    avg_speed = (total_size / 1024.0) / total_time
    print("\n" + "=" * 76)
    print(f"  [SUCCESS] {mode_label} COMPLETED SUCCESSFULLY!")
    print(f"  Total Data      : {total_size} bytes ({total_size/1024.0:.2f} KB)")
    print(f"  Transfer Time   : {total_time:.2f} seconds")
    print(f"  Effective Speed : {avg_speed:.2f} KB/s")
    print("=" * 76)
    return True

def run_bootloader_update_session(bus: can.BusABC,
                                  target_uid: bytes,
                                  bootloader_data: bytes,
                                  block_size: int = 256,
                                  verbose: bool = False) -> bool:
    """
    Executes a complete Bootloader update sequence via the Flash Staging Slot:
    1. Select Target & verify/switch to APPLICATION mode.
    2. Erase Staging Slot (0x08038000).
    3. Stream Bootloader blocks to 0x08038000 with block-level CRC32.
    4. Verify Full Staging Image CRC32.
    5. Issue CMD_UPDATE_BOOTLOADER (0xA4) to validate Cortex-M33 vector table and install to 0x08000000.
    6. Issue CMD_GO_EXECUTE to reboot MCU into the new Bootloader.
    """
    client = IsoTpClient(bus, tx_id=CAN_ID_ISOTP_REQ, rx_id=CAN_ID_ISOTP_RESP, verbose=verbose)
    uid_str = format_uid(target_uid)
    staging_address = BOOTLOADER_STAGING_BASE
    total_size = len(bootloader_data)
    full_crc32 = zlib.crc32(bootloader_data) & 0xFFFFFFFF

    print("=" * 76)
    print("  Starting BOOTLOADER FIRMWARE UPDATE (via Staging Slot)")
    print(f"  Target UID      : {uid_str}")
    print(f"  Staging Address : 0x{staging_address:08X} (Top 32 KB Flash)")
    print(f"  Target Flash    : 0x08000000 - 0x08007FFF (Bootloader Sector)")
    print(f"  Payload Size    : {total_size} bytes ({total_size/1024.0:.1f} KB)")
    print(f"  Block Size      : {block_size} bytes")
    print(f"  Full CRC32      : 0x{full_crc32:08X} (IEEE 802.3)")
    print("=" * 76)

    # --------------------------------------------------------------------------
    # Step 1: Select Target by UID & Ensure APPLICATION mode
    # --------------------------------------------------------------------------
    print("\n[Step 1/6] Selecting Target Device by 96-bit UID...")
    select_pdu = bytes([CMD_SELECT_TARGET]) + target_uid
    if not client.send_pdu(select_pdu, timeout=1.0):
        print("  [FAILED] Failed to transmit CMD_SELECT_TARGET frame", file=sys.stderr)
        return False

    resp = client.recv_pdu(timeout=1.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        print(f"  [FAILED] Target did not acknowledge selection (resp={resp.hex() if resp else 'None'})", file=sys.stderr)
        return False

    mode = resp[1] if len(resp) > 1 else 0x01
    print(f"  [OK] Device acknowledged selection! Active mode: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}")

    if mode == 0x00:  # BOOTLOADER mode -> Bootloader update must be done from APPLICATION
        print("\n[Step 1b] Device is currently in BOOTLOADER mode.")
        print("  --> Bootloader cannot overwrite itself while executing!")
        print("  --> Requesting jump to APPLICATION mode...")
        go_pdu = bytes([CMD_GO_EXECUTE])
        client.send_pdu(go_pdu, timeout=1.0)
        time.sleep(0.5)

        # Re-select target in Application
        print("  --> Re-connecting to device in Application...")
        if not client.send_pdu(select_pdu, timeout=1.5):
            print("  [FAILED] Failed to re-select target after jump", file=sys.stderr)
            return False

        resp = client.recv_pdu(timeout=1.5)
        if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
            print("  [FAILED] Device did not respond after booting Application", file=sys.stderr)
            return False

        mode = resp[1] if len(resp) > 1 else 0x01
        if mode != 0x01:
            print(f"  [FAILED] Expected APPLICATION mode, got: {OTA_MODE_NAMES.get(mode, 'UNKNOWN')}", file=sys.stderr)
            return False
        print(f"  [OK] Successfully switched to APPLICATION mode!")

    # --------------------------------------------------------------------------
    # Step 2: Erase Staging Slot (0x08038000)
    # --------------------------------------------------------------------------
    print(f"\n[Step 2/6] Erasing Staging Slot (0x{staging_address:08X}, size={total_size} B)...")
    erase_pdu = bytes([CMD_ERASE_APP]) + struct.pack('<II', staging_address, total_size)
    if not client.send_pdu(erase_pdu, timeout=2.0):
        print("  [FAILED] Failed to send erase staging command", file=sys.stderr)
        return False

    resp = client.recv_pdu(timeout=6.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        print(f"  [FAILED] Staging erase rejected or timeout (resp={resp.hex() if resp else 'None'})", file=sys.stderr)
        return False

    print("  [OK] Staging Slot erased successfully!")

    # --------------------------------------------------------------------------
    # Step 3: Stream Bootloader Blocks to Staging Slot
    # --------------------------------------------------------------------------
    print(f"\n[Step 3/6] Streaming {total_size} bytes to Staging Slot in blocks of {block_size} B...")
    t_stream_start = time.time()
    offset = 0
    block_index = 0

    while offset < total_size:
        chunk = bootloader_data[offset : offset + block_size]
        curr_addr = staging_address + offset
        chunk_len = len(chunk)

        payload_for_crc = struct.pack('<IH', curr_addr, chunk_len) + chunk
        block_crc32 = zlib.crc32(payload_for_crc) & 0xFFFFFFFF
        write_pdu = bytes([CMD_WRITE_MEMORY]) + payload_for_crc + struct.pack('<I', block_crc32)

        success = False
        for retry in range(3):
            if not client.send_pdu(write_pdu, timeout=1.0):
                continue
            resp = client.recv_pdu(timeout=1.0)
            if resp and len(resp) >= 1 and resp[0] == OTA_RESP_ACK:
                success = True
                break
            elif verbose:
                print(f"\n  [RETRY] Block #{block_index} failed, retrying ({retry+1}/3)...")

        if not success:
            print(f"\n  [FAILED] Error writing staging block at offset 0x{offset:X} after 3 retries!", file=sys.stderr)
            return False

        offset += chunk_len
        block_index += 1
        print_progress_bar(offset, total_size, t_stream_start, prefix="Staging: ")

    print("\n  [OK] All Bootloader blocks successfully uploaded to Staging Slot!")

    # --------------------------------------------------------------------------
    # Step 4: Verify Full Staging CRC32
    # --------------------------------------------------------------------------
    print(f"\n[Step 4/6] Verifying Staged Image Checksum (CRC32: 0x{full_crc32:08X})...")
    verify_pdu = bytes([CMD_VERIFY_FIRMWARE]) + struct.pack('<II', total_size, full_crc32)
    if not client.send_pdu(verify_pdu, timeout=2.0):
        print("  [FAILED] Failed to send CMD_VERIFY_FIRMWARE command", file=sys.stderr)
        return False

    resp = client.recv_pdu(timeout=2.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        print(f"  [FAILED] Staged image checksum verification rejected! (resp={resp.hex() if resp else 'None'})", file=sys.stderr)
        return False

    print("  [OK] Staged Bootloader integrity confirmed by target hardware!")

    # --------------------------------------------------------------------------
    # Step 5: Install & Commit Bootloader to 0x08000000 (CMD_UPDATE_BOOTLOADER)
    # --------------------------------------------------------------------------
    print("\n[Step 5/6] Committing & Installing Bootloader into Flash (0x08000000)...")
    print("  --> Target will validate Cortex-M33 vector table and flash 0x08000000...")
    install_pdu = bytes([CMD_UPDATE_BOOTLOADER]) + struct.pack('<II', total_size, full_crc32)
    if not client.send_pdu(install_pdu, timeout=3.0):
        print("  [FAILED] Failed to send CMD_UPDATE_BOOTLOADER command", file=sys.stderr)
        return False

    resp = client.recv_pdu(timeout=6.0)
    if not resp or len(resp) < 1 or resp[0] != OTA_RESP_ACK:
        print(f"  [FAILED] Bootloader installation failed on device! (resp={resp.hex() if resp else 'None'})", file=sys.stderr)
        return False

    print("  [OK] NEW BOOTLOADER SUCCESSFULLY INSTALLED AND VERIFIED AT 0x08000000!")

    # --------------------------------------------------------------------------
    # Step 6: Reboot into New Bootloader
    # --------------------------------------------------------------------------
    print("\n[Step 6/6] Requesting system reset to launch new Bootloader...")
    go_pdu = bytes([CMD_GO_EXECUTE])
    client.send_pdu(go_pdu, timeout=1.0)
    time.sleep(0.3)
    print("  [OK] Reset command sent! Target is booting new Bootloader...")

    total_time = max(0.001, time.time() - t_stream_start)
    avg_speed = (total_size / 1024.0) / total_time
    print("\n" + "=" * 76)
    print("  [SUCCESS] BOOTLOADER UPDATE COMPLETED SUCCESSFULLY!")
    print(f"  Total Data      : {total_size} bytes ({total_size/1024.0:.2f} KB)")
    print(f"  Transfer Time   : {total_time:.2f} seconds")
    print(f"  Effective Speed : {avg_speed:.2f} KB/s")
    print("=" * 76)
    return True

# ==============================================================================
# Main Entry Point
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="STM32 CAN Bus Firmware Updater & Diagnostic Tool (v-box)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Examples:\n"
               "  python tools/can_updater.py --port COM5 --scan\n"
               "  python tools/can_updater.py --port COM5 --dummy --size 32768\n"
               "  python tools/can_updater.py --port COM5 --file build/debug_GCC_NUCLEO-C542RC/v_box.bin\n"
               "  python tools/can_updater.py --port COM5 --update-bootloader build/debug_GCC_NUCLEO-C542RC/v_box_boot.bin\n"
    )

    parser.add_argument("--port", type=str, default=None, help="Serial COM port for SLCAN/CANable (e.g. COM5 or /dev/ttyACM0)")
    parser.add_argument("--interface", type=str, default="slcan", help="python-can interface type (default: slcan; e.g. socketcan)")
    parser.add_argument("--bitrate", type=int, default=500000, help="CAN bus bitrate in bps (default: 500000)")
    parser.add_argument("--scan", action="store_true", help="Scan CAN bus for all active STM32 devices and display their UIDs")
    parser.add_argument("--uid", type=str, default=None, help="Target 96-bit MCU UID in hex (e.g. 0038002A-3132510F-39383531)")
    parser.add_argument("--dummy", action="store_true", help="Run Dummy Update test session (simulates erase and flash with CRC32)")
    parser.add_argument("--size", type=int, default=32768, help="Payload size for Dummy Update in bytes (default: 32768 = 32 KB)")
    parser.add_argument("--file", type=str, default=None, help="Path to binary firmware file (.bin) to flash (Main Application)")
    parser.add_argument("--update-bootloader", type=str, default=None, metavar="BOOT_BIN",
                        help="Update Bootloader firmware using binary file via Staging Slot")
    parser.add_argument("--block-size", type=int, default=256, help="Block size per write transaction (default: 256 bytes)")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose debug logging for all ISO-TP frames")

    args = parser.parse_args()

    # Determine connection parameters
    channel = args.port
    if args.interface == "socketcan" and channel is None:
        channel = "can0"

    if channel is None and args.interface == "slcan":
        # Check standard default COM port or scan
        print("[ERROR] Please specify serial port using --port (e.g. --port COM5 or --port /dev/ttyACM0)", file=sys.stderr)
        sys.exit(1)

    print(f"[INFO] Opening CAN interface '{args.interface}' on channel '{channel}' @ {args.bitrate} bps...")
    try:
        if args.interface == "slcan":
            bus = can.interface.Bus(interface='slcan', channel=channel, bitrate=args.bitrate)
        else:
            bus = can.interface.Bus(interface=args.interface, channel=channel, bitrate=args.bitrate)
    except Exception as e:
        print(f"[ERROR] Failed to open CAN bus: {e}", file=sys.stderr)
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
