#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Merges STM32 CAN Bootloader (0x08000000, 32 KB) and Main Application (0x08008000)
into a single composite firmware image (v_box_full.bin and v_box_full.hex).
"""

import sys
import os
import argparse

BOOT_BASE_ADDR   = 0x08000000
APP_BASE_ADDR    = 0x08008000
BOOT_MAX_SIZE    = APP_BASE_ADDR - BOOT_BASE_ADDR  # Exactly 32,768 bytes (32 KB)
FLASH_MAX_SIZE   = 256 * 1024                     # 256 KB total STM32C542 Flash


def write_intel_hex(output_path, base_address, data):
    """Generates an Intel HEX (.hex) file from raw binary data."""
    with open(output_path, 'w', encoding='ascii') as f:
        curr_upper = None
        offset = 0
        total_len = len(data)

        while offset < total_len:
            abs_addr = base_address + offset
            upper = (abs_addr >> 16) & 0xFFFF
            lower = abs_addr & 0xFFFF

            # Write Extended Linear Address Record (Type 04) if upper 16 bits changed
            if upper != curr_upper:
                curr_upper = upper
                record_data = [(upper >> 8) & 0xFF, upper & 0xFF]
                cksum = (-(0x02 + 0x00 + 0x00 + 0x04 + sum(record_data))) & 0xFF
                f.write(f":02000004{upper:04X}{cksum:02X}\n")

            # Write Data Record (Type 00, up to 16 bytes per line)
            chunk = data[offset : offset + 16]
            chunk_len = len(chunk)
            cksum = (-(chunk_len + ((lower >> 8) & 0xFF) + (lower & 0xFF) + 0x00 + sum(chunk))) & 0xFF
            hex_data = "".join(f"{b:02X}" for b in chunk)
            f.write(f":{chunk_len:02X}{lower:04X}00{hex_data}{cksum:02X}\n")

            offset += chunk_len

        # End of File Record (Type 01)
        f.write(":00000001FF\n")


def main():
    parser = argparse.ArgumentParser(description="Merge Bootloader and Application into single flashable image.")
    parser.add_argument("--boot", required=True, help="Path to v_box_boot.bin")
    parser.add_argument("--app", required=True, help="Path to v_box.bin")
    parser.add_argument("--out-bin", default=None, help="Output path for merged .bin (default: same dir as app)")
    parser.add_argument("--out-hex", default=None, help="Output path for merged .hex (default: same dir as app)")

    args = parser.parse_args()

    if not os.path.exists(args.boot):
        print(f"[ERROR] Bootloader binary not found: {args.boot}", file=sys.stderr)
        sys.exit(1)

    if not os.path.exists(args.app):
        print(f"[ERROR] Application binary not found: {args.app}", file=sys.stderr)
        sys.exit(1)

    with open(args.boot, "rb") as f:
        boot_data = f.read()

    with open(args.app, "rb") as f:
        app_data = f.read()

    boot_len = len(boot_data)
    app_len = len(app_data)

    print("==============================================================================")
    print("                    Firmware Binary Merger (Dual-Target)                      ")
    print("==============================================================================")
    print(f"  Bootloader Image : {args.boot} ({boot_len:,} bytes / {boot_len/1024.0:.2f} KB)")
    print(f"  Application Image: {args.app} ({app_len:,} bytes / {app_len/1024.0:.2f} KB)")

    if boot_len > BOOT_MAX_SIZE:
        print(f"[ERROR] Bootloader size ({boot_len} B) exceeds 32 KB limit ({BOOT_MAX_SIZE} B)!", file=sys.stderr)
        sys.exit(1)

    if (BOOT_MAX_SIZE + app_len) > FLASH_MAX_SIZE:
        print(f"[ERROR] Total merged image size exceeds Flash limit of {FLASH_MAX_SIZE} B!", file=sys.stderr)
        sys.exit(1)

    # Pad bootloader with 0xFF up to exactly 32 KB (0x8000)
    padding = bytes([0xFF] * (BOOT_MAX_SIZE - boot_len))
    merged_data = boot_data + padding + app_data

    out_dir = os.path.dirname(os.path.abspath(args.app))
    out_bin = args.out_bin or os.path.join(out_dir, "v_box_full.bin")
    out_hex = args.out_hex or os.path.join(out_dir, "v_box_full.hex")

    with open(out_bin, "wb") as f:
        f.write(merged_data)

    write_intel_hex(out_hex, BOOT_BASE_ADDR, merged_data)

    merged_len = len(merged_data)
    print("------------------------------------------------------------------------------")
    print(f"  Merged Binary    : {out_bin} ({merged_len:,} bytes / {merged_len/1024.0:.2f} KB)")
    print(f"  Merged Intel HEX : {out_hex}")
    print(f"  Flash Range      : 0x{BOOT_BASE_ADDR:08X} - 0x{BOOT_BASE_ADDR + merged_len - 1:08X}")
    print("  Status           : SUCCESS (Ready for full flashing via ST-Link)")
    print("==============================================================================")


if __name__ == "__main__":
    main()
