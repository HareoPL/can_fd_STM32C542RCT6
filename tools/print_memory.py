#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Memory footprint analysis script for STM32 firmware builds.
Parses the linker map file (or ELF file via size tool) and prints a detailed,
human-readable summary table of FLASH and RAM usage, including FreeRTOS heap,
system stack, and custom sections. Supports single-target (detailed or compact)
and dual-target (Bootloader + MainApp) dashboard views.
"""

import sys
import os
import re
import argparse

# Force UTF-8 stdout if supported to avoid CP1250 encoding crashes on Windows
if hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass


def format_bytes(byte_count):
    """Formats bytes with thousands separator."""
    return f"{byte_count:7,d} B".replace(",", " ")


def format_kb(byte_count):
    """Formats bytes as Kilobytes."""
    kb = byte_count / 1024.0
    return f"{kb:6.1f} KB"


def make_bar(pct, width=20):
    """Creates a visual ASCII progress bar."""
    clamped_pct = max(0.0, min(100.0, pct))
    filled = int(round(width * clamped_pct / 100.0))
    return "=" * filled + "-" * (width - filled)


def parse_map_file(map_path):
    """
    Parses GNU ld map file to extract memory regions, section placements,
    FreeRTOS heap allocation, stack, and heap sizes.
    """
    if not os.path.exists(map_path):
        return None

    try:
        with open(map_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except Exception:
        return None

    # 1. Parse Memory Configuration
    regions = {}
    mem_cfg_pattern = r'Memory Configuration\s+Name\s+Origin\s+Length\s+Attributes\s+(.*?)\n\n'
    mem_cfg_match = re.search(mem_cfg_pattern, content, re.DOTALL)
    if mem_cfg_match:
        for line in mem_cfg_match.group(1).strip().splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[0] != '*default*':
                name = parts[0]
                origin = int(parts[1], 16)
                length = int(parts[2], 16)
                regions[name] = {
                    'name': name,
                    'origin': origin,
                    'length': length,
                    'used': 0,
                    'sections': []
                }

    # Fallback to default STM32C542RC sizes if not found
    if not regions:
        regions['ROM'] = {'name': 'ROM', 'origin': 0x08000000, 'length': 256 * 1024, 'used': 0, 'sections': []}
        regions['RAM'] = {'name': 'RAM', 'origin': 0x20000000, 'length': 64 * 1024, 'used': 0, 'sections': []}

    # 2. Parse top-level output sections in "Linker script and memory map"
    sections = []
    sec_pattern = re.compile(
        r'^(\.[a-zA-Z0-9_\.\-]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)(?:\s+load address\s+(0x[0-9a-fA-F]+))?'
    )

    in_map_section = False
    for line in content.splitlines():
        if 'Linker script and memory map' in line:
            in_map_section = True
            continue
        if not in_map_section:
            continue

        m = sec_pattern.match(line)
        if m:
            sec_name = m.group(1)
            vma = int(m.group(2), 16)
            size = int(m.group(3), 16)
            lma = int(m.group(4), 16) if m.group(4) else None
            # Ignore debug symbols and non-allocated sections with address 0
            if size > 0 and vma != 0:
                sections.append({
                    'name': sec_name,
                    'vma': vma,
                    'size': size,
                    'lma': lma
                })

    # 3. Detect FreeRTOS ucHeap size
    freertos_heap = 0
    heap_match = re.search(r'\.bss\.ucHeap\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)', content)
    if heap_match:
        freertos_heap = int(heap_match.group(1), 16)
    else:
        sym_match = re.search(r'0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s+.*?ucHeap', content)
        if sym_match:
            freertos_heap = int(sym_match.group(1), 16)

    # 4. Detect STACK_SIZE and HEAP_SIZE symbols
    stack_size = 1024
    stack_match = re.search(r'(?:STACK_SIZE|_Min_Stack_Size)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)', content)
    if stack_match:
        val = stack_match.group(1)
        stack_size = int(val, 16) if val.startswith('0x') else int(val)

    libc_heap_size = 512
    libc_match = re.search(r'(?:HEAP_SIZE|_Min_Heap_Size)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)', content)
    if libc_match:
        val = libc_match.group(1)
        libc_heap_size = int(val, 16) if val.startswith('0x') else int(val)

    return {
        'regions': regions,
        'sections': sections,
        'freertos_heap': freertos_heap,
        'stack_size': stack_size,
        'libc_heap_size': libc_heap_size
    }


def find_region_for_addr(addr, regions):
    """Finds the memory region corresponding to a given physical address."""
    for reg_name, reg in regions.items():
        if reg['origin'] <= addr < reg['origin'] + reg['length']:
            return reg_name
    return None


def analyze_memory(map_path):
    """
    Parses and processes map file into structured memory statistics.
    """
    data = parse_map_file(map_path)
    if not data:
        return None

    regions = data['regions']
    sections = data['sections']
    freertos_heap = data['freertos_heap']
    stack_size = data['stack_size']
    libc_heap_size = data['libc_heap_size']

    rom_sections = []
    ram_sections = []
    other_sections = {}

    rom_used = 0
    ram_used = 0

    for sec in sections:
        vma = sec['vma']
        size = sec['size']
        lma = sec['lma']
        name = sec['name']

        is_noload = name in ('.bss', '.stack', '.heap') or 'noload' in name.lower() or name.startswith('.bss')
        if lma is not None and not is_noload:
            lma_reg = find_region_for_addr(lma, regions)
            if lma_reg == 'ROM':
                rom_used += size
                rom_sections.append({'name': f"{name} (LMA)", 'size': size, 'desc': 'Flash initializers'})

        vma_reg = find_region_for_addr(vma, regions)
        if vma_reg == 'ROM':
            rom_used += size
            desc = ''
            if name in ('.text', '.vectors'):
                desc = 'Code, ISR vectors'
            elif name == '.rodata':
                desc = 'Constants, strings'
            rom_sections.append({'name': name, 'size': size, 'desc': desc})
        elif vma_reg == 'RAM':
            ram_used += size
            ram_sections.append(sec)
        elif vma_reg:
            if vma_reg not in other_sections:
                other_sections[vma_reg] = []
            other_sections[vma_reg].append(sec)

    rom_total = regions.get('ROM', {}).get('length', 256 * 1024)
    ram_total = regions.get('RAM', {}).get('length', 64 * 1024)

    rom_free = rom_total - rom_used
    ram_free = ram_total - ram_used

    rom_pct = (rom_used / rom_total) * 100.0 if rom_total else 0.0
    ram_pct = (ram_used / ram_total) * 100.0 if ram_total else 0.0

    code_size = sum(s['size'] for s in rom_sections if s['name'] in ('.text', '.vectors'))
    rodata_size = sum(s['size'] for s in rom_sections if s['name'] == '.rodata')
    data_lma_size = sum(s['size'] for s in rom_sections if '(LMA)' in s['name'])

    std_rom_names = {'.text', '.vectors', '.rodata', '.ARM', '.init_array', '.fini_array', '.copy.table', '.zero.table'}
    custom_rom_sections = [
        s for s in rom_sections
        if s['name'] not in std_rom_names and '(LMA)' not in s['name']
    ]

    has_stack_sec = any(s['name'] == '.stack' for s in ram_sections)
    has_heap_sec = any(s['name'] == '.heap' for s in ram_sections)

    static_bss_used = ram_used - freertos_heap
    if has_stack_sec:
        static_bss_used -= stack_size
    if has_heap_sec:
        static_bss_used -= libc_heap_size
    if static_bss_used < 0:
        static_bss_used = 0

    std_ram_names = {'.data', '.bss', '.stack', '.heap'}
    custom_ram_sections = [s for s in ram_sections if s['name'] not in std_ram_names]

    status_label = "OK"
    if rom_pct >= 90.0 or ram_pct >= 90.0:
        status_label = "CRITICAL (Low Memory)"
    elif rom_pct >= 80.0 or ram_pct >= 80.0:
        status_label = "WARNING (Approaching Capacity)"

    return {
        'regions': regions,
        'sections': sections,
        'rom_used': rom_used,
        'rom_total': rom_total,
        'rom_free': rom_free,
        'rom_pct': rom_pct,
        'ram_used': ram_used,
        'ram_total': ram_total,
        'ram_free': ram_free,
        'ram_pct': ram_pct,
        'freertos_heap': freertos_heap,
        'code_size': code_size,
        'rodata_size': rodata_size,
        'data_lma_size': data_lma_size,
        'custom_rom_sections': custom_rom_sections,
        'static_bss_used': static_bss_used,
        'stack_size': stack_size,
        'libc_heap_size': libc_heap_size,
        'custom_ram_sections': custom_ram_sections,
        'other_sections': other_sections,
        'status_label': status_label,
    }


def print_compact_report(stats, elf_path):
    """Prints a concise 1-line memory summary for build progress logs."""
    elf_name = os.path.basename(elf_path)
    rom_used = stats['rom_used']
    rom_total = stats['rom_total']
    rom_pct = stats['rom_pct']
    ram_used = stats['ram_used']
    ram_total = stats['ram_total']
    ram_pct = stats['ram_pct']
    status = stats['status_label']
    print(f"[MEM] {elf_name:<14}: FLASH {format_bytes(rom_used)} / {format_kb(rom_total)} ({rom_pct:5.1f}%) | RAM {format_bytes(ram_used)} / {format_kb(ram_total)} ({ram_pct:5.1f}%) [{status}]")


def print_memory_report(stats, elf_path, build_type="Debug"):
    """Formats and prints the complete detailed memory summary table."""
    regions = stats['regions']
    other_sections = stats['other_sections']
    rom_used = stats['rom_used']
    rom_total = stats['rom_total']
    rom_free = stats['rom_free']
    rom_pct = stats['rom_pct']
    ram_used = stats['ram_used']
    ram_total = stats['ram_total']
    ram_free = stats['ram_free']
    ram_pct = stats['ram_pct']
    freertos_heap = stats['freertos_heap']
    code_size = stats['code_size']
    rodata_size = stats['rodata_size']
    data_lma_size = stats['data_lma_size']
    custom_rom_sections = stats['custom_rom_sections']
    static_bss_used = stats['static_bss_used']
    stack_size = stats['stack_size']
    libc_heap_size = stats['libc_heap_size']
    custom_ram_sections = stats['custom_ram_sections']
    status_label = stats['status_label']

    elf_basename = os.path.basename(elf_path)
    is_boot = 'boot' in elf_basename.lower()
    target_role = "CAN BOOTLOADER" if is_boot else "MAIN APPLICATION"

    line_sep = "=" * 78
    sub_sep = "-" * 78

    print(line_sep)
    print(f"       STM32C542 {target_role} Memory Footprint ({elf_basename})")
    print(line_sep)
    print("  Memory Region        Used [B]     Total [B]     Free [B]   Usage [%]  Visual Bar")
    print(sub_sep)

    # 1. FLASH
    print(f"  FLASH (ROM)        {format_bytes(rom_used)}   {format_kb(rom_total)}   {format_bytes(rom_free)}    {rom_pct:5.2f}%   [{make_bar(rom_pct)}]")
    print(f"    |-- .text        {format_bytes(code_size)}        -             -           -     (Code, ISR vectors)")
    if rodata_size > 0:
        print(f"    |-- .rodata       {format_bytes(rodata_size)}        -             -           -     (Constants, strings)")
    for cs in custom_rom_sections:
        print(f"    |-- {cs['name']:<13} {format_bytes(cs['size'])}        -             -           -     (Custom Flash section)")
    if data_lma_size > 0:
        print(f"    `-- .data (LMA)   {format_bytes(data_lma_size)}        -             -           -     (Flash initializers)")

    print(sub_sep)

    # 2. RAM
    print(f"  RAM (SRAM)         {format_bytes(ram_used)}   {format_kb(ram_total)}   {format_bytes(ram_free)}    {ram_pct:5.2f}%   [{make_bar(ram_pct)}]")
    if freertos_heap > 0:
        heap_pct = (freertos_heap / ram_total) * 100.0
        print(f"    |-- FreeRTOS Heap {format_bytes(freertos_heap)}        -             -       {heap_pct:5.2f}%   (ucHeap pool in .bss)")
    for cs in custom_ram_sections:
        cs_pct = (cs['size'] / ram_total) * 100.0
        print(f"    |-- {cs['name']:<13} {format_bytes(cs['size'])}        -             -       {cs_pct:5.2f}%   (Custom RAM section)")

    static_pct = (static_bss_used / ram_total) * 100.0
    print(f"    |-- Static & BSS  {format_bytes(static_bss_used)}        -             -        {static_pct:5.2f}%   (Globals, buffers)")

    stack_pct = (stack_size / ram_total) * 100.0
    print(f"    |-- Main Stack    {format_bytes(stack_size)}        -             -        {stack_pct:5.2f}%   (MSP / ISR stack)")

    libc_pct = (libc_heap_size / ram_total) * 100.0
    print(f"    `-- Libc Heap     {format_bytes(libc_heap_size)}        -             -        {libc_pct:5.2f}%   (sysmem / _sbrk)")

    # 3. Additional Regions (e.g. SHARED_RAM)
    for reg_name, reg_secs in other_sections.items():
        reg_total = regions.get(reg_name, {}).get('length', 0)
        reg_used = sum(s['size'] for s in reg_secs)
        reg_free = reg_total - reg_used
        reg_pct = (reg_used / reg_total) * 100.0 if reg_total else 0.0
        print(sub_sep)
        if reg_name == 'SHARED_RAM':
            print(f"  SHARED_RAM (IPC)   {format_bytes(reg_used)}       64 B          0 B     [Fixed]   (Mailbox @ 0x2000FFC0)")
            for s in reg_secs:
                print(f"    `-- {s['name']:<13} {format_bytes(s['size'])}        -             -           -     (Shared .noinit IPC)")
        else:
            print(f"  {reg_name:<18} {format_bytes(reg_used)}   {format_kb(reg_total)}   {format_bytes(reg_free)}    {reg_pct:5.2f}%   [{make_bar(reg_pct)}]")
            for s in reg_secs:
                print(f"    |-- {s['name']:<13} {format_bytes(s['size'])}        -             -           -")

    # Format build type display
    bt_lower = build_type.lower()
    display_build_type = 'Debug' if 'debug' in bt_lower else ('Release' if 'release' in bt_lower else build_type)

    # 4. Footer
    print(line_sep)
    print(f"  Target MCU : STM32C542RCT6 (ARM Cortex-M33 @ 144 MHz)")
    print(f"  Firmware   : {elf_basename} [{display_build_type}]")
    print(f"  Status     : {status_label} (FLASH: {100.0 - rom_pct:.1f}% free, RAM: {100.0 - ram_pct:.1f}% free)")
    print(line_sep)


def print_dual_dashboard(boot_map, app_map, boot_bin=None, app_bin=None, merged_bin=None, merged_hex=None):
    """Prints a consolidated Dual-Target Firmware & Memory Summary dashboard."""
    boot_stats = analyze_memory(boot_map) if boot_map and os.path.exists(boot_map) else None
    app_stats = analyze_memory(app_map) if app_map and os.path.exists(app_map) else None

    boot_flash_used = boot_stats['rom_used'] if boot_stats else (os.path.getsize(boot_bin) if boot_bin and os.path.exists(boot_bin) else 0)
    boot_flash_total = boot_stats['rom_total'] if boot_stats else (32 * 1024)
    boot_flash_pct = (boot_flash_used / boot_flash_total) * 100.0 if boot_flash_total else 0.0

    boot_ram_used = boot_stats['ram_used'] if boot_stats else 0
    boot_ram_total = boot_stats['ram_total'] if boot_stats else (64 * 1024 - 64)
    boot_ram_pct = (boot_ram_used / boot_ram_total) * 100.0 if boot_ram_total else 0.0

    app_flash_used = app_stats['rom_used'] if app_stats else (os.path.getsize(app_bin) if app_bin and os.path.exists(app_bin) else 0)
    app_flash_total = app_stats['rom_total'] if app_stats else (224 * 1024)
    app_flash_pct = (app_flash_used / app_flash_total) * 100.0 if app_flash_total else 0.0

    app_ram_used = app_stats['ram_used'] if app_stats else 0
    app_ram_total = app_stats['ram_total'] if app_stats else (64 * 1024 - 64)
    app_ram_pct = (app_ram_used / app_ram_total) * 100.0 if app_ram_total else 0.0

    app_heap = app_stats['freertos_heap'] if app_stats else 0
    app_heap_pct = (app_heap / app_ram_total) * 100.0 if app_ram_total else 0.0

    if merged_bin and os.path.exists(merged_bin):
        merged_size = os.path.getsize(merged_bin)
    elif boot_bin and app_bin and os.path.exists(boot_bin) and os.path.exists(app_bin):
        merged_size = 32768 + os.path.getsize(app_bin)
    else:
        merged_size = 32768 + app_flash_used

    start_addr = 0x08000000
    end_addr = start_addr + merged_size - 1

    line_sep = "=" * 88
    sub_sep = "-" * 88

    print(line_sep)
    print("                   STM32C542 Dual-Target Firmware & Memory Summary                      ")
    print(line_sep)
    print("  Target / Component      Flash Address        Flash Usage [B]       RAM Usage [B]      ")
    print(sub_sep)
    print(f"  CAN Bootloader (Os)     0x08000000 ( 32 KB)  {format_bytes(boot_flash_used)} ({boot_flash_pct:5.1f}%)     {format_bytes(boot_ram_used)} ({boot_ram_pct:5.1f}%)  ")
    print(f"  Main Application (RTOS) 0x08008000 (224 KB)  {format_bytes(app_flash_used)} ({app_flash_pct:5.1f}%)     {format_bytes(app_ram_used)} ({app_ram_pct:5.1f}%)  ")
    if app_heap > 0:
        print(f"    `-- FreeRTOS ucHeap   -                     -                    {format_bytes(app_heap)} ({app_heap_pct:5.1f}%)  ")
    print(f"  Shared IPC (.noinit)    0x2000FFC0 (  64 B)   -                        64 B (Fixed)   ")
    print(sub_sep)
    print(f"  Composite Flash Range:  0x{start_addr:08X} - 0x{end_addr:08X} ({merged_size:,} bytes / {merged_size/1024.0:.1f} KB)")
    print("  Output Files (build/debug_GCC_NUCLEO-C542RC/):")
    print("    - Merged Full Image : v_box_full.hex | v_box_full.bin (Ready for ST-Link)")
    print("    - Main Application  : v_box.elf | v_box.hex | v_box.bin (OTA Update)")
    print("    - CAN Bootloader    : v_box_boot.elf | v_box_boot.hex | v_box_boot.bin")
    print(line_sep)


def main():
    parser = argparse.ArgumentParser(description="STM32 Firmware Memory Footprint Reporter")
    parser.add_argument("elf_or_map", nargs="?", default=None, help="Path to ELF binary or .map file")
    parser.add_argument("--build-type", default="Debug", help="Build type (Debug, Release, etc.)")
    parser.add_argument("--compact", action="store_true", help="Print compact 1-line summary")
    parser.add_argument("--dual", nargs=2, metavar=("BOOT_MAP", "APP_MAP"), help="Print dual-target dashboard")
    parser.add_argument("--boot-bin", help="Path to boot binary for dual dashboard")
    parser.add_argument("--app-bin", help="Path to app binary for dual dashboard")
    parser.add_argument("--merged-bin", help="Path to merged binary for dual dashboard")
    parser.add_argument("--merged-hex", help="Path to merged hex for dual dashboard")
    args = parser.parse_args()

    if args.dual:
        boot_map = args.dual[0]
        app_map = args.dual[1]
        print_dual_dashboard(boot_map, app_map, args.boot_bin, args.app_bin, args.merged_bin, args.merged_hex)
        sys.exit(0)

    input_path = args.elf_or_map
    if not input_path:
        parser.print_help()
        sys.exit(1)

    if input_path.endswith('.map'):
        map_path = input_path
        elf_path = input_path[:-4] + '.elf'
    else:
        elf_path = input_path
        base = os.path.splitext(input_path)[0]
        map_path = base + '.map'

    if not os.path.exists(map_path):
        print(f"[WARN] Map file '{map_path}' not found. Cannot generate memory report.")
        sys.exit(0)

    stats = analyze_memory(map_path)
    if not stats:
        print(f"[WARN] Could not parse map file '{map_path}'.")
        sys.exit(0)

    if args.compact:
        print_compact_report(stats, elf_path)
    else:
        print_memory_report(stats, elf_path, args.build_type)


if __name__ == '__main__':
    main()
