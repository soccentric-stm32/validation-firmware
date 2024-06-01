/**
 * @file    hwval_sections.h
 * @brief   Linker-section attributes for the RAM budget.
 * @details DMA can reach DTCM only through the slow AHBS slave bus (RM0410
 *          2.1.6/2.1.8/2.1.13), so bulk DMA-fed test buffers live in SRAM1
 *          (".sram1", NOLOAD). MPU region 3 (bsp/mpu_setup.c) makes that span
 *          non-cacheable (GEN-007) and main.c zeroes it at boot.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @target  NUCLEO-F767ZI (STM32F767ZIT6)
 * @srs     NFR-004 (>= 256 KB SRAM1 and >= 64 KB DTCM free for the destructive
 *          RAM tests), GEN-007 (DMA buffers coherent with the caches)
 */

#ifndef HWVAL_SECTIONS_H
#define HWVAL_SECTIONS_H

/** Place a static buffer in non-cacheable SRAM1, 32-byte aligned. */
#define HWVAL_SRAM1 __attribute__((section(".sram1"), aligned(32)))

#endif /* HWVAL_SECTIONS_H */
