/**
******************************************************************************
* @file    stm32c542xc_boot_flash.ld
* @brief   Linker script for STM32C542RCT6 CAN Bootloader (32 KB Flash)
******************************************************************************
* @attention
*
* Copyright (c) 2026 STMicroelectronics / v-box project.
* All rights reserved.
******************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

HEAP_SIZE = 0x200;
STACK_SIZE = 0x400;

MEMORY
{
  ROM (rx)        : org = 0x08000000, len = 0x08000   /* 32 KB Bootloader */
  RAM (xrw)       : org = 0x20000000, len = 0x0FFC0   /* 64 KB - 64 B Shared RAM */
  SHARED_RAM (rw) : org = 0x2000FFC0, len = 0x00040   /* 64 B IPC Shared RAM (.noinit) */
}

SECTIONS
{
  .vectors :
  {
    . = ALIGN(8);
    KEEP(*(.vectors));
    . = ALIGN(8);
  } > ROM

  .text :
  {
    . = ALIGN(8);
    *(.text);
    *(.text*);
    *(.glue_7);
    *(.glue_7t);
    *(.eh_frame);
    KEEP (*(.init));
    KEEP (*(.fini));
    . = ALIGN(8);
    _etext = .;
  } > ROM

  .rodata :
  {
    . = ALIGN(8);
    *(.rodata);
    *(.rodata*);
    . = ALIGN(8);
  } > ROM

  .ARM.extab (READONLY) :
  {
    . = ALIGN(8);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(8);
  } > ROM

  .ARM (READONLY) :
  {
    . = ALIGN(8);
    __exidx_start = .;
    *(.ARM.exidx*);
    __exidx_end = .;
    . = ALIGN(8);
  } > ROM

  .preinit_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*));
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(8);
  } > ROM

  .init_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)));
    KEEP (*(.init_array*));
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(8);
  } > ROM

  .fini_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)));
    KEEP (*(.fini_array*));
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(8);
  } > ROM

  .copy.table (READONLY) :
  {
    . = ALIGN(8);
    __copy_table_start__ = .;
    LONG(LOADADDR(.data));
    LONG (ADDR(.data));
    LONG (SIZEOF(.data) / 4);
    __copy_table_end__ = .;
  } > ROM

  .zero.table (READONLY) :
  {
    . = ALIGN(8);
    __zero_table_start__ = .;
    LONG (ADDR(.bss));
    LONG (SIZEOF(.bss) / 4);
    __zero_table_end__ = .;
  } > ROM

  .data :
  {
    . = ALIGN(8);
    _sidata = LOADADDR(.data);
    __data_start__ = .;
    _sdata = .;
    *(.data);
    *(.data*);
    . = ALIGN(8);
    _edata = .;
  } > RAM AT> ROM

  .bss :
  {
    . = ALIGN(8);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss);
    *(.bss*);
    *(COMMON);
    . = ALIGN(8);
    _ebss = .;
    __bss_end__ = _ebss;
  } > RAM

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    __end__ = .;
    PROVIDE (end = .);
    _heap_start = .;
    . += HEAP_SIZE;
    . = ALIGN(8);
    _heap_end = .;
    __HeapLimit = .;
  } > RAM

  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    __StackLimit = .;
    . += STACK_SIZE;
    . = ALIGN(8);
    __StackTop = .;
    _estack = .;
    __stack = .;
  } > RAM

  .boot_shared (NOLOAD) :
  {
    . = ALIGN(4);
    _sboot_shared = .;
    KEEP(*(.noinit.boot_shared*))
    . = ALIGN(4);
    _eboot_shared = .;
  } > SHARED_RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 :
  {
    *(.ARM.attributes)
  }
}
