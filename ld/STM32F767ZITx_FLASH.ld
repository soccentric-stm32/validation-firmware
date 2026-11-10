/*
 * ld/STM32F767ZITx_FLASH.ld - HWVAL linker script
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  STM32F767ZIT6 (1 MB x 2 flash, DTCM 128 KB, SRAM1 368 KB,
 *          SRAM2 16 KB), NUCLEO-F767ZI
 * SRS:     GEN-006, GEN-007, TFW-022, ASM-004, NFR-004
 *
 * HWVAL runs as a slot image under the A/B bootloader (SRS-BOOT-F767
 * LAY-002): it is linked at the slot offset 0x0800C000 and BOOT maps the
 * active bank at 0x08000000, so the same image runs from slot A and slot B.
 * The board stays in dual-bank mode, so the destructive single-bank sector-11
 * flash tests report WRONG_FLASH_MODE (SAFE-011) and never erase a slot.
 */

ENTRY(Reset_Handler)

/* Main/exception stack top: top of DTCM RAM. */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);

MEMORY
{
    /* One bootloader slot: 976 KiB from 0x0800C000 (LAY-002). */
    FLASH   (rx)  : ORIGIN = 0x0800C000, LENGTH = 976K
    DTCMRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    /* SRAM1 minus the 64 KiB .noinit block at its tail. */
    SRAM1   (rwx) : ORIGIN = 0x20020000, LENGTH = 304K
    /* .noinit: survives any reset that does not remove power (TFW-022
     * results table, fault/panic records). 64 KiB because the results
     * table holds one row per registered test (256 max). */
    NOINIT  (rw)  : ORIGIN = 0x2006C000, LENGTH =  64K
    /* SRAM2, general use by the kernel/tasks. */
    SRAM2   (rwx) : ORIGIN = 0x2007C000, LENGTH =   8K
    /* .noncacheable: MPU non-cacheable block for DMA buffers (GEN-007). */
    NCACHE  (rw)  : ORIGIN = 0x2007E000, LENGTH =   8K
    /* ITCM RAM: core-only, zero-wait-state (CACHE-004, CORE-009). No
     * sections are placed here; tests copy code/data at runtime. */
    ITCMRAM (rwx) : ORIGIN = 0x00000000, LENGTH =  16K
}

PROVIDE(hwval_itcm_base = ORIGIN(ITCMRAM));

/* No libc heap: FreeRTOS heap_4 is static (section 5 of the SRS). */
_Min_Heap_Size  = 0;
/* MSP exception stack guard. */
_Min_Stack_Size = 0x1000;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata.*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > DTCMRAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > DTCMRAM

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        __noinit_start = .;
        *(.noinit .noinit.*)
        . = ALIGN(4);
        __noinit_end = .;
    } > NOINIT

    .noncacheable (NOLOAD) :
    {
        . = ALIGN(32);
        __noncacheable_start = .;
        *(.noncacheable .noncacheable.*)
        . = ALIGN(32);
        __noncacheable_end = .;
    } > NCACHE

    /* NFR-004: the bulk DMA-fed test buffers (DMA streams, DMA2D, ADC)
     * are placed here so that, after the move, >= 64 KB of DTCM stays
     * free for the destructive RAM tests. HWVAL_SRAM1 in
     * board/hwval_sections.h is the matching section attribute. MPU
     * region 3 (src/mpu_setup.c) marks the span non-cacheable.
     * Buffers are written before they are read and zeroed at boot in
     * main.c, so NOLOAD is safe. */
    .sram1 (NOLOAD) :
    {
        . = ALIGN(32);
        __sram1_start = .;
        *(.sram1 .sram1.*)
        . = ALIGN(32);
        __sram1_end = .;
    } > SRAM1

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > DTCMRAM
}

/* GEN-006: the whole image (including the .data load image) stays inside
 * the bootloader slot. The build fails otherwise. */
__flash_image_start = ORIGIN(FLASH);
__flash_image_end = LOADADDR(.data) + SIZEOF(.data);
ASSERT(ORIGIN(FLASH) == 0x0800C000,
       "GEN-006: the image must be linked at the bootloader slot offset")
ASSERT(__flash_image_end <= ORIGIN(FLASH) + LENGTH(FLASH),
       "GEN-006: image does not fit the bootloader slot")
ASSERT(_edata - _sdata + (_ebss - _sbss) + _Min_Stack_Size <= LENGTH(DTCMRAM),
       "DTCM overflow")
ASSERT(__sram1_end <= ORIGIN(NOINIT),
       ".sram1 spills into the reserved NOINIT tail of SRAM1")
ASSERT((__sram1_start & 0x1FFFF) == 0,
       ".sram1 base must stay 128 KB aligned for MPU region 3")