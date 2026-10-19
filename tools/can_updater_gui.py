#!/usr/bin/env python3
"""
V-BOX CAN OTA UPDATER & DIAGNOSTIC STUDIO (STM32C542 / FDCAN2)
Advanced GUI for STM32 Firmware Updates (MainApp & Bootloader) & Bus Diagnostics.

Pure-Python implementation supporting both PyQt6 and PyQt5 with:
  - Smart COM Port Enumeration (CANable 2.0 SLCAN vs ST-Link VCP)
  - Continuous Passive & Active Bus Monitoring (0x7E9 & Recovery Beacon 0xEE)
  - Clickable Fleet Status KPI Tiles & Pulsing SOS Emergency Banner
  - PC Crash Address Decoding via arm-none-eabi-addr2line
  - Repository-Aware Auto-Build & Out-of-Date Build Detection
  - Pre-Flight Binary Guard (Cortex-M33 Vector Table & Flash Partition Validator)
  - 5-Stage Visual Pipeline Stepper & Real-Time Transfer Telemetry
  - Dual Monitored Terminal (CAN/ISO-TP Transmission Log & MCU UART2 Serial Monitor)
"""

import sys
import os
import time
import zlib
import struct
import shutil
import argparse
import subprocess
import threading
from typing import Optional, List, Dict, Tuple

# Local import from tools directory
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
if CURRENT_DIR not in sys.path:
    sys.path.insert(0, CURRENT_DIR)

import can_updater

try:
    import can
except ImportError:
    print("[ERROR] python-can is not installed. Run: pip install -r tools/requirements_ota.txt", file=sys.stderr)
    sys.exit(1)

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("[ERROR] pyserial is not installed. Run: pip install -r tools/requirements_ota.txt", file=sys.stderr)
    sys.exit(1)

# ==============================================================================
# Qt Compatibility Layer (PyQt6 with automatic fallback to PyQt5)
# ==============================================================================
try:
    from PyQt6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
        QGridLayout, QLabel, QPushButton, QComboBox, QLineEdit,
        QProgressBar, QTableWidget, QTableWidgetItem, QHeaderView,
        QTabWidget, QPlainTextEdit, QFileDialog, QMessageBox,
        QRadioButton, QCheckBox, QGroupBox, QFrame, QSplitter, QButtonGroup,
        QSpinBox, QMenu
    )
    from PyQt6.QtCore import Qt, QThread, pyqtSignal, QTimer, QSize, QPoint
    from PyQt6.QtGui import QColor, QFont, QTextCursor, QIcon, QPalette, QBrush, QAction
    QT_VERSION = 6
    ALIGN_CENTER = Qt.AlignmentFlag.AlignCenter
    ALIGN_LEFT = Qt.AlignmentFlag.AlignLeft
    ALIGN_RIGHT = Qt.AlignmentFlag.AlignRight
    ALIGN_VCENTER = Qt.AlignmentFlag.AlignVCenter
    HEADER_STRETCH = QHeaderView.ResizeMode.Stretch
    HEADER_RESIZE_TO_CONTENTS = QHeaderView.ResizeMode.ResizeToContents
    TABLE_SELECT_ROWS = QTableWidget.SelectionBehavior.SelectRows
    TABLE_SINGLE_SELECTION = QTableWidget.SelectionMode.SingleSelection
except ImportError:
    from PyQt5.QtWidgets import (
        QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
        QGridLayout, QLabel, QPushButton, QComboBox, QLineEdit,
        QProgressBar, QTableWidget, QTableWidgetItem, QHeaderView,
        QTabWidget, QPlainTextEdit, QFileDialog, QMessageBox,
        QRadioButton, QCheckBox, QGroupBox, QFrame, QSplitter, QButtonGroup,
        QSpinBox, QMenu, QAction
    )
    from PyQt5.QtCore import Qt, QThread, pyqtSignal, QTimer, QSize, QPoint
    from PyQt5.QtGui import QColor, QFont, QTextCursor, QIcon, QPalette, QBrush
    QT_VERSION = 5
    ALIGN_CENTER = Qt.AlignCenter
    ALIGN_LEFT = Qt.AlignLeft
    ALIGN_RIGHT = Qt.AlignRight
    ALIGN_VCENTER = Qt.AlignVCenter
    HEADER_STRETCH = QHeaderView.Stretch
    HEADER_RESIZE_TO_CONTENTS = QHeaderView.ResizeToContents
    TABLE_SELECT_ROWS = QTableWidget.SelectRows
    TABLE_SINGLE_SELECTION = QTableWidget.SingleSelection

# ==============================================================================
# Dark Engineering Theme (QSS)
# ==============================================================================
DARK_ENGINEERING_THEME = """
QMainWindow, QWidget {
    background-color: #12131A;
    color: #E2E8F0;
    font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
    font-size: 13px;
}
QLabel {
    border: none;
    background-color: transparent;
}
QGroupBox {
    border: 1px solid #2D3748;
    border-radius: 8px;
    margin-top: 10px;
    padding: 12px 10px 8px 10px;
    font-weight: 600;
    color: #94A3B8;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 2px 8px;
    background-color: #12131A;
    color: #38BDF8;
}
QPushButton {
    background-color: #1E293B;
    color: #F8FAFC;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 6px 14px;
    font-weight: 600;
    min-height: 18px;
}
QPushButton:hover {
    background-color: #2E3B52;
    border-color: #60A5FA;
}
QPushButton:pressed {
    background-color: #0F172A;
}
QPushButton:disabled,
QPushButton#btn_primary:disabled,
QPushButton#btn_success:disabled,
QPushButton#btn_danger:disabled,
QPushButton#btn_scan_table:disabled {
    background-color: #0F172A;
    color: #475569;
    border: 1px solid #1E293B;
}
QPushButton#btn_primary {
    background-color: #2563EB;
    border: 1px solid #3B82F6;
    color: #FFFFFF;
}
QPushButton#btn_primary:hover {
    background-color: #1D4ED8;
    border-color: #93C5FD;
}
QPushButton#btn_success {
    background-color: #059669;
    border: 1px solid #10B981;
    color: #FFFFFF;
}
QPushButton#btn_success:hover {
    background-color: #047857;
    border-color: #6EE7B7;
}
QPushButton#btn_danger {
    background-color: #DC2626;
    border: 1px solid #EF4444;
    color: #FFFFFF;
}
QPushButton#btn_danger:hover {
    background-color: #B91C1C;
    border-color: #FCA5A5;
}
QLineEdit, QComboBox, QSpinBox {
    background-color: #0B0F19;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 5px 8px;
    color: #F8FAFC;
    selection-background-color: #2563EB;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
    border: 1px solid #38BDF8;
}
QComboBox QAbstractItemView {
    background-color: #0B0F19;
    border: 1px solid #334155;
    selection-background-color: #1E293B;
    color: #F8FAFC;
}
QTableWidget {
    background-color: #0B0F19;
    border: 1px solid #2D3748;
    border-radius: 6px;
    gridline-color: #1E293B;
    color: #F8FAFC;
    selection-background-color: #1E3A8A;
    selection-color: #FFFFFF;
}
QHeaderView::section {
    background-color: #1E293B;
    color: #94A3B8;
    padding: 6px;
    font-weight: 600;
    border: none;
    border-bottom: 1px solid #334155;
}
QProgressBar {
    background-color: #0B0F19;
    border: 1px solid #334155;
    border-radius: 6px;
    text-align: center;
    color: #FFFFFF;
    font-weight: bold;
    height: 24px;
}
QProgressBar::chunk {
    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2563EB, stop:1 #06B6D4);
    border-radius: 5px;
}
QTabWidget::pane {
    border: 1px solid #2D3748;
    border-radius: 6px;
    background-color: #07090E;
}
QTabBar::tab {
    background-color: #1A1F2E;
    color: #94A3B8;
    padding: 7px 16px;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    margin-right: 3px;
    font-weight: 600;
}
QTabBar::tab:selected {
    background-color: #07090E;
    color: #38BDF8;
    border-bottom: 2px solid #38BDF8;
}
QPlainTextEdit {
    background-color: #07090E;
    border: none;
    color: #E2E8F0;
    font-family: 'Consolas', 'Fira Code', 'Courier New', monospace;
    font-size: 12px;
}
QRadioButton {
    color: #CBD5E1;
    font-weight: 500;
}
QRadioButton::indicator {
    width: 14px;
    height: 14px;
}
QCheckBox {
    color: #CBD5E1;
}
QScrollBar:vertical {
    background: #0B0F19;
    width: 12px;
    margin: 0px;
    border: 1px solid #1E293B;
    border-radius: 4px;
}
QScrollBar::handle:vertical {
    background: #475569;
    min-height: 24px;
    border-radius: 4px;
}
QScrollBar::handle:vertical:hover {
    background: #64748B;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}
QScrollBar:horizontal {
    background: #0B0F19;
    height: 12px;
    margin: 0px;
    border: 1px solid #1E293B;
    border-radius: 4px;
}
QScrollBar::handle:horizontal {
    background: #475569;
    min-width: 24px;
    border-radius: 4px;
}
QScrollBar::handle:horizontal:hover {
    background: #64748B;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}
"""

# ==============================================================================
# Helper Functions: Ports, Repository, Binary Validation & Crash Decoding
# ==============================================================================

def find_serial_ports() -> List[Dict]:
    """
    Enumerates serial ports with accurate classification (CANable 2.0 vs ST-Link VCP).
    Returns a list of dicts with port metadata.
    """
    ports = []
    try:
        raw_ports = serial.tools.list_ports.comports()
    except Exception:
        raw_ports = []

    for p in raw_ports:
        desc = p.description or ""
        hwid = p.hwid or ""
        vid = p.vid
        pid = p.pid

        # Classify device accurately without false positives
        is_stlink = False
        is_canable = False

        if (vid == 0x0483 and pid in (0x374B, 0x374E, 0x3752, 0x3754)) or "STLINK" in desc.upper() or "ST-LINK" in desc.upper():
            is_stlink = True
        elif (vid == 0x16D0 and pid == 0x117E) or "CANABLE" in desc.upper() or "SLCAN" in desc.upper():
            is_canable = True

        if is_canable:
            label = f"{p.device} - CANable 2.0 (SLCAN)"
        elif is_stlink:
            label = f"{p.device} - ST-Link VCP (UART2)"
        else:
            label = f"{p.device} - {desc}" if desc else p.device

        ports.append({
            "device": p.device,
            "description": desc,
            "hwid": hwid,
            "vid": vid,
            "pid": pid,
            "is_canable": is_canable,
            "is_stlink": is_stlink,
            "label": label
        })
    return ports



def get_vbox_repo_root() -> Optional[str]:
    """Returns absolute path to repository root if running inside 5_v-box repo, else None."""
    candidate = os.path.abspath(os.path.join(CURRENT_DIR, ".."))
    if os.path.isfile(os.path.join(candidate, "CMakeLists.txt")) and os.path.isdir(os.path.join(candidate, "app", "bootloader")):
        return candidate
    return None


def is_inside_vbox_repo() -> bool:
    """Checks whether current execution is inside 5_v-box project repository."""
    return get_vbox_repo_root() is not None


def is_build_stale(bin_path: str, repo_root: str) -> Tuple[bool, Optional[str], float]:
    """
    Checks if any source code in app/, generated/, linker/ is newer than the target binary.
    Returns (is_stale, newest_source_file_rel, time_diff_sec).
    """
    if not os.path.isfile(bin_path):
        return True, None, 0.0

    bin_mtime = os.path.getmtime(bin_path)
    newest_mtime = bin_mtime
    newest_file = None

    check_dirs = [
        os.path.join(repo_root, "app"),
        os.path.join(repo_root, "generated"),
        os.path.join(repo_root, "linker")
    ]
    check_exts = {".c", ".h", ".ld"}

    for d in check_dirs:
        if not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            for f in files:
                ext = os.path.splitext(f)[1].lower()
                if ext in check_exts or f == "CMakeLists.txt":
                    fp = os.path.join(root, f)
                    try:
                        fm = os.path.getmtime(fp)
                        if fm > newest_mtime:
                            newest_mtime = fm
                            newest_file = os.path.relpath(fp, repo_root)
                    except OSError:
                        pass

    is_stale = newest_file is not None
    diff_sec = (newest_mtime - bin_mtime) if is_stale else 0.0
    return is_stale, newest_file, diff_sec


def inspect_firmware_binary(data: bytes, target_type: str = "APP") -> Tuple[bool, str, Dict]:
    """
    Pre-Flight Binary Guard: Inspects binary size, Cortex-M33 vector table (MSP & Reset), CRC32, and alignment.
    Prevents flashing Bootloader as App or vice-versa.
    target_type in {"APP", "BOOTLOADER", "DUMMY"}.
    """
    result = {
        "size": len(data),
        "msp": 0,
        "reset": 0,
        "crc32": 0,
        "pad_len": 0,
        "is_thumb": False,
        "valid_msp": False,
        "valid_reset": False,
        "mode_mismatch": False
    }

    if target_type == "DUMMY":
        result["crc32"] = zlib.crc32(data) & 0xFFFFFFFF
        return True, "Dummy update simulation payload", result

    size = len(data)
    if target_type == "APP":
        if size < 512 or size > 196608:
            return False, f"Błędny rozmiar aplikacji: {size} B (dozwolony zakres: 512 B - 192 KB)", result
    elif target_type == "BOOTLOADER":
        if size < 512 or size > 32768:
            return False, f"Błędny rozmiar bootloadera: {size} B (dozwolony zakres: 512 B - 32 KB)", result

    if size < 8:
        return False, "Plik binarny jest za krótki (brak wektorów startowych Cortex-M33)", result

    msp, reset = struct.unpack('<II', data[:8])
    result["msp"] = msp
    result["reset"] = reset
    result["crc32"] = zlib.crc32(data) & 0xFFFFFFFF

    # 16-byte alignment check
    if (size % 16) != 0:
        result["pad_len"] = 16 - (size % 16)

    # 1. Check Initial_MSP: must reside within STM32C5 SRAM (0x20000000 .. 0x20010000)
    if 0x20000000 <= msp <= 0x20010000:
        result["valid_msp"] = True
    else:
        return False, f"Błędny wskaźnik stosu Initial_MSP: 0x{msp:08X} (wymagany zakres SRAM: 0x20000000..0x20010000)", result

    # 2. Check Thumb mode: bit 0 of Reset_Handler must be 1
    if (reset & 0x01) == 0x01:
        result["is_thumb"] = True
    else:
        return False, f"Wektor Reset_Handler (0x{reset:08X}) nie posiada flagi Thumb (bit 0 musi być 1)", result

    # 3. Check partition boundaries
    if target_type == "APP":
        # Must reside in Application sector (0x08008001 .. 0x08037FFF)
        if 0x08008001 <= reset <= 0x08037FFF:
            result["valid_reset"] = True
        elif 0x08000001 <= reset <= 0x08007FFF:
            result["mode_mismatch"] = True
            return False, f"WYKRYTO NIEZGODNOŚĆ! Wybrany plik to obraz BOOTLOADERA (Reset=0x{reset:08X} w 0x08000000..0x08007FFF), a wybrano aktualizację Aplikacji Głównej!", result
        else:
            return False, f"Reset_Handler (0x{reset:08X}) wskazuje poza partycję Aplikacji (0x08008000..0x08037FFF)", result

    elif target_type == "BOOTLOADER":
        # Must reside in Bootloader sector (0x08000001 .. 0x08007FFF)
        if 0x08000001 <= reset <= 0x08007FFF:
            result["valid_reset"] = True
        elif 0x08008001 <= reset <= 0x08037FFF:
            result["mode_mismatch"] = True
            return False, f"WYKRYTO NIEZGODNOŚĆ! Wybrany plik to obraz APLIKACJI GŁÓWNEJ (Reset=0x{reset:08X} w 0x08008000..0x08037FFF), a wybrano aktualizację Bootloadera!", result
        else:
            return False, f"Reset_Handler (0x{reset:08X}) wskazuje poza partycję Bootloadera (0x08000000..0x08007FFF)", result

    return True, "Weryfikacja Pre-Flight zakończona sukcesem", result


_PC_DECODE_CACHE: Dict[int, str] = {}
_PC_DECODING_IN_PROGRESS: set = set()


def decode_pc_address(pc_addr: int, repo_root: Optional[str] = None, on_decoded_cb=None, force_sync_docker: bool = False) -> str:
    """
    Decodes MCU program counter (PC) using arm-none-eabi-addr2line and v_box.elf.
    Uses in-memory caching for 0ms repeated lookups.
    Non-blocking: if Docker is needed, it runs asynchronously in background to prevent GUI freeze.
    """
    if pc_addr == 0 or pc_addr == 0xFFFFFFFF:
        return "Adres nieustalony (0xFFFFFFFF)"

    if pc_addr in _PC_DECODE_CACHE:
        return _PC_DECODE_CACHE[pc_addr]

    if not repo_root:
        repo_root = get_vbox_repo_root()

    elf_candidates = []
    if repo_root:
        elf_candidates.append(os.path.join(repo_root, "build", "debug_GCC_NUCLEO-C542RC", "v_box.elf"))
        elf_candidates.append(os.path.join(repo_root, "build", "docker", "v_box.elf"))

    elf_path = None
    for c in elf_candidates:
        if os.path.isfile(c):
            elf_path = c
            break

    if not elf_path:
        res = f"0x{pc_addr:08X} (Brak v_box.elf)"
        _PC_DECODE_CACHE[pc_addr] = res
        return res

    # 1. Try local native addr2line (fast, ~1-3 ms)
    addr_tool = shutil.which("arm-none-eabi-addr2line") or shutil.which("addr2line")
    if addr_tool:
        try:
            cmd = [addr_tool, "-f", "-C", "-e", elf_path, f"0x{pc_addr:08X}"]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=1.0)
            if res.returncode == 0:
                lines = [l.strip() for l in res.stdout.strip().splitlines() if l.strip()]
                if len(lines) >= 2:
                    func_name = lines[0]
                    loc = os.path.basename(lines[1])
                    result = f"{func_name}() [{loc}]"
                    _PC_DECODE_CACHE[pc_addr] = result
                    return result
                elif len(lines) == 1:
                    result = f"{lines[0]} (0x{pc_addr:08X})"
                    _PC_DECODE_CACHE[pc_addr] = result
                    return result
        except Exception:
            pass

    # 2. Synchronous Docker (only if explicitly requested by manual button click)
    if force_sync_docker and repo_root and shutil.which("docker"):
        try:
            rel_elf = os.path.relpath(elf_path, repo_root).replace("\\", "/")
            cmd = [
                "docker", "run", "--rm",
                "-v", f"{repo_root}:/workspaces/5_v-box",
                "-w", "/workspaces/5_v-box",
                "vbox-builder",
                "arm-none-eabi-addr2line", "-f", "-C", "-e", rel_elf, f"0x{pc_addr:08X}"
            ]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=3.0)
            if res.returncode == 0:
                lines = [l.strip() for l in res.stdout.strip().splitlines() if l.strip()]
                if len(lines) >= 2:
                    func_name = lines[0]
                    loc = os.path.basename(lines[1])
                    result = f"{func_name}() [{loc}]"
                    _PC_DECODE_CACHE[pc_addr] = result
                    return result
                elif len(lines) == 1:
                    result = f"{lines[0]} (0x{pc_addr:08X})"
                    _PC_DECODE_CACHE[pc_addr] = result
                    return result
        except Exception:
            pass

    # 3. Asynchronous background Docker resolution (prevents GUI thread freezing)
    if repo_root and shutil.which("docker") and pc_addr not in _PC_DECODING_IN_PROGRESS:
        _PC_DECODING_IN_PROGRESS.add(pc_addr)

        def _bg_resolver():
            try:
                rel_elf = os.path.relpath(elf_path, repo_root).replace("\\", "/")
                cmd = [
                    "docker", "run", "--rm",
                    "-v", f"{repo_root}:/workspaces/5_v-box",
                    "-w", "/workspaces/5_v-box",
                    "vbox-builder",
                    "arm-none-eabi-addr2line", "-f", "-C", "-e", rel_elf, f"0x{pc_addr:08X}"
                ]
                res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5.0)
                if res.returncode == 0:
                    lines = [l.strip() for l in res.stdout.strip().splitlines() if l.strip()]
                    if len(lines) >= 2:
                        func_name = lines[0]
                        loc = os.path.basename(lines[1])
                        res_str = f"{func_name}() [{loc}]"
                    elif len(lines) == 1:
                        res_str = f"{lines[0]} (0x{pc_addr:08X})"
                    else:
                        res_str = f"0x{pc_addr:08X}"
                    _PC_DECODE_CACHE[pc_addr] = res_str
                    if on_decoded_cb:
                        on_decoded_cb(pc_addr, res_str)
            except Exception:
                pass
            finally:
                _PC_DECODING_IN_PROGRESS.discard(pc_addr)

        t = threading.Thread(target=_bg_resolver, daemon=True)
        t.start()

    # Fast default return without blocking GUI
    default_res = f"0x{pc_addr:08X}"
    _PC_DECODE_CACHE[pc_addr] = default_res
    return default_res



def check_build_environment(repo_root: Optional[str]) -> Tuple[bool, str, str]:
    """
    Checks if a firmware build tool (Native Ninja + ARM GCC or Docker + vbox-builder) is available.
    Returns:
        can_build (bool): True if project can be built.
        engine_name (str): 'NINJA', 'DOCKER', or 'NONE'.
        status_desc (str): Concise description and guidance.
    """
    if not repo_root or not os.path.isdir(repo_root):
        return False, "NONE", "Brak repozytorium projektu 5_v-box."

    # 1. Native Ninja + arm-none-eabi-gcc check
    ninja_tool = shutil.which("ninja")
    gcc_tool = shutil.which("arm-none-eabi-gcc")
    if ninja_tool and gcc_tool:
        return True, "NINJA", "Lokalny kompilator Ninja z toolchainem arm-none-eabi-gcc jest dostępny."

    # 2. Docker + vbox-builder image check
    docker_tool = shutil.which("docker")
    if docker_tool:
        try:
            r_info = subprocess.run(
                ["docker", "info"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=1.5
            )
            if r_info.returncode == 0:
                r_img = subprocess.run(
                    ["docker", "image", "inspect", "vbox-builder"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=1.5
                )
                if r_img.returncode == 0:
                    return True, "DOCKER", "Środowisko Docker Desktop i obraz vbox-builder są gotowe do kompilacji."
                else:
                    return False, "DOCKER_NO_IMAGE", "Usługa Docker działa, lecz obraz 'vbox-builder' nie został zbudowany."
            else:
                return False, "DOCKER_NOT_RUNNING", "Wykryto narzędzie Docker, lecz aplikacja Docker Desktop nie jest uruchomiona."
        except Exception:
            pass

    return False, "NONE", "Brak środowiska Docker Desktop oraz lokalnego kompilatora arm-none-eabi-gcc z ninja."


# ==============================================================================
# Worker Threads (QThread)
# ==============================================================================

class CanBusMonitorWorker(QThread):
    """
    Continuous background bus listener for 0x7E9 frames, device discovery, and recovery beacons.
    Safely pauses when an OTA flashing session is in progress.
    """
    sig_devices_updated = pyqtSignal(list, list)   # (devices, beacons)
    sig_recovery_beacon = pyqtSignal(dict)         # beacon_dict
    sig_bus_status = pyqtSignal(bool, str)         # (connected, status_msg)
    sig_log = pyqtSignal(str, str)                 # (level, message)

    def __init__(self, interface: str, channel: str, bitrate: int):
        super().__init__()
        self.interface = interface
        self.channel = channel
        self.bitrate = bitrate
        self.bus: Optional[can.BusABC] = None
        self._running = True
        self._flashing_active = False
        self._scan_requested = False
        self.auto_scan_enabled = False
        self._last_auto_scan = 0.0
        self._devices: List[Dict] = []
        self._beacons: List[Dict] = []
        self._seen_beacon_hashes = set()
        self._last_beacon_prune = 0.0

    def clear_all_beacons(self):
        """Clears all cached recovery beacons."""
        self._beacons = []
        self._seen_beacon_hashes.clear()
        self.sig_devices_updated.emit(self._devices, self._beacons)

    def clear_beacons_for_uid(self, uid_bytes: Optional[bytes]):
        """Clears recovery beacons matching the specified device UID."""
        if not uid_bytes:
            self.clear_all_beacons()
            return
        h_xor = can_updater.compute_mcu_uid_hash(uid_bytes)
        h_crc = zlib.crc32(uid_bytes) & 0xFF
        orig_len = len(self._beacons)
        self._beacons = [b for b in self._beacons if b.get("uid_hash") not in (h_xor, h_crc)]
        self._seen_beacon_hashes.discard(h_xor)
        self._seen_beacon_hashes.discard(h_crc)
        if len(self._devices) <= 1:
            self._beacons.clear()
        if len(self._beacons) != orig_len:
            self.sig_devices_updated.emit(self._devices, self._beacons)

    def _prune_stale_beacons(self):
        """
        Prunes expired beacons (no 0xEE frame seen in > 3.5s) and removes
        beacons for any devices currently confirmed in APPLICATION mode.
        """
        now = time.time()
        app_hashes = {d.get("uid_hash") for d in self._devices if d.get("mode") == "APPLICATION"}
        app_hashes.update({d.get("uid_crc_hash") for d in self._devices if d.get("mode") == "APPLICATION"})

        orig_len = len(self._beacons)
        if len(self._devices) == 1 and self._devices[0].get("mode") == "APPLICATION":
            self._beacons = []
        else:
            self._beacons = [
                b for b in self._beacons
                if (now - b.get("timestamp", 0.0)) < 3.5 and b.get("uid_hash") not in app_hashes
            ]

        if len(self._beacons) != orig_len:
            self.sig_devices_updated.emit(self._devices, self._beacons)

    def request_scan(self):
        """Triggers an active 0x7DF discovery scan on next loop cycle."""
        self._scan_requested = True

    def pause_for_flashing(self):
        """Pauses bus polling so OtaFlashWorker has exclusive access to the CAN bus."""
        self._flashing_active = True
        time.sleep(0.08)

    def resume_monitoring(self):
        """Resumes background bus polling after flashing finishes."""
        self._flashing_active = False

    def stop(self):
        """Stops the monitoring thread and releases bus resource."""
        self._running = False
        self.wait(1000)

    def _emit_log(self, level: str, msg: str):
        self.sig_log.emit(level, msg)

    def run(self):
        # Open CAN bus connection
        self.sig_log.emit("INFO", f"Otwieranie interfejsu '{self.interface}' na kanale '{self.channel}' @ {self.bitrate} bps...")
        try:
            if self.interface == "slcan":
                self.bus = can.interface.Bus(interface='slcan', channel=self.channel, bitrate=self.bitrate)
            else:
                self.bus = can.interface.Bus(interface=self.interface, channel=self.channel, bitrate=self.bitrate)
            self.sig_bus_status.emit(True, f"Połączono: {self.channel} @ {self.bitrate} bps")
            self.sig_log.emit("OK", f"Pomyślnie otwarto magistralę CAN ({self.channel} @ {self.bitrate} bps)")
        except Exception as e:
            self.sig_bus_status.emit(False, f"Błąd połączenia: {e}")
            self.sig_log.emit("ERROR", f"Nie udało się otworzyć portu CAN: {e}")
            return

        # Perform initial scan immediately on connect
        self._scan_requested = True

        while self._running:
            if self._flashing_active:
                time.sleep(0.1)
                continue

            now = time.time()

            # Periodic pruning of stale/expired recovery beacons (every 1.0s)
            if now - self._last_beacon_prune > 1.0:
                self._last_beacon_prune = now
                self._prune_stale_beacons()

            # Check if active discovery scan is requested
            if self._scan_requested or (self.auto_scan_enabled and (now - self._last_auto_scan) > 3.0):
                is_manual = self._scan_requested
                self._scan_requested = False
                self._last_auto_scan = now
                try:
                    new_beacons: List[Dict] = []
                    log_fn = self._emit_log if is_manual else None
                    found_devs = can_updater.scan_nodes(self.bus, timeout=0.35, verbose=False,
                                                         log_cb=log_fn, beacons_out=new_beacons)
                    self._devices = found_devs
                    for b in new_beacons:
                        self._process_beacon(b)
                    self._prune_stale_beacons()
                    self.sig_devices_updated.emit(self._devices, self._beacons)
                except Exception as e:
                    if is_manual:
                        self.sig_log.emit("WARN", f"Błąd podczas skanowania węzłów: {e}")

            # Passive reception of 0x7E9 frames
            try:
                rx_msg = self.bus.recv(timeout=0.04)
            except Exception:
                rx_msg = None

            if rx_msg and rx_msg.arbitration_id == can_updater.CAN_ID_OTA_DISCOVERY_RESP and len(rx_msg.data) >= 8:
                sub_idx = rx_msg.data[0]
                if sub_idx == 0xEE:
                    reason_code = rx_msg.data[1]
                    fail_cnt = rx_msg.data[2]
                    uid_hash = rx_msg.data[3]
                    pc = rx_msg.data[4] | (rx_msg.data[5] << 8) | (rx_msg.data[6] << 16) | (rx_msg.data[7] << 24)
                    reason_map = {
                        0x00: "NONE",
                        0x01: "HARDFAULT",
                        0x02: "RTOS_ASSERT",
                        0x03: "WATCHDOG",
                        0x04: "NO_APP (Blank/Missing Flash)"
                    }
                    beacon_dict = {
                        "reason_code": reason_code,
                        "reason_str": reason_map.get(reason_code, f"UNKNOWN(0x{reason_code:02X})"),
                        "fail_cnt": fail_cnt,
                        "uid_hash": uid_hash,
                        "pc": pc,
                        "timestamp": time.time()
                    }
                    self._process_beacon(beacon_dict)

            time.sleep(0.01)

        # Cleanup on exit
        if self.bus:
            try:
                self.bus.shutdown()
            except Exception:
                pass
            self.bus = None
        self.sig_bus_status.emit(False, "Rozłączono")
        self.sig_log.emit("INFO", "Zamknięto magistralę CAN.")

    def _process_beacon(self, beacon_dict: Dict):
        """Registers beacon and queries full 96-bit UID if needed."""
        uid_hash = beacon_dict["uid_hash"]

        # Drop beacon if matching device is already confirmed in APPLICATION mode
        matched_app = next(
            (d for d in self._devices if d.get("mode") == "APPLICATION" and (d.get("uid_hash") == uid_hash or d.get("uid_crc_hash") == uid_hash)),
            None
        )
        if matched_app or (len(self._devices) == 1 and self._devices[0].get("mode") == "APPLICATION"):
            return

        # Add or update in list
        existing = next((b for b in self._beacons if b["uid_hash"] == uid_hash), None)
        is_changed = False
        if existing:
            if (existing.get("reason_code") != beacon_dict.get("reason_code") or
                existing.get("fail_cnt") != beacon_dict.get("fail_cnt") or
                existing.get("pc") != beacon_dict.get("pc")):
                is_changed = True
            existing.update(beacon_dict)
        else:
            is_changed = True
            self._beacons.append(beacon_dict)

        # Rate-limit emission of sig_recovery_beacon (emit immediately on change, otherwise at most once every 2s)
        now = time.time()
        attr_name = f"_last_emit_{uid_hash}"
        last_emit = getattr(self, attr_name, 0.0)
        if is_changed or (now - last_emit > 2.0):
            setattr(self, attr_name, now)
            self.sig_recovery_beacon.emit(beacon_dict)

        # If full 96-bit UID not yet in devices, trigger discovery
        matched_dev = next((d for d in self._devices if d.get("uid_hash") == uid_hash or d.get("uid_crc_hash") == uid_hash), None)
        if not matched_dev and len(self._devices) == 1:
            matched_dev = self._devices[0]
        if not matched_dev and uid_hash not in self._seen_beacon_hashes:
            self._seen_beacon_hashes.add(uid_hash)
            # Broadcast discover to get full UID
            try:
                disc_frame = [0x02, can_updater.CMD_DISCOVER_NODES, 0, 0, 0, 0, 0, 0]
                msg = can.Message(arbitration_id=can_updater.CAN_ID_OTA_BROADCAST, is_extended_id=False, data=disc_frame)
                self.bus.send(msg)
            except Exception:
                pass


class OtaFlashWorker(QThread):
    """
    Background worker executing MainApp, Bootloader, or Dummy update session.
    """
    sig_step_changed = pyqtSignal(int, int, str)
    sig_progress = pyqtSignal(int, int, float, float, int)
    sig_log = pyqtSignal(str, str)
    sig_finished = pyqtSignal(bool, str)

    def __init__(self, bus: can.BusABC, target_uid: bytes, firmware_data: bytes,
                 update_type: str = "APP", app_address: int = 0x08008000,
                 block_size: int = 256, verbose: bool = False):
        super().__init__()
        self.bus = bus
        self.target_uid = target_uid
        self.firmware_data = firmware_data
        self.update_type = update_type  # "APP", "BOOTLOADER", "DUMMY"
        self.app_address = app_address
        self.block_size = block_size
        self.verbose = verbose
        self._cancelled = False

    def request_cancel(self):
        self._cancelled = True

    def _is_cancelled(self) -> bool:
        return self._cancelled

    def _on_progress(self, current: int, total: int, speed: float, eta: float, retries: int):
        self.sig_progress.emit(current, total, speed, eta, retries)

    def _on_step(self, step_idx: int, total_steps: int, title: str):
        self.sig_step_changed.emit(step_idx, total_steps, title)

    def _on_log(self, level: str, msg: str):
        self.sig_log.emit(level, msg)

    def run(self):
        success = False
        try:
            if self.update_type == "APP" or self.update_type == "DUMMY":
                is_dummy = (self.update_type == "DUMMY")
                success = can_updater.run_update_session(
                    bus=self.bus,
                    target_uid=self.target_uid,
                    firmware_data=self.firmware_data,
                    app_address=self.app_address,
                    block_size=self.block_size,
                    is_dummy=is_dummy,
                    verbose=self.verbose,
                    progress_cb=self._on_progress,
                    step_cb=self._on_step,
                    log_cb=self._on_log,
                    cancel_cb=self._is_cancelled
                )
            elif self.update_type == "BOOTLOADER":
                success = can_updater.run_bootloader_update_session(
                    bus=self.bus,
                    target_uid=self.target_uid,
                    bootloader_data=self.firmware_data,
                    block_size=self.block_size,
                    verbose=self.verbose,
                    progress_cb=self._on_progress,
                    step_cb=self._on_step,
                    log_cb=self._on_log,
                    cancel_cb=self._is_cancelled
                )
            else:
                self.sig_finished.emit(False, f"Nieobsługiwany typ aktualizacji: {self.update_type}")
                return

            if success:
                # Post-Flash Health Check
                self._on_log("INFO", "Weryfikacja po-aktualizacyjna (Post-Flash Health Check)... Oczekiwanie 600 ms na rozruch...")
                time.sleep(0.6)
                try:
                    verified_nodes = can_updater.scan_nodes(self.bus, timeout=0.8, verbose=False)
                    matched = next((n for n in verified_nodes if n["uid"] == self.target_uid), None)
                    if matched:
                        self._on_log("OK", f"UKŁAD POTWIERDZONY NA MAGISTRALI! Aktywny tryb: {matched['mode']} ({matched['version']})")
                        self.sig_finished.emit(True, f"Aktualizacja zakończona sukcesem! Układ działa w trybie {matched['mode']}.")
                    else:
                        self._on_log("WARN", "Układ zrestartował się, lecz nie odpowiedział na natychmiastowe zapytanie discovery.")
                        self.sig_finished.emit(True, "Aktualizacja zakończona sukcesem (zrestartowano układ).")
                except Exception:
                    self.sig_finished.emit(True, "Aktualizacja zakończona pomyślnie.")
            else:
                reason = "Anulowano przez użytkownika" if self._cancelled else "Wystąpił błąd transmisji lub odrzucenie ramki przez MCU"
                self.sig_finished.emit(False, reason)

        except Exception as e:
            self._on_log("ERROR", f"Wyjątek sesji aktualizacji: {e}")
            self.sig_finished.emit(False, str(e))


class BuildProjectWorker(QThread):
    """
    Asynchronously builds the firmware project (via Docker container or native Ninja/CMake)
    and streams build output to terminal.
    """
    sig_build_log = pyqtSignal(str)
    sig_build_finished = pyqtSignal(bool, str)

    def __init__(self, repo_root: str):
        super().__init__()
        self.repo_root = repo_root

    def run(self):
        self.sig_build_log.emit("=" * 60)
        self.sig_build_log.emit("[BUILD] Rozpoczynanie kompilacji projektu...")

        docker_bat = os.path.join(self.repo_root, ".devcontainer", "docker_build.bat")
        ninja_exe = shutil.which("ninja")
        is_docker_env = os.path.isfile("/.dockerenv")

        cmd = []
        if is_docker_env or (ninja_exe and os.path.isdir(os.path.join(self.repo_root, "build", "docker"))):
            cmd = ["ninja", "-C", "build/docker"]
        elif os.name == "nt" and os.path.isfile(docker_bat):
            cmd = ["cmd.exe", "/c", docker_bat]
        elif ninja_exe:
            cmd = ["ninja", "-C", "build/debug_GCC_NUCLEO-C542RC"]
        else:
            self.sig_build_finished.emit(False, "Nie znaleziono docker_build.bat ani kompilatora ninja w systemie.")
            return

        self.sig_build_log.emit(f"[BUILD] Uruchamianie: {' '.join(cmd)}")

        try:
            proc = subprocess.Popen(
                cmd,
                cwd=self.repo_root,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                universal_newlines=True
            )

            for line in iter(proc.stdout.readline, ''):
                if line:
                    self.sig_build_log.emit(line.rstrip())

            proc.stdout.close()
            ret = proc.wait()
            if ret == 0:
                self.sig_build_log.emit("[BUILD] KOMPILACJA ZAKOŃCZONA POMYŚLNIE!")
                self.sig_build_finished.emit(True, "Projekt został pomyślnie zbudowany.")
            else:
                self.sig_build_log.emit(f"[BUILD] Kompilacja zwróciła kod błędu: {ret}")
                self.sig_build_finished.emit(False, f"Błąd kompilacji (kod {ret})")

        except Exception as e:
            self.sig_build_log.emit(f"[BUILD ERROR] Wyjątek procesu budowania: {e}")
            self.sig_build_finished.emit(False, str(e))


class SerialUartWorker(QThread):
    """
    Background reader for ST-Link Virtual COM Port (UART2 @ 115200 bps).
    """
    sig_uart_line = pyqtSignal(str)
    sig_uart_status = pyqtSignal(bool, str)

    def __init__(self, port: str, baudrate: int = 115200):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self._running = True
        self._ser: Optional[serial.Serial] = None

    def stop(self):
        self._running = False
        if self._ser:
            try:
                self._ser.close()
            except Exception:
                pass
        self.wait(800)

    def run(self):
        try:
            self._ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.sig_uart_status.emit(True, f"Połączono: {self.port} @ {self.baudrate} bps")
        except Exception as e:
            self.sig_uart_status.emit(False, f"Błąd otwarcia {self.port}: {e}")
            return

        while self._running and self._ser and self._ser.is_open:
            try:
                line = self._ser.readline()
                if line:
                    try:
                        text = line.decode('utf-8', errors='replace').rstrip()
                        if text:
                            self.sig_uart_line.emit(text)
                    except Exception:
                        pass
            except Exception:
                break
            time.sleep(0.005)

        if self._ser:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None
        self.sig_uart_status.emit(False, "Rozłączono")


# ==============================================================================
# UI Custom Components
# ==============================================================================

class DeviceSummaryCard(QFrame):
    """
    Interactive KPI Tile with count, description, and steady status highlighting.
    """
    clicked = pyqtSignal()

    def __init__(self, title: str, subtitle: str, color_mode: str = "neutral"):
        super().__init__()
        self.title = title
        self.subtitle = subtitle
        self.color_mode = color_mode
        self._count = 0

        self.setObjectName("summaryCard")
        self.setFrameShape(QFrame.Shape.StyledPanel if QT_VERSION == 6 else QFrame.StyledPanel)
        self.setCursor(Qt.CursorShape.PointingHandCursor if QT_VERSION == 6 else Qt.PointingHandCursor)
        self.setFixedHeight(66)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 6, 12, 6)
        layout.setSpacing(1)

        self.lbl_title = QLabel(self.title)
        self.lbl_title.setStyleSheet("font-size: 10px; font-weight: 700; color: #94A3B8; text-transform: uppercase; border: none; background: transparent;")

        self.lbl_value = QLabel("0")
        self.lbl_value.setStyleSheet("font-size: 18px; font-weight: 800; color: #F8FAFC; border: none; background: transparent;")

        self.lbl_sub = QLabel(self.subtitle)
        self.lbl_sub.setStyleSheet("font-size: 10px; color: #64748B; border: none; background: transparent;")

        layout.addWidget(self.lbl_title)
        layout.addWidget(self.lbl_value)
        layout.addWidget(self.lbl_sub)

        self._apply_style()

    def set_value(self, count: int, display_text: Optional[str] = None):
        self._count = count
        self.lbl_value.setText(display_text if display_text else str(count))
        self._apply_style()

    def _apply_style(self):
        if self.color_mode == "alarm":
            if self._count > 0:
                border = "#DC2626"
                bg = "#250C0E"
                text_color = "#F87171"
                title_color = "#FCA5A5"
                sub_color = "#FCA5A5"
                self.lbl_title.setText("🚨 Wzywa Pomocy (Recovery)")
                self.lbl_sub.setText("Kliknij, aby przefiltrować")
            else:
                border = "#2D3748"
                bg = "#0B0F19"
                text_color = "#64748B"
                title_color = "#94A3B8"
                sub_color = "#64748B"
                self.lbl_title.setText("Wzywa Pomocy (Recovery)")
                self.lbl_sub.setText("Brak awarii (OK)")
        elif self.color_mode == "success":
            border = "#065F46"
            bg = "#061A14"
            text_color = "#34D399"
            title_color = "#94A3B8"
            sub_color = "#64748B"
        else:
            border = "#2D3748"
            bg = "#0B0F19"
            text_color = "#38BDF8"
            title_color = "#94A3B8"
            sub_color = "#64748B"

        self.lbl_title.setStyleSheet(f"font-size: 10px; font-weight: 700; color: {title_color}; text-transform: uppercase; border: none; background: transparent;")
        self.lbl_sub.setStyleSheet(f"font-size: 10px; color: {sub_color}; border: none; background: transparent;")
        self.lbl_value.setStyleSheet(f"font-size: 18px; font-weight: 800; color: {text_color}; border: none; background: transparent;")

        self.setStyleSheet(f"""
            QFrame#summaryCard {{
                background-color: {bg};
                border: 1px solid {border};
                border-radius: 8px;
            }}
            QFrame#summaryCard:hover {{
                border-color: #60A5FA;
            }}
            QLabel {{
                border: none;
                background-color: transparent;
            }}
        """)

    def mousePressEvent(self, event):
        self.clicked.emit()
        super().mousePressEvent(event)


class PipelineStepperWidget(QWidget):
    """
    Visual 5-step / 6-step progress pipeline with circular indicators and status labels.
    """
    def __init__(self):
        super().__init__()
        self.steps_app = [
            "1. Cel UID",
            "2. Reset Boot",
            "3. Kasowanie",
            "4. Zapis Bloków",
            "5. Weryfikacja"
        ]
        self.steps_boot = [
            "1. Cel UID",
            "2. Kasuj Staging",
            "3. Zapis Staging",
            "4. Weryfikuj CRC",
            "5. Instaluj Boot",
            "6. Restart"
        ]
        self.current_step = 0
        self.total_steps = 5
        self.step_labels: List[QLabel] = []
        self.step_circles: List[QLabel] = []
        self.step_lines: List[QFrame] = []

        self.main_layout = QHBoxLayout(self)
        self.main_layout.setContentsMargins(4, 4, 4, 4)
        self.main_layout.setSpacing(4)

        self.build_ui(is_bootloader=False)

    def build_ui(self, is_bootloader: bool = False):
        # Deep clear existing layout and widgets
        while self.main_layout.count():
            item = self.main_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
            elif item.layout():
                child = item.layout()
                while child.count():
                    c_item = child.takeAt(0)
                    if c_item.widget():
                        c_item.widget().deleteLater()

        steps = self.steps_boot if is_bootloader else self.steps_app
        self.total_steps = len(steps)
        self.step_circles = []
        self.step_labels = []
        self.step_lines = []

        for idx, title in enumerate(steps, 1):
            box = QVBoxLayout()
            box.setAlignment(ALIGN_CENTER)
            box.setSpacing(2)

            circle = QLabel(str(idx))
            circle.setFixedSize(26, 26)
            circle.setAlignment(ALIGN_CENTER)
            circle.setStyleSheet("""
                background-color: #1E293B;
                color: #94A3B8;
                border: 2px solid #334155;
                border-radius: 13px;
                font-weight: bold;
                font-size: 11px;
            """)

            label = QLabel(title)
            label.setAlignment(ALIGN_CENTER)
            label.setStyleSheet("font-size: 10px; color: #64748B; font-weight: 500;")

            box.addWidget(circle, 0, ALIGN_CENTER)
            box.addWidget(label, 0, ALIGN_CENTER)

            self.main_layout.addLayout(box)
            self.step_circles.append(circle)
            self.step_labels.append(label)

            if idx < len(steps):
                line_box = QVBoxLayout()
                line_box.setContentsMargins(0, 0, 0, 0)
                line_box.setSpacing(0)
                line_box.addSpacing(12)  # Align with center of 26px circle
                line = QFrame()
                line.setFixedHeight(2)
                line.setStyleSheet("background-color: #334155;")
                line_box.addWidget(line)
                line_box.addStretch(1)
                self.main_layout.addLayout(line_box, 1)
                self.step_lines.append(line)

    def set_step(self, step_idx: int, total_steps: int):
        self.current_step = step_idx
        for i, (c, l) in enumerate(zip(self.step_circles, self.step_labels), 1):
            if i < step_idx:
                c.setText("✓")
                c.setStyleSheet("background-color: #065F46; color: #34D399; border: 2px solid #10B981; border-radius: 13px; font-weight: bold;")
                l.setStyleSheet("font-size: 10px; color: #10B981; font-weight: 600;")
            elif i == step_idx:
                c.setText(str(i))
                c.setStyleSheet("background-color: #1E3A8A; color: #38BDF8; border: 2px solid #38BDF8; border-radius: 13px; font-weight: bold;")
                l.setStyleSheet("font-size: 10px; color: #38BDF8; font-weight: 700;")
            else:
                c.setText(str(i))
                c.setStyleSheet("background-color: #1E293B; color: #64748B; border: 2px solid #334155; border-radius: 13px;")
                l.setStyleSheet("font-size: 10px; color: #475569; font-weight: 500;")

        # Update connecting lines
        for idx, line in enumerate(self.step_lines, 1):
            if idx < step_idx:
                line.setStyleSheet("background-color: #10B981;")
            else:
                line.setStyleSheet("background-color: #334155;")

    def reset(self):
        self.current_step = 0
        for i, (c, l) in enumerate(zip(self.step_circles, self.step_labels), 1):
            c.setText(str(i))
            c.setStyleSheet("background-color: #1E293B; color: #94A3B8; border: 2px solid #334155; border-radius: 13px; font-weight: bold;")
            l.setStyleSheet("font-size: 10px; color: #64748B; font-weight: 500;")
        for line in self.step_lines:
            line.setStyleSheet("background-color: #334155;")


# ==============================================================================
# Main Window: CanUpdaterMainWindow
# ==============================================================================

class CanUpdaterMainWindow(QMainWindow):
    """
    Main application window implementing the 5 logical zones from the architecture plan.
    """
    sig_pc_decoded = pyqtSignal(int, str)

    def __init__(self, init_port: Optional[str] = None, init_bitrate: int = 500000):
        super().__init__()
        self.init_port = init_port
        self.init_bitrate = init_bitrate

        self.repo_root = get_vbox_repo_root()
        self.monitor_worker: Optional[CanBusMonitorWorker] = None
        self.flash_worker: Optional[OtaFlashWorker] = None
        self.build_worker: Optional[BuildProjectWorker] = None
        self.uart_worker: Optional[SerialUartWorker] = None

        self.devices_cache: List[Dict] = []
        self.beacons_cache: List[Dict] = []
        self.selected_target_uid: Optional[bytes] = None
        self.loaded_firmware_data: Optional[bytes] = None
        self.current_firmware_path: Optional[str] = None
        self.current_filter_mode = "ALL"  # "ALL", "READY", "ALARM"
        self._last_logged_beacon_key = None

        self.sig_pc_decoded.connect(self._on_pc_async_decoded)

        self.setWindowTitle("V-BOX CAN OTA UPDATER & DIAGNOSTIC STUDIO (STM32C542 / FDCAN2)")
        self.resize(1200, 860)
        self.setMinimumSize(1180, 840)
        self.setStyleSheet(DARK_ENGINEERING_THEME)

        # Enable Drag and Drop
        self.setAcceptDrops(True)

        self._init_ui()
        self._update_build_system_state()
        self._refresh_com_ports()

        # Auto-connect if initial port provided
        if self.init_port:
            self._connect_can_bus()

    # --------------------------------------------------------------------------
    # UI Construction
    # --------------------------------------------------------------------------
    def _init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_vbox = QVBoxLayout(central_widget)
        main_vbox.setContentsMargins(14, 8, 14, 8)
        main_vbox.setSpacing(6)

        # Zone 1: Connection Panel
        main_vbox.addWidget(self._create_connection_panel())

        # Zone 2: Fleet Dashboard (3 KPI Cards)
        main_vbox.addWidget(self._create_fleet_dashboard())

        # Zone 3: Device Table Panel
        main_vbox.addWidget(self._create_device_table_panel())

        # Zone 4: Lower sub-zone (Config & Pipeline Stepper side by side)
        lower_widget = QWidget()
        lower_hbox = QHBoxLayout(lower_widget)
        lower_hbox.setContentsMargins(0, 0, 0, 0)
        lower_hbox.setSpacing(10)

        lower_hbox.addWidget(self._create_firmware_config_panel(), 1)
        lower_hbox.addWidget(self._create_progress_pipeline_panel(), 1)
        main_vbox.addWidget(lower_widget)

        # Zone 5: Dual Terminal Panel (CAN ISO-TP Log + UART2 Serial Monitor)
        terminal_panel = self._create_dual_terminal_panel()
        terminal_panel.setMinimumHeight(140)
        main_vbox.addWidget(terminal_panel, 1)

    def _create_connection_panel(self) -> QGroupBox:
        grp = QGroupBox("POŁĄCZENIE MAGISTRALI CAN (CANable 2.0 / SLCAN)")
        layout = QHBoxLayout(grp)
        layout.setContentsMargins(10, 6, 10, 6)
        layout.setSpacing(6)

        # Interface
        layout.addWidget(QLabel("Interfejs:"))
        self.combo_iface = QComboBox()
        self.combo_iface.addItems(["slcan", "socketcan"])
        self.combo_iface.setFixedWidth(85)
        layout.addWidget(self.combo_iface)

        # COM Port
        layout.addWidget(QLabel("Port COM:"))
        self.combo_port = QComboBox()
        self.combo_port.setMinimumWidth(180)
        layout.addWidget(self.combo_port, 1)

        self.btn_refresh_ports = QPushButton("🔄")
        self.btn_refresh_ports.setToolTip("Odśwież listę portów szeregowych")
        self.btn_refresh_ports.setFixedSize(28, 26)
        self.btn_refresh_ports.clicked.connect(self._refresh_com_ports)
        layout.addWidget(self.btn_refresh_ports)

        # Bitrate
        layout.addWidget(QLabel("Prędkość:"))
        self.combo_bitrate = QComboBox()
        self.combo_bitrate.addItems(["500000 bps", "250000 bps", "1000000 bps", "125000 bps"])
        self.combo_bitrate.setFixedWidth(105)
        layout.addWidget(self.combo_bitrate)

        # Connect / Disconnect Button
        self.btn_connect = QPushButton("🔌 Połącz")
        self.btn_connect.setObjectName("btn_primary")
        self.btn_connect.setFixedWidth(90)
        self.btn_connect.clicked.connect(self._toggle_can_connection)
        layout.addWidget(self.btn_connect)

        # Status Badge
        self.lbl_bus_status = QLabel("(● ROZŁĄCZONO)")
        self.lbl_bus_status.setStyleSheet("color: #94A3B8; font-weight: bold; margin-left: 2px; margin-right: 6px;")
        layout.addWidget(self.lbl_bus_status)

        # Auto-scan Checkbox
        self.chk_auto_scan = QCheckBox("Auto-Scan (3s)")
        self.chk_auto_scan.setChecked(False)
        self.chk_auto_scan.toggled.connect(self._on_auto_scan_toggled)
        layout.addWidget(self.chk_auto_scan)

        return grp

    def _create_fleet_dashboard(self) -> QWidget:
        widget = QWidget()
        layout = QHBoxLayout(widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)

        self.card_all = DeviceSummaryCard("🖧 Wykryte Urządzenia", "Kliknij, aby pokazać wszystkie", "neutral")
        self.card_all.clicked.connect(lambda: self._filter_devices("ALL"))

        self.card_ready = DeviceSummaryCard("✅ Gotowe do Pracy", "Application / Bootloader", "success")
        self.card_ready.clicked.connect(lambda: self._filter_devices("READY"))

        self.card_alarm = DeviceSummaryCard("Wzywa Pomocy (Recovery)", "Brak awarii (OK)", "alarm")
        self.card_alarm.clicked.connect(lambda: self._filter_devices("ALARM"))

        layout.addWidget(self.card_all, 1)
        layout.addWidget(self.card_ready, 1)
        layout.addWidget(self.card_alarm, 1)

        return widget

    def _create_device_table_panel(self) -> QGroupBox:
        grp = QGroupBox("TABELA WYKRYTYCH WĘZŁÓW STM32")
        grp.setMinimumHeight(135)
        grp.setMaximumHeight(160)
        layout = QVBoxLayout(grp)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(6)

        # Action bar right above table
        header_bar = QHBoxLayout()
        header_bar.setSpacing(6)
        lbl_hint = QLabel("Wybierz urządzenie z tabeli poniżej:")
        lbl_hint.setStyleSheet("color: #94A3B8; font-size: 11px;")
        header_bar.addWidget(lbl_hint, 1)

        # Prominent scan button
        self.btn_scan_table = QPushButton("🔍 Skanuj (0x7DF)")
        self.btn_scan_table.setObjectName("btn_scan_table")
        self.btn_scan_table.setStyleSheet("""
            QPushButton#btn_scan_table {
                background-color: #0284C7;
                border: 1px solid #38BDF8;
                color: #FFFFFF;
                font-weight: 700;
                font-size: 11px;
                padding: 4px 12px;
                border-radius: 6px;
            }
            QPushButton#btn_scan_table:hover {
                background-color: #0369A1;
                border-color: #7DD3FC;
            }
        """)
        self.btn_scan_table.setEnabled(False)
        self.btn_scan_table.clicked.connect(self._request_bus_scan)
        header_bar.addWidget(self.btn_scan_table)

        # Quick Diagnostic Actions placed alongside table
        self.btn_quick_boot = QPushButton("📥 Do Bootloadera")
        self.btn_quick_boot.setEnabled(False)
        self.btn_quick_boot.setStyleSheet("font-size: 11px; padding: 4px 10px;")
        self.btn_quick_boot.setToolTip("Wskaż urządzenie z tabeli poniżej, aby wymusić przejście w tryb Bootloadera.")
        self.btn_quick_boot.clicked.connect(self._quick_enter_bootloader)
        header_bar.addWidget(self.btn_quick_boot)

        self.btn_quick_app = QPushButton("▶️ Skocz do App")
        self.btn_quick_app.setEnabled(False)
        self.btn_quick_app.setStyleSheet("font-size: 11px; padding: 4px 10px;")
        self.btn_quick_app.setToolTip("Wskaż urządzenie z tabeli poniżej, aby zrestartować MCU i uruchomić Aplikację.")
        self.btn_quick_app.clicked.connect(self._quick_jump_app)
        header_bar.addWidget(self.btn_quick_app)

        self.btn_quick_addr2line = QPushButton("🔎 Dekoduj PC")
        self.btn_quick_addr2line.setStyleSheet("font-size: 11px; padding: 4px 10px;")
        self.btn_quick_addr2line.setToolTip(
            "Tłumaczy szesnastkowy adres rejestru PC (Program Counter) mikrokontrolera\n"
            "(np. 0x08008124) na dokładną nazwę funkcji w języku C oraz plik i numer linii\n"
            "(np. HardFault_Handler() [stm32c5xx_it.c:54]) przy użyciu narzędzia addr2line i symboli v_box.elf."
        )
        self.btn_quick_addr2line.clicked.connect(self._quick_decode_selected_pc)
        header_bar.addWidget(self.btn_quick_addr2line)

        layout.addLayout(header_bar)

        self.table_devices = QTableWidget(0, 6)
        self.table_devices.setHorizontalHeaderLabels([
            "Status", "96-bitowy UID", "Hash", "Tryb Pracy", "Wersja FW", "Diagnostyka / Crash Info"
        ])
        self.table_devices.setSelectionBehavior(TABLE_SELECT_ROWS)
        self.table_devices.setSelectionMode(TABLE_SINGLE_SELECTION)
        self.table_devices.verticalHeader().setVisible(False)
        self.table_devices.verticalHeader().setDefaultSectionSize(26)
        self.table_devices.setMinimumHeight(75)
        self.table_devices.setMaximumHeight(105)
        if QT_VERSION == 6:
            self.table_devices.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
            self.table_devices.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        else:
            self.table_devices.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
            self.table_devices.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)

        self.table_devices.horizontalHeader().setSectionResizeMode(0, HEADER_RESIZE_TO_CONTENTS)
        self.table_devices.horizontalHeader().setSectionResizeMode(1, HEADER_RESIZE_TO_CONTENTS)
        self.table_devices.horizontalHeader().setSectionResizeMode(2, HEADER_RESIZE_TO_CONTENTS)
        self.table_devices.horizontalHeader().setSectionResizeMode(3, HEADER_RESIZE_TO_CONTENTS)
        self.table_devices.horizontalHeader().setSectionResizeMode(4, HEADER_RESIZE_TO_CONTENTS)
        self.table_devices.horizontalHeader().setSectionResizeMode(5, HEADER_STRETCH)
        self.table_devices.itemClicked.connect(self._on_table_row_clicked)
        self.table_devices.itemSelectionChanged.connect(self._on_table_selection_changed)

        layout.addWidget(self.table_devices)
        return grp


    def _create_firmware_config_panel(self) -> QGroupBox:
        grp = QGroupBox("KONFIGURACJA AKTUALIZACJI I WYBÓR WSADU")
        grp.setMinimumHeight(200)
        grp.setMaximumHeight(225)
        layout = QVBoxLayout(grp)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(5)

        # Target selection row
        row_target = QHBoxLayout()
        row_target.setSpacing(6)
        row_target.addWidget(QLabel("Docelowy UID:"))
        self.txt_target_uid = QLineEdit()
        self.txt_target_uid.setFixedHeight(26)
        self.txt_target_uid.setPlaceholderText("Wybierz urządzenie z tabeli powyżej...")
        self.txt_target_uid.textChanged.connect(self._on_target_uid_changed)
        row_target.addWidget(self.txt_target_uid, 1)

        self.btn_clear_uid = QPushButton("✕")
        self.btn_clear_uid.setFixedSize(26, 26)
        self.btn_clear_uid.clicked.connect(lambda: self.txt_target_uid.clear())
        row_target.addWidget(self.btn_clear_uid)
        layout.addLayout(row_target)

        # Update Target Radio Buttons
        row_radio = QHBoxLayout()
        row_radio.setSpacing(10)
        lbl_target_type = QLabel("Wsad:")
        lbl_target_type.setStyleSheet("font-weight: 600; color: #94A3B8;")
        row_radio.addWidget(lbl_target_type)
        self.rad_app = QRadioButton("Aplikacja (App)")
        self.rad_app.setToolTip("Główna aplikacja w pamięci Flash pod adresem 0x08008000")
        self.rad_boot = QRadioButton("Bootloader")
        self.rad_boot.setToolTip("Aktualizacja bootloadera przez Staging Slot (0x08038000 -> 0x08000000)")
        self.rad_dummy = QRadioButton("Test RAM (Dummy)")
        self.rad_dummy.setToolTip("Bezpieczny test transmisji magistrali CAN w pamięci RAM")
        self.rad_app.setChecked(True)

        self.btn_group_target = QButtonGroup(self)
        self.btn_group_target.addButton(self.rad_app, 1)
        self.btn_group_target.addButton(self.rad_boot, 2)
        self.btn_group_target.addButton(self.rad_dummy, 3)
        self.btn_group_target.buttonClicked.connect(self._on_target_type_changed)

        row_radio.addWidget(self.rad_app)
        row_radio.addWidget(self.rad_boot)
        row_radio.addWidget(self.rad_dummy)
        row_radio.addStretch(1)
        layout.addLayout(row_radio)

        # File Selection Row
        row_file_label = QHBoxLayout()
        row_file_label.setSpacing(6)
        row_file_label.addWidget(QLabel("Plik wsadowy (.bin):"))
        self.lbl_stale_warning = QLabel("")
        self.lbl_stale_warning.setStyleSheet("color: #F59E0B; font-weight: bold; font-size: 11px;")
        row_file_label.addWidget(self.lbl_stale_warning, 1, ALIGN_RIGHT)
        layout.addLayout(row_file_label)

        row_file = QHBoxLayout()
        row_file.setSpacing(6)
        self.txt_file_path = QLineEdit()
        self.txt_file_path.setFixedHeight(26)
        self.txt_file_path.setPlaceholderText("Przeciągnij plik .bin tutaj lub wybierz z dysku...")
        self.txt_file_path.textChanged.connect(self._on_file_path_changed)
        row_file.addWidget(self.txt_file_path, 1)

        self.btn_browse = QPushButton("📂 Przeglądaj...")
        self.btn_browse.setFixedHeight(26)
        self.btn_browse.clicked.connect(self._browse_firmware_file)
        row_file.addWidget(self.btn_browse)
        layout.addLayout(row_file)

        # Repo-Aware options (build buttons)
        if self.repo_root:
            row_repo = QHBoxLayout()
            row_repo.setSpacing(6)
            self.chk_use_repo_artifact = QCheckBox("Użyj artefaktu z build/ (Auto-Path)")
            self.chk_use_repo_artifact.setChecked(True)
            self.chk_use_repo_artifact.setToolTip(
                "Automatycznie wskazuje najnowszy plik binarny (.bin) skompilowany w repozytorium:\n"
                "• Dla Aplikacji: build/debug_GCC_NUCLEO-C542RC/v_box.bin\n"
                "• Dla Bootloadera: build/debug_GCC_NUCLEO-C542RC/v_box_boot.bin\n\n"
                "Gdy ta opcja jest włączona, po każdej nowej kompilacji ścieżka i wsad\n"
                "są natychmiast odświeżane bez konieczności ręcznego szukania pliku na dysku."
            )
            self.chk_use_repo_artifact.toggled.connect(self._on_use_repo_artifact_toggled)
            row_repo.addWidget(self.chk_use_repo_artifact, 1)

            self.btn_build_project = QPushButton("🔨 Zbuduj Projekt")
            self.btn_build_project.setFixedHeight(26)
            self.btn_build_project.clicked.connect(self._build_project_now)
            row_repo.addWidget(self.btn_build_project)

            self.btn_build_help = QPushButton("ℹ️")
            self.btn_build_help.setFixedSize(26, 26)
            self.btn_build_help.setToolTip("Instrukcja konfiguracji środowiska kompilacji (Docker Desktop / Ninja)")
            self.btn_build_help.clicked.connect(self._show_build_instructions)
            row_repo.addWidget(self.btn_build_help)

            layout.addLayout(row_repo)

        # Pre-Flight Binary Guard Box
        self.frame_preflight = QFrame()
        self.frame_preflight.setObjectName("framePreflight")
        self.frame_preflight.setMinimumHeight(48)
        self.frame_preflight.setStyleSheet("""
            QFrame#framePreflight {
                background-color: #0B0F19;
                border: 1px solid #334155;
                border-radius: 6px;
            }
            QLabel {
                border: none;
                background-color: transparent;
            }
        """)
        pf_vbox = QVBoxLayout(self.frame_preflight)
        pf_vbox.setContentsMargins(8, 4, 8, 4)
        pf_vbox.setSpacing(2)

        self.lbl_pf_status = QLabel("Pre-Flight Guard: Oczekiwanie na wybór pliku .bin...")
        self.lbl_pf_status.setStyleSheet("font-weight: bold; color: #94A3B8; border: none; background: transparent;")
        self.lbl_pf_details = QLabel("Rozmiar: — | Wektory: — | CRC32: —")
        self.lbl_pf_details.setStyleSheet("font-family: monospace; font-size: 11px; color: #64748B; border: none; background: transparent;")

        pf_vbox.addWidget(self.lbl_pf_status)
        pf_vbox.addWidget(self.lbl_pf_details)
        layout.addWidget(self.frame_preflight)

        return grp

    def _create_progress_pipeline_panel(self) -> QGroupBox:
        grp = QGroupBox("POTOK WYKONANIA (PIPELINE) I TELEMETRIA")
        grp.setMinimumHeight(200)
        grp.setMaximumHeight(225)
        layout = QVBoxLayout(grp)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(6)

        # Stepper Widget
        self.stepper_widget = PipelineStepperWidget()
        layout.addWidget(self.stepper_widget)

        # Progress bar
        self.progress_bar = QProgressBar()
        self.progress_bar.setValue(0)
        self.progress_bar.setFormat("Oczekiwanie... (0%)")
        layout.addWidget(self.progress_bar)

        # Telemetry metrics row
        row_telemetry = QHBoxLayout()
        self.lbl_tel_speed = QLabel("Prędkość: 0.0 KB/s")
        self.lbl_tel_speed.setStyleSheet("color: #38BDF8; font-weight: 600;")
        self.lbl_tel_eta = QLabel("ETA: —")
        self.lbl_tel_eta.setStyleSheet("color: #94A3B8;")
        self.lbl_tel_block = QLabel("Bloki: 0 / 0")
        self.lbl_tel_block.setStyleSheet("color: #94A3B8;")
        self.lbl_tel_retries = QLabel("Ponowienia: 0")
        self.lbl_tel_retries.setStyleSheet("color: #94A3B8;")

        row_telemetry.addWidget(self.lbl_tel_speed)
        row_telemetry.addWidget(self.lbl_tel_eta)
        row_telemetry.addWidget(self.lbl_tel_block)
        row_telemetry.addWidget(self.lbl_tel_retries)
        layout.addLayout(row_telemetry)

        # Action Buttons
        row_actions = QHBoxLayout()
        self.btn_start_flash = QPushButton("🚀 ROZPOCZNIJ AKTUALIZACJĘ")
        self.btn_start_flash.setObjectName("btn_success")
        self.btn_start_flash.setStyleSheet("padding: 8px 18px; font-size: 13px; font-weight: bold;")
        self.btn_start_flash.clicked.connect(self._start_flashing_session)
        row_actions.addWidget(self.btn_start_flash, 2)

        self.btn_cancel_flash = QPushButton("⏹ Przerwij")
        self.btn_cancel_flash.setObjectName("btn_danger")
        self.btn_cancel_flash.setEnabled(False)
        self.btn_cancel_flash.clicked.connect(self._cancel_flashing_session)
        row_actions.addWidget(self.btn_cancel_flash, 1)
        layout.addLayout(row_actions)

        return grp

    def _create_dual_terminal_panel(self) -> QTabWidget:
        self.tabs_terminal = QTabWidget()

        # Tab 1: CAN / ISO-TP Transmission Log
        tab_can = QWidget()
        vbox_can = QVBoxLayout(tab_can)
        vbox_can.setContentsMargins(6, 6, 6, 6)

        self.txt_log_can = QPlainTextEdit()
        self.txt_log_can.setReadOnly(True)
        vbox_can.addWidget(self.txt_log_can)

        row_can_ctrl = QHBoxLayout()
        self.chk_auto_scroll_can = QCheckBox("Auto-scroll")
        self.chk_auto_scroll_can.setChecked(True)
        row_can_ctrl.addWidget(self.chk_auto_scroll_can)

        self.chk_verbose_can = QCheckBox("Verbose ISO-TP")
        self.chk_verbose_can.setToolTip(
            "Włącza szczegółowy podgląd niskopoziomowych ramek CAN (SF, FF, CF, FC) protokołu ISO-TP (ISO 15765-2).\n\n"
            "• Zaznaczone: w terminalu widać każdy pojedynczy bajt i ramkę magistrali CAN.\n"
            "• Odznaczone (zalecane): wyświetlane są tylko etapy i podsumowania bloków, co zapobiega zalewaniu logów tysiącami linii."
        )
        row_can_ctrl.addWidget(self.chk_verbose_can)

        row_can_ctrl.addStretch(1)

        btn_copy_can = QPushButton("📋 Kopiuj")
        btn_copy_can.clicked.connect(lambda: QApplication.clipboard().setText(self.txt_log_can.toPlainText()))
        row_can_ctrl.addWidget(btn_copy_can)

        btn_save_can = QPushButton("💾 Zapisz log...")
        btn_save_can.clicked.connect(lambda: self._save_log_to_file(self.txt_log_can.toPlainText()))
        row_can_ctrl.addWidget(btn_save_can)

        btn_clear_can = QPushButton("🗑️ Wyczyść")
        btn_clear_can.clicked.connect(self.txt_log_can.clear)
        row_can_ctrl.addWidget(btn_clear_can)

        vbox_can.addLayout(row_can_ctrl)
        self.tabs_terminal.addTab(tab_can, "Log Transmisji CAN / ISO-TP")

        # Tab 2: MCU UART2 Serial Monitor
        tab_uart = QWidget()
        vbox_uart = QVBoxLayout(tab_uart)
        vbox_uart.setContentsMargins(6, 6, 6, 6)

        row_uart_conn = QHBoxLayout()
        row_uart_conn.addWidget(QLabel("Port ST-Link VCP:"))
        self.combo_uart_port = QComboBox()
        self.combo_uart_port.setMinimumWidth(260)
        row_uart_conn.addWidget(self.combo_uart_port, 1)

        self.btn_uart_connect = QPushButton("🔌 Połącz UART2 (115200)")
        self.btn_uart_connect.clicked.connect(self._toggle_uart_connection)
        row_uart_conn.addWidget(self.btn_uart_connect)

        self.lbl_uart_status = QLabel("(Rozłączono)")
        self.lbl_uart_status.setStyleSheet("color: #64748B; font-weight: bold;")
        row_uart_conn.addWidget(self.lbl_uart_status)

        vbox_uart.addLayout(row_uart_conn)

        self.txt_log_uart = QPlainTextEdit()
        self.txt_log_uart.setReadOnly(True)
        vbox_uart.addWidget(self.txt_log_uart)

        row_uart_ctrl = QHBoxLayout()
        self.chk_auto_scroll_uart = QCheckBox("Auto-scroll")
        self.chk_auto_scroll_uart.setChecked(True)
        row_uart_ctrl.addWidget(self.chk_auto_scroll_uart)

        row_uart_ctrl.addStretch(1)

        btn_copy_uart = QPushButton("📋 Kopiuj")
        btn_copy_uart.clicked.connect(lambda: QApplication.clipboard().setText(self.txt_log_uart.toPlainText()))
        row_uart_ctrl.addWidget(btn_copy_uart)

        btn_save_uart = QPushButton("💾 Zapisz log...")
        btn_save_uart.clicked.connect(lambda: self._save_log_to_file(self.txt_log_uart.toPlainText()))
        row_uart_ctrl.addWidget(btn_save_uart)

        btn_clear_uart = QPushButton("🗑️ Wyczyść")
        btn_clear_uart.clicked.connect(self.txt_log_uart.clear)
        row_uart_ctrl.addWidget(btn_clear_uart)

        vbox_uart.addLayout(row_uart_ctrl)
        self.tabs_terminal.addTab(tab_uart, "Terminal Szeregowy MCU UART2 (ST-Link VCP)")

        return self.tabs_terminal

    # --------------------------------------------------------------------------
    # COM Port Handling
    # --------------------------------------------------------------------------
    def _refresh_com_ports(self):
        ports = find_serial_ports()
        self.combo_port.clear()
        self.combo_uart_port.clear()

        canable_idx = -1
        stlink_idx = -1

        for idx, p in enumerate(ports):
            self.combo_port.addItem(p["label"], p["device"])
            self.combo_uart_port.addItem(p["label"], p["device"])

            if p["is_canable"] and canable_idx == -1:
                canable_idx = idx
            if p["is_stlink"] and stlink_idx == -1:
                stlink_idx = idx

        # Auto-select best ports
        if canable_idx != -1:
            self.combo_port.setCurrentIndex(canable_idx)
        elif self.combo_port.count() > 0:
            self.combo_port.setCurrentIndex(0)

        if stlink_idx != -1:
            self.combo_uart_port.setCurrentIndex(stlink_idx)
        elif self.combo_uart_port.count() > 0:
            self.combo_uart_port.setCurrentIndex(0)

        # Auto-update default file path if in repo
        if self.repo_root and hasattr(self, 'chk_use_repo_artifact') and self.chk_use_repo_artifact.isChecked():
            self._update_default_repo_path()

    def _toggle_can_connection(self):
        if self.monitor_worker and self.monitor_worker.isRunning():
            # Disconnect
            self.monitor_worker.stop()
            self.monitor_worker = None
            self.btn_connect.setText("🔌 Połącz")
            self.btn_connect.setObjectName("btn_primary")
            if hasattr(self, "btn_scan_table"):
                self.btn_scan_table.setEnabled(False)
            self.lbl_bus_status.setText("(● ROZŁĄCZONO)")
            self.lbl_bus_status.setStyleSheet("color: #94A3B8; font-weight: bold;")
        else:
            self._connect_can_bus()

    def _connect_can_bus(self):
        if self.combo_port.count() == 0:
            QMessageBox.warning(self, "Brak portów", "Nie wykryto żadnego portu szeregowego na komputerze!")
            return

        channel = self.combo_port.currentData() or self.combo_port.currentText().split()[0]
        iface = self.combo_iface.currentText()
        bitrate_str = self.combo_bitrate.currentText().split()[0]
        bitrate = int(bitrate_str)

        self.monitor_worker = CanBusMonitorWorker(interface=iface, channel=channel, bitrate=bitrate)
        self.monitor_worker.auto_scan_enabled = self.chk_auto_scan.isChecked()
        self.monitor_worker.sig_devices_updated.connect(self._on_devices_updated)
        self.monitor_worker.sig_recovery_beacon.connect(self._on_recovery_beacon_received)
        self.monitor_worker.sig_bus_status.connect(self._on_bus_status_changed)
        self.monitor_worker.sig_log.connect(self._append_can_log)
        self.monitor_worker.start()

        self.btn_connect.setText("🔌 Rozłącz")
        self.btn_connect.setObjectName("btn_danger")
        if hasattr(self, "btn_scan_table"):
            self.btn_scan_table.setEnabled(True)

    def _on_bus_status_changed(self, connected: bool, msg: str):
        if connected:
            self.lbl_bus_status.setText(f"(● {msg})")
            self.lbl_bus_status.setStyleSheet("color: #10B981; font-weight: bold;")
        else:
            self.lbl_bus_status.setText("(● ROZŁĄCZONO)")
            self.lbl_bus_status.setStyleSheet("color: #EF4444; font-weight: bold;")
            self.btn_connect.setText("🔌 Połącz")
            self.btn_connect.setObjectName("btn_primary")
            if hasattr(self, "btn_scan_table"):
                self.btn_scan_table.setEnabled(False)

    def _request_bus_scan(self):
        if self.monitor_worker:
            self.monitor_worker.request_scan()

    def _on_auto_scan_toggled(self, checked: bool):
        if self.monitor_worker:
            self.monitor_worker.auto_scan_enabled = checked

    # --------------------------------------------------------------------------
    # Device Table & Fleet Telemetry Handling
    # --------------------------------------------------------------------------
    def _on_devices_updated(self, devices: List[Dict], beacons: List[Dict]):
        # Prune beacons against devices confirmed in APPLICATION mode
        app_hashes = {d.get("uid_hash") for d in devices if d.get("mode") == "APPLICATION"}
        app_hashes.update({d.get("uid_crc_hash") for d in devices if d.get("mode") == "APPLICATION"})

        if devices and all(d.get("mode") == "APPLICATION" for d in devices):
            beacons = []
        elif app_hashes:
            beacons = [b for b in beacons if b.get("uid_hash") not in app_hashes]

        self.devices_cache = devices
        self.beacons_cache = beacons
        self._refresh_device_table()
        self._update_kpi_cards()
        if len(devices) == 1 and not self.selected_target_uid:
            d = devices[0]
            uid_str = d.get("uid_str") or (can_updater.format_uid(d["uid"]) if d.get("uid") else None)
            if uid_str:
                self.txt_target_uid.setText(uid_str)

    def _on_recovery_beacon_received(self, beacon: Dict):
        # Update device table if node already present or add placeholder
        hash_val = beacon.get("uid_hash", 0)

        # Drop beacon if device is already confirmed running APPLICATION mode
        matched_dev = next((d for d in self.devices_cache if d.get("uid_hash") == hash_val or d.get("uid_crc_hash") == hash_val), None)
        if (matched_dev and matched_dev.get("mode") == "APPLICATION") or (len(self.devices_cache) == 1 and self.devices_cache[0].get("mode") == "APPLICATION"):
            return

        existing = next((b for b in self.beacons_cache if b.get("uid_hash") == hash_val), None)
        is_changed = False
        if existing:
            if (existing.get("reason_code") != beacon.get("reason_code") or
                existing.get("fail_cnt") != beacon.get("fail_cnt") or
                existing.get("pc") != beacon.get("pc")):
                is_changed = True
            existing.update(beacon)
        else:
            is_changed = True
            self.beacons_cache.append(beacon)

        pc = beacon.get("pc", 0)
        decoded_pc = decode_pc_address(pc, self.repo_root, on_decoded_cb=self.sig_pc_decoded.emit)

        current_key = (hash_val, beacon.get("reason_code"), beacon.get("fail_cnt"), pc)
        if getattr(self, "_last_logged_beacon_key", None) != current_key:
            self._last_logged_beacon_key = current_key
            self._append_can_log("WARN", f"Sygnał ratunkowy 0xEE (Hash: 0x{hash_val:02X}, Powód: {beacon.get('reason_str')}, Awarii: {beacon.get('fail_cnt')}/3) @ {decoded_pc}")

        if is_changed:
            self._refresh_device_table()
            self._update_kpi_cards()

    def _on_pc_async_decoded(self, pc_addr: int, decoded_str: str):
        # Safely update table cell when background PC decoder finishes
        hex_target = f"0x{pc_addr:08X}"
        for row in range(self.table_devices.rowCount()):
            item_diag = self.table_devices.item(row, 5)
            if item_diag and hex_target in item_diag.text():
                new_text = item_diag.text().replace(hex_target, decoded_str)
                item_diag.setText(new_text)

    def _update_kpi_cards(self):
        # Exclude beacons that belong to nodes confirmed in APPLICATION mode
        app_hashes = {d.get("uid_hash") for d in self.devices_cache if d.get("mode") == "APPLICATION"}
        app_hashes.update({d.get("uid_crc_hash") for d in self.devices_cache if d.get("mode") == "APPLICATION"})

        valid_beacons = [b for b in self.beacons_cache if b.get("uid_hash") not in app_hashes]
        if len(self.devices_cache) == 1 and self.devices_cache[0].get("mode") == "APPLICATION":
            valid_beacons = []

        total_nodes = max(len(self.devices_cache), len(valid_beacons))
        alarm_nodes = len(valid_beacons)

        alarm_hashes = {b.get("uid_hash", 0) for b in valid_beacons}
        ready_app = sum(1 for d in self.devices_cache if d.get("mode") == "APPLICATION")
        ready_boot = sum(1 for d in self.devices_cache if d.get("mode") == "BOOTLOADER" and d.get("uid_hash") not in alarm_hashes and d.get("uid_crc_hash") not in alarm_hashes)
        ready_count = ready_app + ready_boot

        self.card_all.set_value(total_nodes, str(total_nodes))
        self.card_ready.set_value(ready_count, f"{ready_app} App / {ready_boot} Boot" if ready_count > 0 else "0")
        self.card_alarm.set_value(alarm_nodes, f"{alarm_nodes} W TRYBIE SOS" if alarm_nodes > 0 else "Brak awarii (OK)")

    def _filter_devices(self, mode: str):
        self.current_filter_mode = mode
        self._refresh_device_table()
        if mode == "ALARM" and self.beacons_cache:
            # If user clicked the emergency tile, try auto-filling target UID if matched
            b = self.beacons_cache[0]
            hash_val = b.get("uid_hash", 0)
            matched = next((d for d in self.devices_cache if d.get("uid_hash") == hash_val or d.get("uid_crc_hash") == hash_val), None)
            if not matched and len(self.devices_cache) == 1:
                matched = self.devices_cache[0]
            if matched and matched.get("uid_str"):
                self.txt_target_uid.setText(matched["uid_str"])

    def _refresh_device_table(self):
        self.table_devices.setRowCount(0)
        display_list = []
        matched_beacon_indices = set()

        for d in self.devices_cache:
            h_xor = d.get("uid_hash", 0)
            h_crc = d.get("uid_crc_hash", 0)

            # Find matching beacon ONLY if device is NOT running APPLICATION
            matched_b = None
            if d.get("mode") != "APPLICATION":
                for idx, b in enumerate(self.beacons_cache):
                    if idx in matched_beacon_indices:
                        continue
                    bh = b.get("uid_hash", 0)
                    if bh == h_xor or bh == h_crc:
                        matched_b = b
                        matched_beacon_indices.add(idx)
                        break

                # 1-device fallback on CAN bus ONLY if not in APPLICATION mode
                if matched_b is None and len(self.devices_cache) == 1 and len(self.beacons_cache) == 1 and 0 not in matched_beacon_indices:
                    matched_b = self.beacons_cache[0]
                    matched_beacon_indices.add(0)

            display_list.append((d, matched_b))

        # Include orphan beacons (nodes where full 96-bit UID is not yet discovered)
        for idx, b in enumerate(self.beacons_cache):
            if idx not in matched_beacon_indices:
                display_list.append((None, b))

        # Apply KPI filter
        filtered = []
        for dev, beacon in display_list:
            is_alarm = (beacon is not None) or (dev and dev.get("mode") == "RECOVERY_MODE")
            is_ready = dev and (dev.get("mode") in ("APPLICATION", "BOOTLOADER")) and not beacon

            if self.current_filter_mode == "READY" and not is_ready:
                continue
            if self.current_filter_mode == "ALARM" and not is_alarm:
                continue
            filtered.append((dev, beacon))

        self.table_devices.setRowCount(len(filtered))

        for row, (dev, beacon) in enumerate(filtered):
            pc = beacon.get("pc", 0) if beacon else 0
            decoded_pc = decode_pc_address(pc, self.repo_root, on_decoded_cb=self.sig_pc_decoded.emit) if beacon else ""

            if dev and dev.get("mode") == "APPLICATION":
                status_item = QTableWidgetItem("🟢 Zdrowy (App)")
                status_item.setForeground(QBrush(QColor("#10B981")))
                mode_str = "APPLICATION"
                diag_str = "Praca stabilna (OK)"
            elif beacon or (dev and dev.get("mode") == "RECOVERY_MODE"):
                status_item = QTableWidgetItem("🔴 SOS (AWARIA APP)")
                status_item.setForeground(QBrush(QColor("#EF4444")))
                mode_str = "BOOTLOADER [RECOVERY]"
                reason_str = beacon.get('reason_str', 'RECOVERY') if beacon else 'RECOVERY'
                fail_cnt = beacon.get('fail_cnt', 0) if beacon else 0
                diag_str = f"Awaria App: {reason_str} ({fail_cnt}/3) @ {decoded_pc} | Bootloader stabilny (gotowy do wgrania)"
            elif dev and dev.get("mode") == "BOOTLOADER":
                status_item = QTableWidgetItem("🟡 Bootloader")
                status_item.setForeground(QBrush(QColor("#F59E0B")))
                mode_str = "BOOTLOADER"
                diag_str = "Tryb serwisowy / Programowanie Flash (OK)"
            else:
                status_item = QTableWidgetItem("⚪ Nieznany")
                mode_str = "NIEZNANY"
                diag_str = "—"

            if dev:
                uid_str = dev.get("uid_str") or (can_updater.format_uid(dev["uid"]) if dev.get("uid") else "[Oczekiwanie na pełny UID]")
            else:
                uid_str = "[Wykryto z Beaconu 0xEE]"
            hash_val = dev.get('uid_hash', 0) if dev else (beacon.get('uid_hash', 0) if beacon else 0)
            hash_str = f"0x{hash_val:02X}"
            ver_str = dev.get("version", "—") if dev else "—"

            self.table_devices.setItem(row, 0, status_item)
            self.table_devices.setItem(row, 1, QTableWidgetItem(uid_str))
            self.table_devices.setItem(row, 2, QTableWidgetItem(hash_str))
            self.table_devices.setItem(row, 3, QTableWidgetItem(mode_str))
            self.table_devices.setItem(row, 4, QTableWidgetItem(ver_str))
            self.table_devices.setItem(row, 5, QTableWidgetItem(diag_str))

        self._update_quick_action_buttons_state()

    def _on_table_row_clicked(self, item: QTableWidgetItem):
        row = item.row()
        uid_item = self.table_devices.item(row, 1)
        if uid_item:
            uid_text = uid_item.text()
            if not uid_text.startswith("["):
                self.txt_target_uid.setText(uid_text)
            else:
                hash_item = self.table_devices.item(row, 2)
                hash_str = hash_item.text() if hash_item else ""
                try:
                    hash_val = int(hash_str, 16)
                    matched = next((d for d in self.devices_cache if d.get("uid_hash") == hash_val or d.get("uid_crc_hash") == hash_val), None)
                    if not matched and len(self.devices_cache) == 1:
                        matched = self.devices_cache[0]
                    if matched and matched.get("uid_str"):
                        self.txt_target_uid.setText(matched["uid_str"])
                    else:
                        self._append_can_log("INFO", f"Wybrano węzeł awaryjny (Hash: {hash_str}). Zapytanie o pełny UID na magistrali...")
                        self._request_bus_scan()
                except Exception:
                    self._request_bus_scan()

        self._update_quick_action_buttons_state()

    def _on_table_selection_changed(self):
        row = self.table_devices.currentRow()
        if row < 0:
            return
        uid_item = self.table_devices.item(row, 1)
        if uid_item:
            uid_text = uid_item.text().strip()
            if not uid_text.startswith("["):
                self.txt_target_uid.setText(uid_text)
            else:
                hash_item = self.table_devices.item(row, 2)
                hash_str = hash_item.text().strip() if hash_item else ""
                try:
                    hash_val = int(hash_str, 16)
                    matched = next((d for d in self.devices_cache if d.get("uid_hash") == hash_val or d.get("uid_crc_hash") == hash_val), None)
                    if not matched and len(self.devices_cache) == 1:
                        matched = self.devices_cache[0]
                    if matched and matched.get("uid_str"):
                        self.txt_target_uid.setText(matched["uid_str"])
                except Exception:
                    pass
        self._update_quick_action_buttons_state()

    def _is_target_in_recovery(self, uid_bytes: Optional[bytes]) -> bool:
        if not uid_bytes:
            return False
        # If target device is discovered and running APPLICATION, it is NOT in recovery
        for d in self.devices_cache:
            if d.get("uid") == uid_bytes and d.get("mode") == "APPLICATION":
                return False
        if len(self.devices_cache) == 1 and self.devices_cache[0].get("mode") == "APPLICATION":
            return False

        h_xor = can_updater.compute_mcu_uid_hash(uid_bytes)
        h_crc = zlib.crc32(uid_bytes) & 0xFF
        for b in self.beacons_cache:
            bh = b.get("uid_hash", 0)
            if bh == h_xor or bh == h_crc:
                return True
        if len(self.devices_cache) == 1 and len(self.beacons_cache) >= 1:
            return True
        return False

    def _update_quick_action_buttons_state(self):
        if not self.selected_target_uid:
            self.btn_quick_boot.setEnabled(False)
            self.btn_quick_app.setEnabled(False)
            self.btn_quick_boot.setToolTip("Wskaż urządzenie z tabeli powyżej, aby wymusić przejście w tryb Bootloadera.")
            self.btn_quick_app.setToolTip("Wskaż urządzenie z tabeli powyżej, aby zrestartować MCU i uruchomić Aplikację.")
            return

        self.btn_quick_boot.setEnabled(True)
        self.btn_quick_boot.setToolTip("Wysyła polecenie CMD_ENTER_BOOTLOADER (0xA2), aby przejść do Bootloadera.")

        if self._is_target_in_recovery(self.selected_target_uid):
            self.btn_quick_app.setEnabled(False)
            self.btn_quick_app.setToolTip("Zablokowane: Urządzenie zgłasza awarię Aplikacji (Recovery / SOS). Wgraj poprawny wsad Aplikacji przed jej uruchomieniem.")
        else:
            self.btn_quick_app.setEnabled(True)
            self.btn_quick_app.setToolTip("Wysyła polecenie CMD_GO_EXECUTE (0x21), aby zrestartować mikrokontroler i uruchomić Aplikację.")

    def _on_rescue_device(self, beacon: Dict):
        hash_val = beacon.get("uid_hash", 0)
        # Find matching device UID if known
        matched = next((d for d in self.devices_cache if d.get("uid_hash") == hash_val), None)
        if matched:
            self.txt_target_uid.setText(matched["uid_str"])
        else:
            self._append_can_log("INFO", f"Wybrano układ ratunkowy (Hash: 0x{hash_val:02X}). Wysyłanie zapytania o pełny UID...")
            self._request_bus_scan()

        # Switch to MainApp update
        self.rad_app.setChecked(True)
        self._on_target_type_changed()
        self._append_can_log("INFO", "Gotowy do procedury ratunkowej (Rescue Flash).")

    # --------------------------------------------------------------------------
    # Firmware Selection & Repository Auto-Path
    # --------------------------------------------------------------------------
    def _on_target_type_changed(self):
        is_boot = self.rad_boot.isChecked()
        self.stepper_widget.build_ui(is_bootloader=is_boot)
        if self.repo_root and hasattr(self, 'chk_use_repo_artifact') and self.chk_use_repo_artifact.isChecked():
            self._update_default_repo_path()
        else:
            self._reinspect_current_binary()

    def _on_use_repo_artifact_toggled(self, checked: bool):
        if checked:
            self._update_default_repo_path()

    def _update_default_repo_path(self):
        if not self.repo_root:
            return
        if self.rad_app.isChecked():
            p = os.path.join(self.repo_root, "build", "debug_GCC_NUCLEO-C542RC", "v_box.bin")
        elif self.rad_boot.isChecked():
            p = os.path.join(self.repo_root, "build", "debug_GCC_NUCLEO-C542RC", "v_box_boot.bin")
        else:
            p = ""
        self.txt_file_path.setText(p)

    def _browse_firmware_file(self):
        init_dir = self.repo_root or os.getcwd()
        path, _ = QFileDialog.getOpenFileName(self, "Wybierz plik oprogramowania STM32 (.bin)", init_dir, "Pliki binarne (*.bin);;Wszystkie pliki (*.*)")
        if path:
            if hasattr(self, 'chk_use_repo_artifact'):
                self.chk_use_repo_artifact.setChecked(False)
            self.txt_file_path.setText(path)

    def _on_file_path_changed(self, text: str):
        self.current_firmware_path = text.strip()
        self._reinspect_current_binary()

    def _reinspect_current_binary(self):
        path = self.current_firmware_path
        target_mode = "BOOTLOADER" if self.rad_boot.isChecked() else ("DUMMY" if self.rad_dummy.isChecked() else "APP")

        if target_mode == "DUMMY":
            self.lbl_pf_status.setText("✔ Tryb Symulacji: Test pamięci RAM (Dummy Update)")
            self.lbl_pf_status.setStyleSheet("color: #38BDF8; font-weight: bold;")
            self.lbl_pf_details.setText("Rozmiar: 32 KB | Tryb: Symulacja bez naruszania pamięci Flash")
            self.btn_start_flash.setEnabled(True)
            self.lbl_stale_warning.setText("")
            return

        if not path or not os.path.isfile(path):
            self.lbl_pf_status.setText("⚠️ Wybierz istniejący plik .bin")
            self.lbl_pf_status.setStyleSheet("color: #94A3B8; font-weight: bold;")
            self.lbl_pf_details.setText("Rozmiar: — | Wektory: — | CRC32: —")
            self.btn_start_flash.setEnabled(False)
            self.lbl_stale_warning.setText("")
            self.loaded_firmware_data = None
            return

        # Check if build is stale
        if self.repo_root:
            is_stale, newest_f, diff_s = is_build_stale(path, self.repo_root)
            if is_stale:
                self.lbl_stale_warning.setText(f"⚠️ Kod w {newest_f} nowszy niż .bin (+{diff_s:.0f}s)!")
            else:
                self.lbl_stale_warning.setText("🟢 Build aktualny")
        else:
            self.lbl_stale_warning.setText("")

        # Read binary data
        try:
            with open(path, "rb") as f:
                data = f.read()
        except Exception as e:
            self.lbl_pf_status.setText(f"Błąd odczytu pliku: {e}")
            self.lbl_pf_status.setStyleSheet("color: #EF4444; font-weight: bold;")
            self.btn_start_flash.setEnabled(False)
            return

        valid, msg, res = inspect_firmware_binary(data, target_type=target_mode)
        if valid:
            self.loaded_firmware_data = data
            pad_info = f" (+{res['pad_len']}B pad)" if res['pad_len'] > 0 else ""
            self.lbl_pf_status.setText(f"✔ Poprawny obraz ({target_mode})! Gotowy do wgrania.")
            self.lbl_pf_status.setStyleSheet("color: #10B981; font-weight: bold;")
            self.lbl_pf_details.setText(f"Rozmiar: {res['size']} B{pad_info} | MSP: 0x{res['msp']:08X} | Reset: 0x{res['reset']:08X} | CRC32: 0x{res['crc32']:08X}")
            self.btn_start_flash.setEnabled(True)
        else:
            self.loaded_firmware_data = None
            self.lbl_pf_status.setText(f"❌ {msg}")
            self.lbl_pf_status.setStyleSheet("color: #EF4444; font-weight: bold;")
            self.lbl_pf_details.setText(f"Rozmiar: {res['size']} B | MSP: 0x{res['msp']:08X} | Reset: 0x{res['reset']:08X}")
            self.btn_start_flash.setEnabled(False)


    def _on_target_uid_changed(self, text: str):
        clean = text.replace("-", "").replace(" ", "").strip()
        if len(clean) == 24:
            try:
                self.selected_target_uid = bytes.fromhex(clean)
                self.txt_target_uid.setStyleSheet("border: 1px solid #10B981;")
            except ValueError:
                self.selected_target_uid = None
                self.txt_target_uid.setStyleSheet("border: 1px solid #EF4444;")
        else:
            self.selected_target_uid = None
            self.txt_target_uid.setStyleSheet("")

        self._update_quick_action_buttons_state()

    def _update_build_system_state(self):
        """Validates build capabilities (Docker / Ninja) and sets button state and tooltip."""
        if not hasattr(self, 'btn_build_project') or not self.repo_root:
            return

        can_build, engine_type, desc = check_build_environment(self.repo_root)
        if can_build:
            self.btn_build_project.setEnabled(True)
            if engine_type == "DOCKER":
                self.btn_build_project.setText("🔨 Zbuduj Projekt (Docker)")
                self.btn_build_project.setToolTip(f"Kompiluj w kontenerze Docker (vbox-builder):\n{desc}")
            else:
                self.btn_build_project.setText("🔨 Zbuduj Projekt (Ninja)")
                self.btn_build_project.setToolTip(f"Kompiluj lokalnym kompilatorem Ninja (arm-none-eabi-gcc):\n{desc}")
        else:
            self.btn_build_project.setEnabled(False)
            self.btn_build_project.setText("🔨 Zbuduj Projekt (Niedostępny)")
            self.btn_build_project.setToolTip(
                f"Kompilacja niedostępna: {desc}\n\n"
                "Kliknij przycisk [ℹ️] obok, aby wyświetlić instrukcję jak odblokować kompilację.\n"
                "Pamiętaj: jeśli posiadasz gotowy plik .bin, możesz go wgrać przyciskiem '📂 Przeglądaj...' bez kompilowania!"
            )

    def _show_build_instructions(self):
        can_build, engine_type, desc = check_build_environment(self.repo_root)
        status_line = f"Aktualny stan systemu: {desc}"

        msg = (
            f"<b>{status_line}</b><br><br>"
            "Aby kompilować projekt mikrokontrolera STM32C542 bezpośrednio z aplikacji, "
            "wymagane jest jedno z poniższych środowisk:<br><br>"
            "<b>1. Środowisko Docker (Zalecane na Windows):</b><br>"
            "&nbsp;&nbsp;• Zainstaluj i uruchom aplikację <b>Docker Desktop</b>.<br>"
            "&nbsp;&nbsp;• Upewnij się, że silnik Docker działa (zielona ikona w zasobniku).<br>"
            "&nbsp;&nbsp;• Zbuduj obraz kontenera, uruchamiając w terminalu skrypt:<br>"
            "&nbsp;&nbsp;&nbsp;&nbsp;<code>.devcontainer\\docker_build.bat</code><br>"
            "&nbsp;&nbsp;&nbsp;&nbsp;lub polecenie: <code>docker build -t vbox-builder .devcontainer</code><br><br>"
            "<b>2. Środowisko Natywne (Bez Dockera):</b><br>"
            "&nbsp;&nbsp;• Zainstaluj <b>Arm GNU Toolchain</b> (<code>arm-none-eabi-gcc</code>) oraz <b>Ninja</b>.<br>"
            "&nbsp;&nbsp;• Dodaj ścieżki do plików wykonywalnych do zmiennej środowiskowej <code>PATH</code>.<br><br>"
            "<b>Wskazówka:</b> Kompilacja nie jest wymagana do wgrania oprogramowania po CAN! "
            "Jeśli posiadasz gotowy plik <code>.bin</code> z zewnętrznego IDE (np. STM32CubeIDE), "
            "po prostu wskaż go przyciskiem <b>'📂 Przeglądaj...'</b>. Wgrywanie działa w 100% natywnie bez Dockera."
        )

        box = QMessageBox(self)
        box.setWindowTitle("Instrukcja konfiguracji środowiska kompilacji")
        box.setTextFormat(Qt.TextFormat.RichText if QT_VERSION == 6 else Qt.RichText)
        box.setText(msg)
        box.setIcon(QMessageBox.Icon.Information if QT_VERSION == 6 else QMessageBox.Information)
        box.exec()

    def _build_project_now(self):
        if not self.repo_root:
            return
        if self.build_worker and self.build_worker.isRunning():
            QMessageBox.information(self, "Budowanie w toku", "Kompilacja jest już uruchomiona w tle!")
            return

        can_build, engine_type, desc = check_build_environment(self.repo_root)
        if not can_build:
            self._show_build_instructions()
            return

        self.tabs_terminal.setCurrentIndex(0)
        self.btn_build_project.setEnabled(False)
        self.build_worker = BuildProjectWorker(self.repo_root)
        self.build_worker.sig_build_log.connect(lambda l: self._append_can_log("INFO", l))
        self.build_worker.sig_build_finished.connect(self._on_build_finished)
        self.build_worker.start()

    def _on_build_finished(self, success: bool, msg: str):
        self._update_build_system_state()
        if success:
            self._append_can_log("OK", f"[BUILD] {msg}")
            self._reinspect_current_binary()
        else:
            self._append_can_log("ERROR", f"[BUILD FAILED] {msg}")

    # --------------------------------------------------------------------------
    # Flashing Session Management
    # --------------------------------------------------------------------------
    def _start_flashing_session(self):
        if not self.monitor_worker or not self.monitor_worker.bus:
            QMessageBox.warning(self, "Brak połączenia", "Najpierw połącz się z magistralą CANable 2.0!")
            return

        # Check target UID
        if not self.selected_target_uid:
            if self.txt_target_uid.text().strip():
                clean = self.txt_target_uid.text().replace("-", "").replace(" ", "").strip()
                if len(clean) == 24:
                    try:
                        self.selected_target_uid = bytes.fromhex(clean)
                    except ValueError:
                        pass
            if not self.selected_target_uid and len(self.devices_cache) == 1:
                self.selected_target_uid = self.devices_cache[0]["uid"]
                self.txt_target_uid.setText(self.devices_cache[0]["uid_str"])
            elif not self.selected_target_uid and self.table_devices.rowCount() == 1:
                uid_item = self.table_devices.item(0, 1)
                if uid_item and not uid_item.text().startswith("["):
                    clean = uid_item.text().replace("-", "").replace(" ", "").strip()
                    if len(clean) == 24:
                        self.selected_target_uid = bytes.fromhex(clean)
                        self.txt_target_uid.setText(uid_item.text().strip())
            elif not self.selected_target_uid and len(self.devices_cache) == 0:
                QMessageBox.warning(self, "Brak celu", "Nie wykryto żadnego urządzenia na magistrali. Wskaż UID ręcznie.")
                return
            elif not self.selected_target_uid:
                QMessageBox.warning(self, "Wskaż urządzenie", "Wykryto wiele urządzeń na magistrali. Wybierz docelowy węzeł z tabeli.")
                return

        target_mode = "BOOTLOADER" if self.rad_boot.isChecked() else ("DUMMY" if self.rad_dummy.isChecked() else "APP")

        # Check stale build
        if target_mode != "DUMMY" and self.repo_root and self.current_firmware_path:
            is_stale, newest_f, diff_s = is_build_stale(self.current_firmware_path, self.repo_root)
            if is_stale:
                reply = QMessageBox.question(
                    self, "Nieaktualny obraz .bin",
                    f"Wykryto niezbudowane zmiany w kodzie źródłowym!\n"
                    f"Plik '{newest_f}' był modyfikowany {diff_s:.0f} sekund po ostatniej kompilacji.\n\n"
                    f"Czy chcesz zbudować projekt przed wgraniem?",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No | QMessageBox.StandardButton.Cancel
                )
                if reply == QMessageBox.StandardButton.Cancel:
                    return
                elif reply == QMessageBox.StandardButton.Yes:
                    self._build_project_now()
                    return

        # Prepare firmware payload
        if target_mode == "DUMMY":
            payload = bytes([(i & 0xFF) for i in range(32768)])
        else:
            if not self.loaded_firmware_data:
                QMessageBox.warning(self, "Brak wsadu", "Plik wsadowy nie został załadowany lub jest niepoprawny!")
                return
            payload = self.loaded_firmware_data
            # Apply 16-byte padding
            if (len(payload) % 16) != 0:
                pad_len = 16 - (len(payload) % 16)
                payload = payload + (b'\xFF' * pad_len)

        # Pause monitor worker
        self.monitor_worker.pause_for_flashing()

        # Switch UI state
        self.btn_start_flash.setEnabled(False)
        self.btn_cancel_flash.setEnabled(True)
        if hasattr(self, "btn_scan_table"):
            self.btn_scan_table.setEnabled(False)
        self.stepper_widget.reset()
        tot_steps = 6 if target_mode == "BOOTLOADER" else 5
        self.stepper_widget.set_step(1, tot_steps)
        self.progress_bar.setValue(0)
        self.progress_bar.setFormat(f"[1/{tot_steps}] Wybór urządzenia docelowego... (0%)")

        # Start flash worker
        self.flash_worker = OtaFlashWorker(
            bus=self.monitor_worker.bus,
            target_uid=self.selected_target_uid,
            firmware_data=payload,
            update_type=target_mode,
            app_address=0x08008000 if target_mode == "APP" else 0x08038000,
            block_size=256,
            verbose=self.chk_verbose_can.isChecked()
        )
        self.flash_worker.sig_step_changed.connect(self._on_flash_step_changed)
        self.flash_worker.sig_progress.connect(self._on_flash_progress)
        self.flash_worker.sig_log.connect(self._append_can_log)
        self.flash_worker.sig_finished.connect(self._on_flash_finished)
        self.flash_worker.start()

    def _cancel_flashing_session(self):
        if self.flash_worker and self.flash_worker.isRunning():
            self._append_can_log("WARN", "Żądanie przerwania aktualizacji przez operatora...")
            self.flash_worker.request_cancel()
            self.btn_cancel_flash.setEnabled(False)

    def _on_flash_step_changed(self, step_idx: int, total_steps: int, title: str):
        self.stepper_widget.set_step(step_idx, total_steps)
        self.progress_bar.setFormat(f"[{step_idx}/{total_steps}] {title} (%p%)")

    def _on_flash_progress(self, current: int, total: int, speed: float, eta: float, retries: int):
        pct = int((current / total) * 100) if total > 0 else 0
        self.progress_bar.setValue(pct)
        self.lbl_tel_speed.setText(f"Prędkość: {speed:.2f} KB/s")
        self.lbl_tel_eta.setText(f"ETA: {eta:.1f} s")
        cur_blk = current // 256
        tot_blk = (total + 255) // 256
        self.lbl_tel_block.setText(f"Bloki: {cur_blk} / {tot_blk}")
        self.lbl_tel_retries.setText(f"Ponowienia: {retries}")

    def _on_flash_finished(self, success: bool, msg: str):
        self.btn_start_flash.setEnabled(True)
        self.btn_cancel_flash.setEnabled(False)
        if hasattr(self, "btn_scan_table"):
            self.btn_scan_table.setEnabled(True)

        if success:
            self.progress_bar.setValue(100)
            self.progress_bar.setFormat("100% - AKTUALIZACJA ZAKOŃCZONA SUKCESEM!")
            self.stepper_widget.set_step(self.stepper_widget.total_steps + 1, self.stepper_widget.total_steps)

            # Clear recovery / SOS beacon caches since firmware was successfully written and launched
            self.beacons_cache.clear()
            if self.monitor_worker:
                self.monitor_worker.clear_all_beacons()
                if self.monitor_worker.bus:
                    try:
                        while self.monitor_worker.bus.recv(timeout=0.02) is not None:
                            pass
                    except Exception:
                        pass
            self._refresh_device_table()
            self._update_kpi_cards()

            QMessageBox.information(self, "Sukces", f"Aktualizacja oprogramowania układowego powiodła się!\n\n{msg}")
        else:
            self.progress_bar.setFormat("BŁĄD AKTUALIZACJI!")
            QMessageBox.critical(self, "Niepowodzenie aktualizacji", f"Aktualizacja nie powiodła się:\n\n{msg}")

        # Resume background bus monitor
        if self.monitor_worker:
            self.monitor_worker.resume_monitoring()
            self.monitor_worker.request_scan()

    # --------------------------------------------------------------------------
    # Quick Diagnostic Actions
    # --------------------------------------------------------------------------
    def _quick_enter_bootloader(self):
        if not self.monitor_worker or not self.monitor_worker.bus:
            QMessageBox.warning(self, "Brak magistrali", "Połącz się najpierw z magistralą CAN.")
            return
        if not self.selected_target_uid:
            QMessageBox.warning(self, "Wybierz cel", "Wskaż urządzenie z tabeli powyżej.")
            return

        self._append_can_log("INFO", f"Wysyłanie CMD_ENTER_BOOTLOADER (0xA2) do {can_updater.format_uid(self.selected_target_uid)}...")
        self.monitor_worker.pause_for_flashing()
        try:
            client = can_updater.IsoTpClient(self.monitor_worker.bus)
            # Select target
            sel_pdu = bytes([can_updater.CMD_SELECT_TARGET]) + self.selected_target_uid
            client.send_pdu(sel_pdu, timeout=1.0)
            client.recv_pdu(timeout=1.0)
            # Enter bootloader
            boot_pdu = bytes([can_updater.CMD_ENTER_BOOTLOADER])
            client.send_pdu(boot_pdu, timeout=1.0)
            resp = client.recv_pdu(timeout=1.0)
            if resp and resp[0] == can_updater.OTA_RESP_ACK:
                self._append_can_log("OK", "Urządzenie potwierdziło przejście do Bootloadera!")
            else:
                self._append_can_log("WARN", f"Odpowiedź: {can_updater.decode_nack(resp)}")
        except Exception as e:
            self._append_can_log("ERROR", f"Błąd wykonania: {e}")
        finally:
            self.monitor_worker.resume_monitoring()
            self.monitor_worker.request_scan()

    def _quick_jump_app(self):
        if not self.monitor_worker or not self.monitor_worker.bus:
            QMessageBox.warning(self, "Brak magistrali", "Połącz się najpierw z magistralą CAN.")
            return
        if not self.selected_target_uid:
            QMessageBox.warning(self, "Wybierz cel", "Wskaż urządzenie z tabeli powyżej.")
            return

        if self._is_target_in_recovery(self.selected_target_uid):
            QMessageBox.warning(
                self,
                "Aplikacja w stanie awarii (Recovery Mode)",
                "To urządzenie zgłasza błąd i pracuje w trybie ratunkowym (Recovery Mode).\n\n"
                "Uruchomienie uszkodzonej aplikacji spowoduje natychmiastowy ponowny błąd mikrokontrolera.\n"
                "Najpierw wgraj poprawne oprogramowanie (Aplikacja Główna)."
            )
            return

        self._append_can_log("INFO", f"Wysyłanie CMD_GO_EXECUTE (0x21) do {can_updater.format_uid(self.selected_target_uid)}...")
        self.monitor_worker.pause_for_flashing()
        try:
            client = can_updater.IsoTpClient(self.monitor_worker.bus)
            sel_pdu = bytes([can_updater.CMD_SELECT_TARGET]) + self.selected_target_uid
            client.send_pdu(sel_pdu, timeout=1.0)
            client.recv_pdu(timeout=1.0)
            go_pdu = bytes([can_updater.CMD_GO_EXECUTE])
            client.send_pdu(go_pdu, timeout=1.0)
            resp = client.recv_pdu(timeout=1.0)
            self._append_can_log("OK", "Wysłano polecenie restartu i skoku do Aplikacji.")
        except Exception as e:
            self._append_can_log("ERROR", f"Błąd wykonania: {e}")
        finally:
            self.monitor_worker.resume_monitoring()
            self.monitor_worker.request_scan()

    def _quick_decode_selected_pc(self):
        row = self.table_devices.currentRow()
        if row == -1:
            QMessageBox.information(self, "Wybierz wiersz", "Wskaż w tabeli urządzenie zgłaszające błąd.")
            return

        diag_item = self.table_devices.item(row, 5)
        diag_text = diag_item.text() if diag_item else ""
        QMessageBox.information(self, "Szczegóły diagnostyczne awarii", f"Informacje diagnostyczne węzła:\n\n{diag_text}")

    # --------------------------------------------------------------------------
    # UART2 Serial Monitor Handling
    # --------------------------------------------------------------------------
    def _toggle_uart_connection(self):
        if self.uart_worker and self.uart_worker.isRunning():
            self.uart_worker.stop()
            self.uart_worker = None
            self.btn_uart_connect.setText("🔌 Połącz UART2 (115200)")
            self.btn_uart_connect.setObjectName("")
            self.lbl_uart_status.setText("(Rozłączono)")
            self.lbl_uart_status.setStyleSheet("color: #64748B; font-weight: bold;")
        else:
            port = self.combo_uart_port.currentData() or self.combo_uart_port.currentText().split()[0]
            if not port:
                QMessageBox.warning(self, "Brak portu", "Wybierz port ST-Link VCP.")
                return
            self.uart_worker = SerialUartWorker(port=port, baudrate=115200)
            self.uart_worker.sig_uart_line.connect(self._append_uart_log)
            self.uart_worker.sig_uart_status.connect(self._on_uart_status_changed)
            self.uart_worker.start()
            self.btn_uart_connect.setText("🔌 Rozłącz UART2")
            self.btn_uart_connect.setObjectName("btn_danger")

    def _on_uart_status_changed(self, connected: bool, msg: str):
        if connected:
            self.lbl_uart_status.setText(f"(● {msg})")
            self.lbl_uart_status.setStyleSheet("color: #10B981; font-weight: bold;")
        else:
            self.lbl_uart_status.setText("(● Rozłączono)")
            self.lbl_uart_status.setStyleSheet("color: #64748B; font-weight: bold;")

    def _append_uart_log(self, line: str):
        ts = time.strftime("%H:%M:%S")
        formatted = f"[{ts}] {line}"
        self.txt_log_uart.appendPlainText(formatted)
        if self.chk_auto_scroll_uart.isChecked():
            self.txt_log_uart.moveCursor(QTextCursor.MoveOperation.End if QT_VERSION == 6 else QTextCursor.End)

    def _append_can_log(self, level: str, msg: str):
        ts = time.strftime("%H:%M:%S")
        prefix = f"[{ts}] [{level:<5}]"
        line = f"{prefix} {msg}"
        self.txt_log_can.appendPlainText(line)
        if self.chk_auto_scroll_can.isChecked():
            self.txt_log_can.moveCursor(QTextCursor.MoveOperation.End if QT_VERSION == 6 else QTextCursor.End)

    def _save_log_to_file(self, content: str):
        path, _ = QFileDialog.getSaveFileName(self, "Zapisz log do pliku", os.getcwd(), "Pliki tekstowe (*.log *.txt)")
        if path:
            try:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(content)
                QMessageBox.information(self, "Zapisano", f"Pomyślnie zapisano log do {path}")
            except Exception as e:
                QMessageBox.critical(self, "Błąd zapisu", str(e))

    # --------------------------------------------------------------------------
    # Drag & Drop Support
    # --------------------------------------------------------------------------
    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            for url in event.mimeData().urls():
                if url.toLocalFile().lower().endswith(".bin"):
                    event.acceptProposedAction()
                    return
        super().dragEnterEvent(event)

    def dropEvent(self, event):
        for url in event.mimeData().urls():
            fpath = url.toLocalFile()
            if fpath.lower().endswith(".bin"):
                if hasattr(self, 'chk_use_repo_artifact'):
                    self.chk_use_repo_artifact.setChecked(False)
                self.txt_file_path.setText(fpath)
                event.acceptProposedAction()
                return
        super().dropEvent(event)

    def closeEvent(self, event):
        if self.flash_worker and self.flash_worker.isRunning():
            reply = QMessageBox.question(
                self, "Aktualizacja w toku",
                "Trwa programowanie pamięci Flash! Zamknięcie okna może uszkodzić oprogramowanie mikrokontrolera.\n\nCzy na pewno chcesz przerwać?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )
            if reply != QMessageBox.StandardButton.Yes:
                event.ignore()
                return
            self.flash_worker.request_cancel()
            self.flash_worker.wait(1000)

        if self.monitor_worker:
            self.monitor_worker.stop()
        if self.uart_worker:
            self.uart_worker.stop()
        if self.build_worker and self.build_worker.isRunning():
            self.build_worker.terminate()
        event.accept()


# ==============================================================================
# Application Entry Point
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(description="V-BOX CAN OTA Updater & Diagnostic Studio GUI")
    parser.add_argument("--port", type=str, default=None, help="Initial COM port for CANable SLCAN (e.g. COM17)")
    parser.add_argument("--bitrate", type=int, default=500000, help="CAN bus bitrate (default: 500000)")
    parser.add_argument("--file", type=str, default=None, help="Initial firmware .bin file to inspect/flash")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setApplicationName("V-Box CAN OTA Updater")

    window = CanUpdaterMainWindow(init_port=args.port, init_bitrate=args.bitrate)
    if args.file and os.path.isfile(args.file):
        window.txt_file_path.setText(os.path.abspath(args.file))

    window.show()
    sys.exit(app.exec() if QT_VERSION == 6 else app.exec_())


if __name__ == "__main__":
    main()
