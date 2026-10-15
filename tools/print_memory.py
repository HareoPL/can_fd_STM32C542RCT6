#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Memory footprint analysis script for STM32 firmware builds.
Parses the linker map file (or ELF file via size tool) and prints a detailed,
human-readable summary table of FLASH and RAM usage, including FreeRTOS heap,
system stack, and custom sections.
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
    # Example:
    # Memory Configuration
    # Name             Origin             Length             Attributes
    # ROM              0x08000000         0x00040000         xr
    # RAM              0x20000000         0x00010000         xrw
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
    # Look for .bss.ucHeap or ucHeap in map file
    freertos_heap = 0
    heap_match = re.search(r'\.bss\.ucHeap\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)', content)
    if heap_match:
        freertos_heap = int(heap_match.group(1), 16)
    else:
        # Fallback check for ucHeap symbol
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


def print_memory_report(data, elf_path, build_type="Debug"):
    """Formats and prints the complete memory summary table."""
    regions = data['regions']
    sections = data['sections']
    freertos_heap = data['freertos_heap']
    stack_size = data['stack_size']
    libc_heap_size = data['libc_heap_size']

    # Classify sections into regions
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

        # Check LMA (Load Memory Address) for ROM placement (e.g. .data initializers stored in Flash)
        # NOLOAD sections (like .bss, .stack, .heap) do not consume ROM space
        is_noload = name in ('.bss', '.stack', '.heap') or 'noload' in name.lower() or name.startswith('.bss')
        if lma is not None and not is_noload:
            lma_reg = find_region_for_addr(lma, regions)
            if lma_reg == 'ROM':
                rom_used += size
                rom_sections.append({'name': f"{name} (LMA)", 'size': size, 'desc': 'Flash initializers'})

        # Check VMA (Virtual/Execution Address)
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

    # Consolidate standard ROM sections
    code_size = sum(s['size'] for s in rom_sections if s['name'] in ('.text', '.vectors'))
    rodata_size = sum(s['size'] for s in rom_sections if s['name'] == '.rodata')
    data_lma_size = sum(s['size'] for s in rom_sections if '(LMA)' in s['name'])
    
    # Custom sections in ROM (not .text, .vectors, .rodata, .data LMA, .ARM*, .*array, .*table)
    std_rom_names = {'.text', '.vectors', '.rodata', '.ARM', '.init_array', '.fini_array', '.copy.table', '.zero.table'}
    custom_rom_sections = [
        s for s in rom_sections 
        if s['name'] not in std_rom_names and '(LMA)' not in s['name']
    ]

    # Calculate RAM subcategories
    # Static & BSS is whatever RAM is used excluding FreeRTOS heap, main stack, and libc heap
    # Check if stack and libc heap have dedicated sections (.stack, .heap)
    has_stack_sec = any(s['name'] == '.stack' for s in ram_sections)
    has_heap_sec = any(s['name'] == '.heap' for s in ram_sections)

    static_bss_used = ram_used - freertos_heap
    if has_stack_sec:
        static_bss_used -= stack_size
    if has_heap_sec:
        static_bss_used -= libc_heap_size
    if static_bss_used < 0:
        static_bss_used = 0

    # Custom sections in RAM (not .data, .bss, .stack, .heap)
    std_ram_names = {'.data', '.bss', '.stack', '.heap'}
    custom_ram_sections = [s for s in ram_sections if s['name'] not in std_ram_names]

    # Status evaluation
    status_label = "OK"
    if rom_pct >= 90.0 or ram_pct >= 90.0:
        status_label = "CRITICAL (Low Memory)"
    elif rom_pct >= 80.0 or ram_pct >= 80.0:
        status_label = "WARNING (Approaching Capacity)"

    elf_basename = os.path.basename(elf_path)

    # Print Table
    line_sep = "=" * 78
    sub_sep = "-" * 78

    print(line_sep)
    print("                   STM32C542RCT6 Memory Footprint Summary")
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

    # 3. Additional Regions (if any defined in linker script)
    for reg_name, reg_secs in other_sections.items():
        reg_total = regions.get(reg_name, {}).get('length', 0)
        reg_used = sum(s['size'] for s in reg_secs)
        reg_free = reg_total - reg_used
        reg_pct = (reg_used / reg_total) * 100.0 if reg_total else 0.0
        print(sub_sep)
        print(f"  {reg_name:<18} {format_bytes(reg_used)}   {format_kb(reg_total)}   {format_bytes(reg_free)}    {reg_pct:5.2f}%   [{make_bar(reg_pct)}]")
        for s in reg_secs:
            print(f"    |-- {s['name']:<13} {format_bytes(s['size'])}        -             -           -")

    # Format build type display
    bt_lower = build_type.lower()
    if 'debug' in bt_lower:
        display_build_type = 'Debug'
    elif 'release' in bt_lower:
        display_build_type = 'Release'
    else:
        display_build_type = build_type

    # 4. User Requested Footer
    print(line_sep)
    print(f"  Target MCU : STM32C542RCT6 (ARM Cortex-M33 @ 144 MHz)")
    print(f"  Firmware   : {elf_basename} [{display_build_type}]")
    print(f"  Status     : {status_label} (FLASH: {100.0 - rom_pct:.1f}% free, RAM: {100.0 - ram_pct:.1f}% free)")
    print(line_sep)


def main():
    parser = argparse.ArgumentParser(description="STM32 Firmware Memory Footprint Reporter")
    parser.add_argument("elf_or_map", help="Path to ELF binary or .map file")
    parser.add_argument("--build-type", default="Debug", help="Build type (Debug, Release, etc.)")
    args = parser.parse_args()

    input_path = args.elf_or_map

    # Determine map file path
    if input_path.endswith('.map'):
        map_path = input_path
        elf_path = input_path[:-4] + '.elf'
    else:
        elf_path = input_path
        # Look for .map next to .elf
        base = os.path.splitext(input_path)[0]
        map_path = base + '.map'

    if not os.path.exists(map_path):
        print(f"[WARN] Map file '{map_path}' not found. Cannot generate memory report.")
        sys.exit(0)

    data = parse_map_file(map_path)
    if not data:
        print(f"[WARN] Could not parse map file '{map_path}'.")
        sys.exit(0)

    print_memory_report(data, elf_path, args.build_type)


if __name__ == '__main__':
    main()
