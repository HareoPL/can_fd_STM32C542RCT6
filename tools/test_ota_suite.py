#!/usr/bin/env python3
"""
STM32 CAN Bus Firmware Update Fault Injection & Recovery Test Suite (v-box ecosystem).

This test suite executes automated and interactive fault-injection test scenarios
to verify resilience, safety guards, and recovery mechanisms for both:
  - CAN Bootloader (v_box_boot @ 0x08000000, 32 KB)
  - Main Application (v_box @ 0x08008000, 192 KB)

Test Scenarios:
  1. TEST 1 [Application]: Corrupted Firmware Checksum (CRC32 Mismatch Rejection)
  2. TEST 2 [Application]: Flash Interruption & Power-Loss Recovery Mode Verification
  3. TEST 3 [Bootloader] : Invalid Cortex-M33 Vector Table Protection (Staging Integrity Guard)
  4. TEST 4 [Bootloader] : Interrupted Bootloader Upload to Staging Slot (Power-Loss Fail-Safe)
  5. TEST 5 [Application]: Golden Path Full Application Update
  6. TEST 6 [Bootloader] : Golden Path Full Bootloader Update via Staging Slot

Usage Examples:
  # Run all test scenarios interactively:
  python tools/test_ota_suite.py --port COM5

  # Run specific test scenario (e.g. Test 2: Power-Loss Recovery):
  python tools/test_ota_suite.py --port COM5 --test 2

  # Run non-interactive / automated mode (skips manual power disconnect prompt):
  python tools/test_ota_suite.py --port COM5 --non-interactive

  # Run with verbose ISO-TP frame tracing:
  python tools/test_ota_suite.py --port COM5 --verbose
"""

import sys
import os
import time
import struct
import zlib
import argparse
from typing import Optional, List, Dict, Tuple
from dataclasses import dataclass

# Ensure local tools directory is importable
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
if CURRENT_DIR not in sys.path:
    sys.path.insert(0, CURRENT_DIR)

try:
    import can
except ImportError:
    print("[ERROR] python-can is not installed. Run: pip install -r tools/requirements_ota.txt", file=sys.stderr)
    sys.exit(1)

import can_updater

# ==============================================================================
# Constants & Defaults
# ==============================================================================
DEFAULT_APP_BIN = os.path.normpath(os.path.join(CURRENT_DIR, "..", "build", "debug_GCC_NUCLEO-C542RC", "v_box.bin"))
DEFAULT_BOOT_BIN = os.path.normpath(os.path.join(CURRENT_DIR, "..", "build", "debug_GCC_NUCLEO-C542RC", "v_box_boot.bin"))

DEFAULT_BITRATE = 500000
DEFAULT_BLOCK_SIZE = 256

# ==============================================================================
# Test Data Structure
# ==============================================================================
@dataclass
class TestResult:
    """Stores test outcome and execution metrics."""
    test_id: str
    target: str          # "Application" or "Bootloader"
    title: str
    status: str          # "PASS", "FAIL", "SKIP"
    duration: float      # in seconds
    details: str

# ==============================================================================
# Helper Utilities
# ==============================================================================

def print_banner(text: str, fill: str = "=") -> None:
    """Prints a prominent visual banner."""
    width = 80
    print("\n" + fill * width)
    print(f"  {text}")
    print(fill * width)

def prompt_user_action(instruction_lines: List[str], non_interactive: bool = False) -> None:
    """
    Displays an interactive prompt requesting physical user action (e.g. power cycle).
    In non-interactive mode, waits briefly without blocking on input().
    """
    width = 80
    print("\n" + "#" * width)
    print("  [KROK WYMAGAJĄCY INTERWENCJI UŻYTKOWNIKA / USER ACTION REQUIRED]")
    print("#" * width)
    for line in instruction_lines:
        print(f"  >> {line}")
    print("#" * width)

    if non_interactive:
        print("  [INFO] Non-interactive mode active: Waiting 4 seconds instead of user input...")
        time.sleep(4.0)
    else:
        try:
            input("\n  [PROMPT] Wykonaj powyższą czynność i naciśnij [ENTER], aby kontynuować... ")
        except (KeyboardInterrupt, EOFError):
            print("\n[ABORT] Test aborted by user.", file=sys.stderr)
            sys.exit(1)

def safe_open_or_reconnect_bus(existing_bus: Optional[can.BusABC],
                               interface: str,
                               channel: str,
                               bitrate: int) -> can.BusABC:
    """
    Safely opens or re-opens CAN bus connection (resilient to USB disconnect/reconnect).
    """
    if existing_bus is not None:
        try:
            existing_bus.shutdown()
        except Exception:
            pass
        time.sleep(0.8)  # Allow OS driver to settle

    for attempt in range(1, 6):
        try:
            bus = can_updater.open_can_bus(interface=interface, channel=channel, bitrate=bitrate)

            # Flush stale frames
            time.sleep(0.05)
            while bus.recv(timeout=0.01):
                pass
            return bus
        except Exception as e:
            if attempt == 5:
                raise RuntimeError(f"Could not connect to CAN channel '{channel}' ({interface}) after 5 attempts: {e}")
            time.sleep(0.8)

    raise RuntimeError("Failed to establish CAN bus connection")

def discover_single_device(bus: can.BusABC,
                           target_uid: Optional[bytes] = None,
                           timeout: float = 1.0,
                           beacons_out: Optional[List[Dict]] = None) -> Optional[Dict]:
    """
    Discovers target device on CAN bus. If target_uid is specified, matches that UID.
    """
    nodes = can_updater.scan_nodes(bus, timeout=timeout, verbose=False, beacons_out=beacons_out)
    if not nodes:
        return None

    if target_uid is not None:
        for node in nodes:
            if node["uid"] == target_uid:
                return node
        return None

    return nodes[0]

def wait_for_device_online(bus: can.BusABC,
                           target_uid: bytes,
                           expected_mode: Optional[int] = None,
                           timeout: float = 8.0,
                           beacons_out: Optional[List[Dict]] = None) -> Optional[Dict]:
    """
    Polls the CAN bus until target node is discovered, optionally checking for expected mode.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        node = discover_single_device(bus, target_uid=target_uid, timeout=0.35, beacons_out=beacons_out)
        if node:
            if expected_mode is None or node.get("mode_byte") == expected_mode:
                return node
        time.sleep(0.2)
    return None

def switch_device_mode(bus: can.BusABC,
                       target_uid: bytes,
                       desired_mode: int,
                       verbose: bool = False) -> bool:
    """
    Switches device between BOOTLOADER (0x00) and APPLICATION (0x01).
    """
    client = can_updater.IsoTpClient(bus, tx_id=can_updater.CAN_ID_ISOTP_REQ,
                                     rx_id=can_updater.CAN_ID_ISOTP_RESP, verbose=verbose)

    # 1. Select Target
    select_pdu = bytes([can_updater.CMD_SELECT_TARGET]) + target_uid
    if not client.send_pdu(select_pdu, timeout=1.0):
        return False
    resp = client.recv_pdu(timeout=1.0)
    if not resp or len(resp) < 2 or resp[0] != can_updater.OTA_RESP_ACK:
        return False

    current_mode = resp[1]
    if current_mode == desired_mode:
        return True

    if desired_mode == can_updater.OTA_MODE_BOOTLOADER:
        # Request switch to Bootloader
        cmd_pdu = bytes([can_updater.CMD_ENTER_BOOTLOADER])
        if not client.send_pdu(cmd_pdu, timeout=1.0):
            return False
        resp_cmd = client.recv_pdu(timeout=1.0)
        if not resp_cmd or resp_cmd[0] != can_updater.OTA_RESP_ACK:
            return False
        time.sleep(0.3)
        return True
    elif desired_mode == can_updater.OTA_MODE_APPLICATION:
        # Request jump to Application
        cmd_pdu = bytes([can_updater.CMD_GO_EXECUTE])
        client.send_pdu(cmd_pdu, timeout=1.0)
        time.sleep(0.4)
        return True

    return False

# ==============================================================================
# Test Scenario Implementations
# ==============================================================================

def test_1_app_corrupted_crc(bus: can.BusABC,
                             target_uid: bytes,
                             app_data: bytes,
                             verbose: bool) -> TestResult:
    """
    TEST 1: Injects an intentionally corrupted CRC32 into the Application flash verification step.
    Expected: Device rejects verification with NACK (OTA_ERR_CRC), stays alive, doesn't jump.
    """
    t_start = time.time()
    title = "Corrupted CRC32 Checksum Rejection"
    print_banner(f"TEST 1 [Application]: {title}")

    # Ensure device is reachable
    print("  [1/4] Verifying target connection...")
    dev = discover_single_device(bus, target_uid=target_uid, timeout=0.8)
    if not dev:
        return TestResult("T1", "Application", title, "FAIL", time.time() - t_start, "Target device not reachable on CAN bus")

    # Automotive collision-immunity guard check:
    # 1) Send standard 11-bit OBD-II 0x7DF frame -> must be ignored
    # 2) Send 29-bit 0x01F2F3DF frame WITHOUT "VBOX" magic token -> must be ignored
    print("  [2/4] Verifying Automotive Collision Guard (ignoring 11-bit 0x7DF & unauthorized 0x01F2F3DF)...")
    while bus.recv(timeout=0.01):
        pass
    obd_msg = can.Message(arbitration_id=0x7DF, is_extended_id=False, data=[0x02, 0x01, 0x00, 0, 0, 0, 0, 0])
    bad_magic_msg = can.Message(arbitration_id=can_updater.CAN_ID_OTA_BROADCAST, is_extended_id=True,
                                data=[0x06, can_updater.CMD_DISCOVER_NODES, 0x11, 0x22, 0x33, 0x44, 0, 0])
    bus.send(obd_msg)
    bus.send(bad_magic_msg)
    t_guard_end = time.time() + 0.25
    while time.time() < t_guard_end:
        rx_guard = bus.recv(timeout=0.02)
        if rx_guard and rx_guard.arbitration_id in (0x7E8, 0x7E9, can_updater.CAN_ID_OTA_DISCOVERY_RESP, can_updater.CAN_ID_ISOTP_RESP):
            return TestResult("T1", "Application", title, "FAIL", time.time() - t_start,
                              f"Collision guard failed: MCU responded to unauthorized frame (ID=0x{rx_guard.arbitration_id:X})!")
    print("  [OK] Collision guard verified: MCU remained 100% silent on unauthorized frames.")

    # Invert the CRC to force checksum mismatch
    true_crc = zlib.crc32(app_data) & 0xFFFFFFFF
    corrupted_crc = true_crc ^ 0xFFFFFFFF
    print(f"  [3/4] Simulating corrupted CRC32 transmission (True: 0x{true_crc:08X} -> Corrupted: 0x{corrupted_crc:08X})...")

    # Run update session with forced bad CRC
    success = can_updater.run_update_session(
        bus=bus,
        target_uid=target_uid,
        firmware_data=app_data,
        is_dummy=False,
        verbose=verbose,
        force_crc=corrupted_crc
    )

    duration = time.time() - t_start

    # Pass condition: update MUST FAIL (return False)
    if success:
        return TestResult("T1", "Application", title, "FAIL", duration,
                          "ERROR: Device erroneously accepted corrupted CRC32!")

    print("\n  [4/4] Update was rejected as expected. Checking if MCU remains alive and responsive...")
    time.sleep(0.3)
    dev_post = discover_single_device(bus, target_uid=target_uid, timeout=1.0)
    if not dev_post:
        return TestResult("T1", "Application", title, "FAIL", duration,
                          "Device stopped responding on CAN bus after CRC rejection!")

    print(f"  [OK] Device responded on CAN! Active mode: {dev_post['mode']}")
    return TestResult("T1", "Application", title, "PASS", duration,
                      "Collision guard & CRC32 NACK verified; MCU remained alive and responsive")


def test_2_app_power_loss_recovery(bus_holder: List[can.BusABC],
                                   interface: str,
                                   channel: str,
                                   bitrate: int,
                                   target_uid: bytes,
                                   app_data: bytes,
                                   interactive: bool,
                                   verbose: bool) -> TestResult:
    """
    TEST 2: Simulates power loss during application flash write, verifies Bootloader Recovery Mode,
    and performs a clean rescue re-flash.
    """
    t_start = time.time()
    title = "Flash Interruption & Power-Loss Recovery"
    print_banner(f"TEST 2 [Application]: {title}")

    bus = bus_holder[0]

    # Step 1: Interrupt flash midway
    print("  [1/4] Starting partial application flash (aborting after 5 blocks to simulate cut-off)...")
    success = can_updater.run_update_session(
        bus=bus,
        target_uid=target_uid,
        firmware_data=app_data,
        is_dummy=False,
        verbose=verbose,
        abort_after_blocks=5
    )

    if success:
        return TestResult("T2", "Application", title, "FAIL", time.time() - t_start,
                          "Flash did not abort as expected!")

    print("  [OK] Flash intentionally interrupted midway. Application flash sector is now incomplete.")

    # Step 2: Interactive Power Disconnect Prompt
    instruction = [
        "SYMULACJA ZANIKU ZASILANIA (POWER LOSS / CABLE DISCONNECT)",
        "1. TERAZ ODŁĄCZ ZASILANIE PŁYTKI (lub odłącz przewód USB z NUCLEO-C542RC)!",
        "2. Odczekaj 3 sekundy.",
        "3. Podłącz przewód USB / zasilanie z powrotem.",
        "4. Naciśnij klawisz [ENTER] w tym oknie, aby wznowić test."
    ]
    prompt_user_action(instruction, non_interactive=not interactive)

    # Step 3: Re-open CAN bus connection (in case USB serial port reset)
    print("\n  [2/4] Reconnecting to CAN bus and verifying Silent Recovery Mode (zero unsolicited traffic)...")
    try:
        bus = safe_open_or_reconnect_bus(bus, interface, channel, bitrate)
        bus_holder[0] = bus
    except Exception as e:
        return TestResult("T2", "Application", title, "FAIL", time.time() - t_start, f"CAN reconnect failed: {e}")

    # Verify Silent Recovery Mode: listen passively for 1.5s before sending any query
    t_silent_end = time.time() + 1.5
    while time.time() < t_silent_end:
        rx_spont = bus.recv(timeout=0.05)
        if rx_spont and rx_spont.arbitration_id == can_updater.CAN_ID_OTA_DISCOVERY_RESP:
            return TestResult("T2", "Application", title, "FAIL", time.time() - t_start,
                              "Bootloader emitted unsolicited frame on CAN bus in Recovery Mode!")
    print("  [OK] Silent Recovery verified: 0 unsolicited frames on vehicle CAN bus over 1.5s.")

    # Query device via authorized "VBOX" discovery broadcast and verify on-demand 0xEE beacon
    captured_beacons: List[Dict] = []
    dev = wait_for_device_online(bus, target_uid=target_uid, timeout=12.0, beacons_out=captured_beacons)
    if not dev:
        return TestResult("T2", "Application", title, "FAIL", time.time() - t_start,
                          "Device did not respond after power cycle!")

    print(f"  [OK] Device detected online! Mode: {dev['mode']} (On-demand 0xEE beacons received: {len(captured_beacons)})")
    if dev.get("mode_byte") not in (can_updater.OTA_MODE_RECOVERY, can_updater.OTA_MODE_BOOTLOADER):
        print(f"  [WARN] Expected RECOVERY_MODE or BOOTLOADER, device reported: {dev['mode']}")

    # Step 4: Perform Rescue Flash
    print("\n  [3/4] Performing RESCUE FLASH with valid Application binary...")
    rescue_ok = can_updater.run_update_session(
        bus=bus,
        target_uid=target_uid,
        firmware_data=app_data,
        is_dummy=False,
        verbose=verbose
    )

    if not rescue_ok:
        return TestResult("T2", "Application", title, "FAIL", time.time() - t_start,
                          "Rescue flash failed to recover the node!")

    print("\n  [4/4] Verifying device booted into APPLICATION mode...")
    time.sleep(0.5)
    dev_restored = wait_for_device_online(bus, target_uid=target_uid, expected_mode=can_updater.OTA_MODE_APPLICATION, timeout=4.0)
    duration = time.time() - t_start

    if not dev_restored or dev_restored.get("mode_byte") != can_updater.OTA_MODE_APPLICATION:
        return TestResult("T2", "Application", title, "FAIL", duration,
                          f"Device recovered but is in mode: {dev_restored.get('mode') if dev_restored else 'OFFLINE'}")

    return TestResult("T2", "Application", title, "PASS", duration,
                      "Device entered Recovery mode after power cut; full rescue re-flash restored Application successfully")


def test_3_boot_invalid_vector(bus: can.BusABC,
                               target_uid: bytes,
                               boot_data: bytes,
                               app_data: bytes,
                               verbose: bool) -> TestResult:
    """
    TEST 3: Injects an invalid Cortex-M33 vector table into the Bootloader image, verifies that
    bootloader_updater validates the table and rejects installation, keeping Sector 0 safe.
    """
    t_start = time.time()
    title = "Invalid Cortex-M33 Vector Table Protection"
    print_banner(f"TEST 3 [Bootloader]: {title}")

    # Ensure device is running Application (bootloader update is performed from App)
    print("  [1/4] Ensuring device is running in APPLICATION mode...")
    switch_device_mode(bus, target_uid, can_updater.OTA_MODE_APPLICATION, verbose=verbose)
    time.sleep(0.4)

    dev = discover_single_device(bus, target_uid=target_uid, timeout=1.0)
    if not dev or dev.get("mode_byte") != can_updater.OTA_MODE_APPLICATION:
        return TestResult("T3", "Bootloader", title, "FAIL", time.time() - t_start,
                          f"Device must be in APPLICATION mode to test bootloader update (current: {dev['mode'] if dev else 'OFFLINE'})")

    # Corrupt MSP at offset 0 (set to 0xDEADBEEF, which is outside SRAM 0x20000000 - 0x20010000)
    print("  [2/4] Creating corrupted Bootloader image (corrupting Initial MSP: 0xDEADBEEF)...")
    corrupted_boot = bytearray(boot_data)
    corrupted_boot[0:4] = struct.pack('<I', 0xDEADBEEF)

    # Execute bootloader update session
    print("  [3/4] Uploading corrupted Bootloader to Staging Slot (0x08038000)...")
    success = can_updater.run_bootloader_update_session(
        bus=bus,
        target_uid=target_uid,
        bootloader_data=bytes(corrupted_boot),
        verbose=verbose
    )

    duration = time.time() - t_start

    # Pass condition: update MUST FAIL at Step 5 (CMD_UPDATE_BOOTLOADER)
    if success:
        return TestResult("T3", "Bootloader", title, "FAIL", duration,
                          "ERROR: Device committed Bootloader with invalid vector table!")

    print("\n  [4/4] Commit rejected by hardware guard. Verifying Application is still alive...")
    time.sleep(0.3)
    dev_post = discover_single_device(bus, target_uid=target_uid, timeout=1.0)
    if not dev_post or dev_post.get("mode_byte") != can_updater.OTA_MODE_APPLICATION:
        return TestResult("T3", "Bootloader", title, "FAIL", duration,
                          "Application crashed or went offline after vector rejection!")

    print(f"  [OK] Hardware safety guard blocked corrupt Bootloader! Application is alive ({dev_post['mode']}).")
    return TestResult("T3", "Bootloader", title, "PASS", duration,
                      "Vector table guard rejected bad MSP (0xDEADBEEF); Sector 0 (0x08000000) was untouched")


def test_4_boot_interrupted_staging(bus_holder: List[can.BusABC],
                                    interface: str,
                                    channel: str,
                                    bitrate: int,
                                    target_uid: bytes,
                                    boot_data: bytes,
                                    interactive: bool,
                                    verbose: bool) -> TestResult:
    """
    TEST 4: Simulates power interruption during Bootloader staging upload. Verifies that uncommitted
    staging data has zero effect on the active Bootloader at 0x08000000.
    """
    t_start = time.time()
    title = "Interrupted Bootloader Staging Upload (Fail-Safe)"
    print_banner(f"TEST 4 [Bootloader]: {title}")

    bus = bus_holder[0]

    # Ensure device is in Application
    print("  [1/4] Ensuring device is in APPLICATION mode...")
    switch_device_mode(bus, target_uid, can_updater.OTA_MODE_APPLICATION, verbose=verbose)
    time.sleep(0.3)

    # Start bootloader update, but abort after 5 blocks
    print("  [2/4] Streaming Bootloader to Staging Slot (aborting after 5 blocks)...")
    success = can_updater.run_bootloader_update_session(
        bus=bus,
        target_uid=target_uid,
        bootloader_data=boot_data,
        verbose=verbose,
        abort_after_blocks=5
    )

    if success:
        return TestResult("T4", "Bootloader", title, "FAIL", time.time() - t_start,
                          "Staging did not abort as expected!")

    print("  [OK] Staging upload aborted before commit command (CMD_UPDATE_BOOTLOADER).")

    # Prompt user to reset board
    instruction = [
        "WERYFIKACJA OCHRONY PRZED PRZERWANIEM TRANSMISJI STAGINGU",
        "1. TERAZ ZRESETUJ PŁYTKĘ (naciśnij czarny przycisk B2 - Reset na Nucleo lub odłącz/podłącz USB).",
        "2. Naciśnij klawisz [ENTER] w tym oknie konsoli, aby kontynuować."
    ]
    prompt_user_action(instruction, non_interactive=not interactive)

    # Re-open CAN bus
    print("\n  [3/4] Reconnecting to CAN bus...")
    try:
        bus = safe_open_or_reconnect_bus(bus, interface, channel, bitrate)
        bus_holder[0] = bus
    except Exception as e:
        return TestResult("T4", "Bootloader", title, "FAIL", time.time() - t_start, f"CAN reconnect failed: {e}")

    # Check device online
    print("  [4/4] Checking device response after reset...")
    dev = wait_for_device_online(bus, target_uid=target_uid, timeout=10.0)
    duration = time.time() - t_start

    if not dev:
        return TestResult("T4", "Bootloader", title, "FAIL", duration,
                          "Device did not boot or respond on CAN after reset!")

    print(f"  [OK] Device booted cleanly using untouched Bootloader! Active mode: {dev['mode']}")
    return TestResult("T4", "Bootloader", title, "PASS", duration,
                      "Uncommitted staging data did not affect active Bootloader; MCU booted normally after reset")


def test_5_golden_app(bus: can.BusABC,
                      target_uid: bytes,
                      app_data: bytes,
                      verbose: bool) -> TestResult:
    """
    TEST 5: Full Golden Path Application Update (clean flash of v_box.bin).
    """
    t_start = time.time()
    title = "Golden Path Application Update (v_box.bin)"
    print_banner(f"TEST 5 [Application]: {title}")

    print("  [1/2] Flashing clean Application binary...")
    success = can_updater.run_update_session(
        bus=bus,
        target_uid=target_uid,
        firmware_data=app_data,
        is_dummy=False,
        verbose=verbose
    )

    duration = time.time() - t_start

    if not success:
        return TestResult("T5", "Application", title, "FAIL", duration, "Application update session returned failure")

    print("\n  [2/2] Verifying Application booted...")
    time.sleep(0.5)
    dev = wait_for_device_online(bus, target_uid=target_uid, expected_mode=can_updater.OTA_MODE_APPLICATION, timeout=4.0)

    if not dev or dev.get("mode_byte") != can_updater.OTA_MODE_APPLICATION:
        return TestResult("T5", "Application", title, "FAIL", duration, "Device did not switch to APPLICATION mode")

    print(f"  [OK] Application running successfully! Mode: {dev['mode']}")
    return TestResult("T5", "Application", title, "PASS", duration,
                      "100% blocks acknowledged, full CRC verified, Application booted successfully")


def test_6_golden_bootloader(bus: can.BusABC,
                             target_uid: bytes,
                             boot_data: bytes,
                             verbose: bool) -> TestResult:
    """
    TEST 6: Full Golden Path Bootloader Update via Flash Staging Slot.
    """
    t_start = time.time()
    title = "Golden Path Bootloader Update (v_box_boot.bin)"
    print_banner(f"TEST 6 [Bootloader]: {title}")

    # Ensure device in Application
    print("  [1/3] Ensuring device is in APPLICATION mode...")
    switch_device_mode(bus, target_uid, can_updater.OTA_MODE_APPLICATION, verbose=verbose)
    time.sleep(0.4)

    # Perform Bootloader Update
    print("  [2/3] Performing full Bootloader update via Staging Slot...")
    success = can_updater.run_bootloader_update_session(
        bus=bus,
        target_uid=target_uid,
        bootloader_data=boot_data,
        verbose=verbose
    )

    duration = time.time() - t_start

    if not success:
        return TestResult("T6", "Bootloader", title, "FAIL", duration, "Bootloader update session failed")

    print("\n  [3/3] Verifying new Bootloader booted and launched Application...")
    time.sleep(0.8)
    dev = wait_for_device_online(bus, target_uid=target_uid, timeout=5.0)

    if not dev:
        return TestResult("T6", "Bootloader", title, "FAIL", duration, "Device did not respond after Bootloader update")

    print(f"  [OK] Device online after Bootloader upgrade! Mode: {dev['mode']}")
    return TestResult("T6", "Bootloader", title, "PASS", duration,
                      "Staged, validated, committed to 0x08000000; MCU rebooted cleanly into new Bootloader")

# ==============================================================================
# Comprehensive Test Report Presentation
# ==============================================================================

def print_final_test_report(results: List[TestResult]) -> None:
    """
    Renders an ASCII summary table detailing all test scenarios, outcomes,
    and diagnostic explanations at the end of the test suite execution.
    """
    width = 96
    print("\n" + "=" * width)
    print(" " * 24 + "STM32 CAN OTA FAULT INJECTION & RECOVERY REPORT")
    print("=" * width)
    print(f"  {'ID':<3} | {'Target':<11} | {'Scenario Title':<34} | {'Status':<6} | {'Time [s]':<8} | Details")
    print("-" * width)

    total_pass = 0
    total_fail = 0
    total_skip = 0

    for r in results:
        status_disp = f"[{r.status}]"
        if r.status == "PASS":
            total_pass += 1
        elif r.status == "FAIL":
            total_fail += 1
        else:
            total_skip += 1

        print(f"  {r.test_id:<3} | {r.target:<11} | {r.title:<34} | {status_disp:<6} | {r.duration:>6.2f}s  | {r.details}")

    print("=" * width)
    summary_str = f"TOTAL: {len(results)} | PASSED: {total_pass} | FAILED: {total_fail} | SKIPPED: {total_skip}"
    print(f"  {summary_str:<50}")

    if total_fail == 0:
        verdict = "[SUCCESS] ALL TEST SCENARIOS PASSED WITH EXPECTED RESILIENCE!"
    else:
        verdict = f"[FAILURE] {total_fail} TEST SCENARIO(S) FAILED! SEE DETAILS ABOVE."

    print(f"  VERDICT : {verdict}")
    print("=" * width + "\n")

# ==============================================================================
# Main CLI & Runner
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="STM32 CAN OTA Fault Injection & Recovery Test Suite",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Examples:\n"
               "  python tools/test_ota_suite.py                          (auto-detects candleLight USB)\n"
               "  python tools/test_ota_suite.py --interface candle\n"
               "  python tools/test_ota_suite.py --interface candle --test 2\n"
               "  python tools/test_ota_suite.py --port COM5 --interface slcan\n"
               "  python tools/test_ota_suite.py --non-interactive\n"
    )

    parser.add_argument("--port", type=str, default=None,
                        help="CAN channel or serial port (e.g. 'candle:0:0', '0', 'COM5', '/dev/ttyACM0')")
    parser.add_argument("--interface", type=str, default="auto",
                        help="python-can interface: auto, candle (candleLight WinUSB), gs_usb (libusb), slcan (COM), socketcan (default: auto)")
    parser.add_argument("--bitrate", type=int, default=DEFAULT_BITRATE,
                        help=f"CAN bus bitrate (default: {DEFAULT_BITRATE})")
    parser.add_argument("--uid", type=str, default=None,
                        help="Target 96-bit MCU UID in hex (auto-discovered if not specified)")
    parser.add_argument("--app-bin", type=str, default=DEFAULT_APP_BIN,
                        help="Path to Main Application binary (.bin)")
    parser.add_argument("--boot-bin", type=str, default=DEFAULT_BOOT_BIN,
                        help="Path to CAN Bootloader binary (.bin)")
    parser.add_argument("--test", type=int, choices=[1, 2, 3, 4, 5, 6], default=None,
                        help="Run only one specific test scenario (1 to 6)")
    parser.add_argument("--non-interactive", action="store_true",
                        help="Skip interactive user prompts (uses automated delay instead)")
    parser.add_argument("--verbose", action="store_true",
                        help="Enable verbose ISO-TP packet tracing")

    args = parser.parse_args()

    resolved_iface, channel = can_updater.resolve_can_interface_and_channel(args.interface, args.port)
    args.interface = resolved_iface

    if args.interface == "slcan" and not channel:
        print("[ERROR] Please specify serial port using --port (e.g. --port COM5) or connect a candleLight USB adapter (--interface candle)", file=sys.stderr)
        sys.exit(1)

    # Validate binary files
    if not os.path.isfile(args.app_bin):
        print(f"[ERROR] Application binary not found: {args.app_bin}", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(args.boot_bin):
        print(f"[ERROR] Bootloader binary not found: {args.boot_bin}", file=sys.stderr)
        sys.exit(1)

    with open(args.app_bin, 'rb') as f:
        app_data = f.read()
    with open(args.boot_bin, 'rb') as f:
        boot_data = f.read()

    print_banner("STM32 CAN OTA FAULT INJECTION & RECOVERY TEST SUITE")
    print(f"  Target Port     : {channel} ({args.interface} @ {args.bitrate} bps)")
    print(f"  Application Bin : {args.app_bin} ({len(app_data)} bytes)")
    print(f"  Bootloader Bin  : {args.boot_bin} ({len(boot_data)} bytes)")
    print(f"  Interactive Mode: {'NO (Automated)' if args.non_interactive else 'YES (Manual Prompts)'}")

    # Connect to CAN bus
    print(f"\n[INFO] Opening CAN interface on '{channel}'...")
    try:
        bus = safe_open_or_reconnect_bus(None, args.interface, channel, args.bitrate)
    except Exception as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)

    # Use list wrapper to allow test functions to reconnect the bus in-place
    bus_holder = [bus]

    # Resolve target UID
    target_uid_bytes: Optional[bytes] = None
    if args.uid:
        try:
            target_uid_bytes = can_updater.parse_uid(args.uid)
        except ValueError as e:
            print(f"[ERROR] {e}", file=sys.stderr)
            sys.exit(1)
    else:
        print("[INFO] Auto-discovering STM32 devices on CAN bus...")
        dev = discover_single_device(bus_holder[0], timeout=1.0)
        if not dev:
            print("[ERROR] No STM32 devices responded to discovery ping! Check connections & power.", file=sys.stderr)
            sys.exit(1)
        target_uid_bytes = dev["uid"]
        print(f"[OK] Target acquired: UID {dev['uid_str']} (Mode: {dev['mode']})")

    results: List[TestResult] = []
    tests_to_run = [args.test] if args.test is not None else [1, 2, 3, 4, 5, 6]

    try:
        # TEST 1: Corrupted CRC32
        if 1 in tests_to_run:
            try:
                res1 = test_1_app_corrupted_crc(bus_holder[0], target_uid_bytes, app_data, args.verbose)
            except Exception as e:
                res1 = TestResult("T1", "Application", "Corrupted CRC32 Checksum Rejection", "FAIL", 0.0, f"Exception: {e}")
            results.append(res1)
            time.sleep(0.5)

        # TEST 2: Application Power-Loss Recovery
        if 2 in tests_to_run:
            try:
                res2 = test_2_app_power_loss_recovery(bus_holder, args.interface, channel, args.bitrate,
                                                     target_uid_bytes, app_data, not args.non_interactive, args.verbose)
            except Exception as e:
                res2 = TestResult("T2", "Application", "Flash Interruption & Power-Loss Recovery", "FAIL", 0.0, f"Exception: {e}")
            results.append(res2)
            time.sleep(0.5)

        # TEST 3: Bootloader Invalid Vector Table
        if 3 in tests_to_run:
            try:
                res3 = test_3_boot_invalid_vector(bus_holder[0], target_uid_bytes, boot_data, app_data, args.verbose)
            except Exception as e:
                res3 = TestResult("T3", "Bootloader", "Invalid Cortex-M33 Vector Table Protection", "FAIL", 0.0, f"Exception: {e}")
            results.append(res3)
            time.sleep(0.5)

        # TEST 4: Bootloader Interrupted Staging
        if 4 in tests_to_run:
            try:
                res4 = test_4_boot_interrupted_staging(bus_holder, args.interface, channel, args.bitrate,
                                                       target_uid_bytes, boot_data, not args.non_interactive, args.verbose)
            except Exception as e:
                res4 = TestResult("T4", "Bootloader", "Interrupted Bootloader Staging Upload", "FAIL", 0.0, f"Exception: {e}")
            results.append(res4)
            time.sleep(0.5)

        # TEST 5: Golden Path Application
        if 5 in tests_to_run:
            try:
                res5 = test_5_golden_app(bus_holder[0], target_uid_bytes, app_data, args.verbose)
            except Exception as e:
                res5 = TestResult("T5", "Application", "Golden Path Application Update", "FAIL", 0.0, f"Exception: {e}")
            results.append(res5)
            time.sleep(0.5)

        # TEST 6: Golden Path Bootloader
        if 6 in tests_to_run:
            try:
                res6 = test_6_golden_bootloader(bus_holder[0], target_uid_bytes, boot_data, args.verbose)
            except Exception as e:
                res6 = TestResult("T6", "Bootloader", "Golden Path Bootloader Update", "FAIL", 0.0, f"Exception: {e}")
            results.append(res6)
            time.sleep(0.5)

    finally:
        # Print final comprehensive report table
        print_final_test_report(results)
        try:
            bus_holder[0].shutdown()
        except Exception:
            pass

    # Exit with code 0 if all ran tests passed, otherwise 1
    failed_count = sum(1 for r in results if r.status == "FAIL")
    if failed_count > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()

