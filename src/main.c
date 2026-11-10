/*
 * hwval - firmware entry point
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  NUCLEO-F767ZI (STM32F767ZIT6)
 * SRS:     GEN-002, GEN-003, GEN-004, GEN-008..GEN-010, GEN-012
 *
 * Section 4 of the SRS: boot sequence and general firmware behaviour.
 * The FreeRTOS tasks (section 5), the CLI (section 6) and the test
 * framework (section 7) attach in later SRS sections.
 */

#include <stdio.h>
#include <string.h>
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "clock.h"
#include "version.h"
#include "uart3.h"
#include "fault.h"
#include "iwdg.h"
#include "reset_cause.h"
#include "mpu_setup.h"
#include "rtos.h"
#include "test_core.h"
#include "link.h"
#include "flash_ops.h"
#include "boot_slot.h"
#include "debug_uart.h"

/* NFR-004: .sram1 is NOLOAD; main.c zeroes it after the MPU is up so the
 * static test buffers get their C zero-init state (startup zeroes .bss). */
extern uint32_t __sram1_start;
extern uint32_t __sram1_end;

static void report_previous_fault(void)
{
    const hwval_fault_record_t *r = hwval_fault_record();
    char line[96];

    if (r->magic != 0x4856464CU /* "HVFL" */ || r->count == 0U) {
        return;
    }
    (void)snprintf(line, sizeof(line),
        "boot: previous fault #%lu CFSR=%08lX HFSR=%08lX PC=%08lX\r\n",
        (unsigned long)r->count, (unsigned long)r->cfsr,
        (unsigned long)r->hfsr, (unsigned long)r->pc);
    hwval_uart3_write(line, strlen(line));
}

int main(void)
{
    hwval_mpu_setup();          /* GEN-007: non-cacheable DMA region, OTP RO */
    /* NFR-004: zero-init the .sram1 buffers (NOLOAD; startup zeroes .bss)
     * - span ends are 32-aligned, so an equality loop is exact. */
    for (uint32_t *p = &__sram1_start; p != &__sram1_end; p++) {
        *p = 0U;
    }
    HAL_Init();                 /* HAL time base on TIM6 (GEN-012)           */
    hwval_clock_init();         /* GEN-004: 216 MHz, scale 1, OD, 7 WS       */
    hwval_dbg_init();           /* DUART-001: debug text on USART6 (PG14)    */
    hwval_uart3_init();         /* VCP console (USART3, section 6 builds on) */
    hwval_boot_init();          /* BOOT hand-over: slot and trial state      */
    hwval_link_boot_init();     /* LINK-010: previous reset was in link mode */
    hwval_dbg('I', "start v%s %s, slot %c %s, reset %s",
              hwval_version_string(), hwval_git_commit(),
              hwval_boot_slot() == HWVAL_SLOT_A ? 'A' :
              hwval_boot_slot() == HWVAL_SLOT_B ? 'B' : '?',
              hwval_boot_trial() == HWVAL_TRIAL_PENDING ? "on trial" :
              hwval_boot_trial() == HWVAL_TRIAL_CONFIRMED ? "confirmed"
                                                          : "no hand-over",
              hwval_reset_cause_name(hwval_reset_cause_flags()));
    if (hwval_link_boot_active() == 0) {
        hwval_version_banner();     /* GEN-003                               */
        hwval_reset_cause_report(); /* GEN-010                               */
        hwval_flash_bank_report();  /* SAFE-011: nDBANK boot warning        */
        report_previous_fault();    /* GEN-008 evidence after fault reset    */
        hwval_bootcheck_evaluate(); /* TFW-007: post-reset test verdicts     */
        hwval_rtos_report_panic();  /* FRT-004/005 hook evidence after reset */
    }
    hwval_iwdg_start(HWVAL_IWDG_TIMEOUT_MS); /* GEN-009                      */

    hwval_uart3_puts("HWVAL: boot ok, starting RTOS tasks (section 5)\r\n");

    hwval_test_register_rtos(); /* RTOS self-tests (section 5.3)             */
    hwval_test_register_dev();  /* device identity (section 8.1)             */
    hwval_test_register_cm7();  /* Cortex-M7 core (section 8.2)              */
    hwval_test_register_cache_mpu(); /* caches/TCM/MPU (section 8.3)          */
    hwval_test_register_ram();  /* internal RAM (section 8.4)                */
    hwval_test_register_flash();/* flash/OB/OTP (section 8.5)                */
    hwval_test_register_clk();  /* clocks (section 8.6)                      */
    hwval_test_register_pwr();  /* power (section 8.7)                       */
    hwval_test_register_rst_wdg(); /* resets + watchdogs (sections 8.8/8.9)  */
    hwval_test_register_rtc();  /* RTC + backup registers (section 8.10)     */
    hwval_test_register_int();  /* NVIC/EXTI/SYSCFG (section 8.11)           */
    hwval_test_register_dma();  /* DMA controllers (section 8.12)            */
    hwval_test_register_gpio(); /* GPIO matrix (section 8.13)                */
    hwval_test_register_led_btn(); /* LEDs + buttons (section 8.14)          */
    hwval_test_register_adc();  /* ADC1/2/3 (section 8.15)                   */
    hwval_test_register_dac();  /* DAC (section 8.16)                        */
    hwval_test_register_tim();  /* TIM1-14 + LPTIM1 (section 8.17)           */
    hwval_test_register_uart(); /* USART/UART (section 8.18)                 */
    hwval_test_register_spi();  /* SPI + I2S (section 8.19)                  */
    hwval_test_register_sai_spdif(); /* SAI + SPDIFRX (section 8.20/8.21)     */
    hwval_test_register_i2c();  /* I2C1-4 (section 8.22)                     */
    hwval_test_register_can();  /* CAN1-3 (section 8.23)                     */
    hwval_test_register_mem();  /* SDMMC/QSPI/FMC (8.24/8.25/8.26)           */
    hwval_test_register_eth_usb(); /* ETH + USB FS/HS (8.27/8.28/8.29)       */
    hwval_test_register_gfx_misc(); /* LTDC/DMA2D/JPEG/DCMI + misc (8.30/31) */
    hwval_test_register_dbg_brd(); /* DBG + ST-LINK/board (8.32/8.33)       */
    hwval_test_register_safe(); /* safety enforcement (section 9)            */
    hwval_test_registration_done(); /* TFW-022: restore results once         */
    hwval_rtos_start();         /* FRT-010: cli/runner/hb/wdg + scheduler    */

    /* Unreachable: the scheduler never returns. */
    hwval_fault_assert(__FILE__, __LINE__);
    return 0;
}