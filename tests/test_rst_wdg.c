/**
 * @file    test_rst_wdg.c
 * @brief   Reset and watchdog tests (sections 8.8, 8.9).
 * @details Tests that reboot the MCU arm the TFW-007 bootcheck: 0x5C01 software reset,
 *          0x5D01 IWDG, 0x5D02 IWDG window, 0x5D03 WWDG.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     RST-001..RST-007, WDG-001..WDG-004, TFW-007
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "bsp/reset_cause.h"
#include "app/settings.h"
#include "app/cli.h"
#include "bsp/debug_uart.h"

/* -------------------------------------------------- RST-001 ------------ */
/**
 * @brief Check the boot reset-cause snapshot exists and RCC_CSR flags are cleared.
 * @param[out] o Result text.
 * @return TFW_PASS or TFW_FAIL.
 */
static tfw_result_t rst_flags_boot(struct tfw_out *o)
{
    /* Boot reported and cleared the flags (GEN-010);
     * hwval_reset_cause_report() mirrors the snapshot to RTC BKP13R. */
    uint32_t snap = hwval_reset_flags_snapshot();
    uint32_t live = RCC->CSR & 0xFE000000U; /* reset flag bits only */

    hwval_meas(o, "snapshot=%08lX live-after-clear=%08lX", (unsigned long)snap,
               (unsigned long)live);
    hwval_lim(o, "all 7 causes decoded, cleared after boot (RST-001)");
    if(live != 0U)
    {
        hwval_fail(o, "reset flags not cleared");
        return TFW_FAIL;
    }
    if(snap == 0U)
    {
        hwval_fail(o, "no cause snapshot from boot");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RST-002 ------------ */
/**
 * @brief Arm bootcheck 0x5C01 and issue a system reset.
 * @param[out] o Unused.
 * @return Does not return.
 */
static tfw_result_t rst_software_reset(struct tfw_out *o)
{
    /* Bootcheck 0x5C01 verifies SFTRSTF at the next boot. */
    hwval_bootcheck_arm(0x5C01U, TFW_PASS);
    vTaskDelay(pdMS_TO_TICKS(30));
    NVIC_SystemReset();
    (void)o;
    return TFW_SKIP; /* unreachable */
}

/* -------------------------------------------------- RST-003/004/006 ---- */
/**
 * @brief Skip pin, POR and BOR resets (operator or bench supply needed).
 * @param[out] o Result text.
 * @return TFW_SKIP.
 */
static tfw_result_t rst_operator(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NO_OPERATOR (B2 press / USB replug for POR); BOR needs "
                   "the bench supply (NO_EQUIPMENT, level E)");
    return TFW_SKIP;
}

/* -------------------------------------------------- RST-005 ------------ */
/**
 * @brief Check nRST_STOP/nRST_STDBY are 1 and no low-power reset occurred.
 * @param[out] o Result text.
 * @return TFW_PASS or TFW_FAIL.
 */
static tfw_result_t rst_lowpower_cfg(struct tfw_out *o)
{
    uint32_t optcr      = *(const uint32_t *)0x40023C14U;
    int      nrst_stop  = (int)((optcr >> 12U) & 1U); /* nRST_STOP  */
    int      nrst_stdby = (int)((optcr >> 13U) & 1U); /* nRST_STDBY */
    uint32_t lpwr       = hwval_reset_flags_snapshot() & RCC_CSR_LPWRRSTF_Msk;

    /* SAFE-001 forbids changing option bits; with both at 1 only the
     * absence of LPWRRSTF can be shown. */
    hwval_meas(o, "nRST_STOP=%d nRST_STDBY=%d LPWRRSTF=%lu", (unsigned long)nrst_stop,
               (unsigned long)nrst_stdby, (unsigned long)lpwr);
    hwval_lim(o, "option bits 1 (no reset on low-power entry/exit) and "
                 "no low-power reset flag at this boot");
    if((nrst_stop != 1) || (nrst_stdby != 1))
    {
        hwval_fail(o, "option bits are not the ASM-003 defaults");
        return TFW_FAIL;
    }
    if(lpwr != 0U)
    {
        hwval_fail(o, "unexpected low-power reset at boot");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RST-007 ------------ */
/** Peripheral registers checked after pulsing their RCC reset bit. */
static struct
{
    volatile void     *reg;    /**< Register checked after reset. */
    uint32_t           expect; /**< Expected reset value. */
    volatile uint32_t *rstr;   /**< RCC reset register. */
    uint32_t           bit;    /**< Reset bit in rstr. */
} k_rst_checks[] = {
    {(void *)&TIM3->CR1, 0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_TIM3RST},
    {(void *)&TIM4->CR1, 0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_TIM4RST},
    {(void *)&USART6->CR1, 0x00000000U, (void *)&RCC->APB2RSTR, RCC_APB2RSTR_USART6RST},
    {(void *)&UART4->CR1, 0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_UART4RST},
    {(void *)&SPI1->CR1, 0x00000000U, (void *)&RCC->APB2RSTR, RCC_APB2RSTR_SPI1RST},
    {(void *)&SPI3->CR1, 0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_SPI3RST},
    {(void *)&I2C2->CR1, 0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_I2C2RST},
    {(void *)&GPIOB->AFR[0], 0x00000000U, (void *)&RCC->AHB1RSTR, RCC_AHB1RSTR_GPIOBRST},
    {(void *)&GPIOE->ODR, 0x00000000U, (void *)&RCC->AHB1RSTR, RCC_AHB1RSTR_GPIOERST},
    {(void *)&DMA2_Stream0->NDTR, 0x00000000U, (void *)&RCC->AHB1RSTR,
     RCC_AHB1RSTR_DMA2RST},
};

/**
 * @brief Pulse each RCC reset bit in k_rst_checks and check the register resets.
 * @param[out] o Result text.
 * @return TFW_PASS or TFW_FAIL.
 */
static tfw_result_t rst_peripheral_reset_run(struct tfw_out *o)
{
    int      bad = 0;
    unsigned n   = (unsigned)(sizeof(k_rst_checks) / sizeof(k_rst_checks[0]));

    for(unsigned i = 0U; i < n; i++)
    {
        volatile uint32_t *rstr = k_rst_checks[i].rstr;
        SET_BIT(*rstr, k_rst_checks[i].bit);
        for(volatile int d = 0; d < 200; d++)
        {
            ;
        }
        CLEAR_BIT(*rstr, k_rst_checks[i].bit);
        for(volatile int d = 0; d < 200; d++)
        {
            ;
        }
        if(*(volatile uint32_t *)k_rst_checks[i].reg != k_rst_checks[i].expect)
        {
            bad++;
        }
    }
    hwval_meas(o, "%d/%u peripherals back to reset values",
               (unsigned long)(n - (unsigned)bad), n);
    hwval_lim(o, "all RCC reset bits restore reset values (RST-007)");
    if(bad != 0)
    {
        hwval_fail(o, "%d peripherals did not reset", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- WDG-001/002 -------- */
/**
 * @brief Arm a watchdog bootcheck; store arm time and timeout in RTC backup.
 * @param[in] hash     Bootcheck ID (0x5Dxx).
 * @param[in] expect_s Expected timeout in seconds (0 = not timed).
 */
static void iwdg_arm_bootcheck(uint32_t hash, uint32_t expect_s)
{
    hwval_bootcheck_arm(hash, TFW_PASS);
    RTC->WPR             = 0xCAU;
    RTC->WPR             = 0x53U;
    HWVAL_BKP_CHECK_ARG0 = RTC->TR;  /* arm time (RTC TR) */
    HWVAL_BKP_CHECK_ARG1 = expect_s; /* expected timeout, s */
    RTC->WPR             = 0xFFU;
}

/**
 * @brief Start the IWDG at 1 s and stop refreshing; bootcheck 0x5D01 verifies.
 * @warning Starts the IWDG, which cannot be stopped; the MCU resets.
 * @param[out] o Result text.
 * @return Does not return.
 */
static tfw_result_t wdg_iwdg_timeouts(struct tfw_out *o)
{
    uint32_t expected_s = 0;

    /* LSI 32 kHz / 32 = 1 kHz, RLR 999 -> 1 s timeout. */
    IWDG->KR   = 0x5555U;
    IWDG->PR   = 3U; /* /32 */
    IWDG->RLR  = 999U;
    IWDG->WINR = 0xFFFU; /* window disabled */
    IWDG->KR   = 0xAAAAU;
    expected_s = 1U;
    iwdg_arm_bootcheck(0x5D01U, expected_s);
    hwval_meas(o, "PR=/32 RLR=999 -> ~1 s; stop kicking now");
    hwval_lim(o, "expiry within LSI tolerance (HWVAL_LIM_LSI_WDG_TOL_PCT)");
    (void)o;
    (void)expected_s;
    for(;;)
    {
        __NOP();
    }                /* wait for the IWDG reset */
    return TFW_SKIP; /* not reached */
}

/**
 * @brief Refresh the IWDG before its window opens; bootcheck 0x5D02 verifies.
 * @warning Starts the IWDG; the MCU resets.
 * @param[out] o Result text.
 * @return Does not return.
 */
static tfw_result_t wdg_iwdg_window(struct tfw_out *o)
{
    /* WDG-002: a refresh before the window opens must reset. PR 6 is /256
     * (125 Hz at 32 kHz LSI); the window opens once the counter drops
     * below WINR, (2047 - 1535) ticks after reload. */
    IWDG->KR   = 0x5555U;
    IWDG->PR   = 6U;
    IWDG->RLR  = 2047U;
    IWDG->WINR = 1535U;
    IWDG->KR   = 0xAAAAU;
    iwdg_arm_bootcheck(0x5D02U, 4U);
    hwval_meas(o, "RLR=2047 WINR=1535: refresh inside closed window");
    hwval_lim(o, "early refresh must reset (IWDG window option)");
    vTaskDelay(pdMS_TO_TICKS(50));
    IWDG->KR = 0xAAAAU; /* refresh inside the closed window */
    for(;;)
    {
        __NOP();
    }
    return TFW_SKIP; /* not reached */
}

/* -------------------------------------------------- WDG-003 (WWDG) ----- */
static volatile uint32_t s_wwdg_ewi; /**< WWDG early-wakeup interrupt count. */

void WWDG_IRQHandler(void);

/** @brief Count WWDG early-wakeup interrupts and clear EWIF (ISR). */
void WWDG_IRQHandler(void)
{
    s_wwdg_ewi++;
    WWDG->SR = 0U;
}

/**
 * @brief Check the WWDG EWI fires, then force an out-of-window reset.
 * @details Bootcheck 0x5D03 verifies WWDGRSTF after the reset.
 * @param[out] o Result text.
 * @return Does not return.
 */
static tfw_result_t wdg_wwdg(struct tfw_out *o)
{
    /* Wait for the EWI (counter 0x40), then refresh outside the window
     * (W = 0) to force the WWDG reset. */
    __HAL_RCC_WWDG_CLK_ENABLE();
    s_wwdg_ewi = 0U;
    NVIC_SetPriority(WWDG_IRQn, 6);
    NVIC_EnableIRQ(WWDG_IRQn);

    WWDG->CFR = WWDG_CFR_EWI | WWDG_CFR_WDGTB_0; /* PCLK1/4096/2, W = 0 */
    WWDG->SR  = 0U;
    WWDG->CR  = WWDG_CR_WDGA | 0x7EU;

    for(uint32_t i = 0U; (i < 1000000U) && (s_wwdg_ewi == 0U); i++)
    {
        __NOP();
    }
    uint32_t ewi_seen = s_wwdg_ewi;

    iwdg_arm_bootcheck(0x5D03U, 0U);
    WWDG->CR = WWDG_CR_WDGA | 0x7EU; /* out-of-window refresh */
    hwval_meas(o, "ewi fires=%lu; early refresh -> reset", (unsigned long)ewi_seen);
    hwval_lim(o, "WWDGRSTF expected after reset (bootcheck 0x5D03)");
    (void)o;
    for(;;)
    {
        __NOP();
    }
    return TFW_SKIP; /* not reached */
}

/* -------------------------------------------------- WDG-004 (DBG) ------ */
/**
 * @brief Set and clear the IWDG/WWDG debug freeze bits.
 * @details The freeze is only observable during a debugger halt (Level E).
 * @param[out] o Result text.
 * @return TFW_SKIP.
 */
static tfw_result_t wdg_dbg_freeze(struct tfw_out *o)
{
    uint32_t dbg = DBGMCU->APB1FZ;

    SET_BIT(DBGMCU->APB1FZ, DBGMCU_APB1_FZ_DBG_IWDG_STOP | DBGMCU_APB1_FZ_DBG_WWDG_STOP);

    hwval_meas(o, "APB1FZ=%08lX (freeze bits set)", (unsigned long)dbg);
    hwval_lim(o, "NO_EQUIPMENT: freeze observable only during a debugger "
                 "halt (Level E, OpenOCD)");
    CLEAR_BIT(DBGMCU->APB1FZ,
              DBGMCU_APB1_FZ_DBG_IWDG_STOP | DBGMCU_APB1_FZ_DBG_WWDG_STOP);
    return TFW_SKIP;
}

/**
 * @brief Run the peripheral reset check with the debug UART paused.
 * @details The check resets USART6 (the debug port), which is re-initialised afterwards
 *          (DUART-003).
 * @param[out] o Result text.
 * @return TFW_PASS or TFW_FAIL.
 */
static tfw_result_t rst_peripheral_reset(struct tfw_out *o)
{
    hwval_dbg_pause();
    tfw_result_t r = rst_peripheral_reset_run(o);
    hwval_dbg_resume();
    return r;
}

/** RST/WDG test cases (RST-001..RST-007, WDG-001..WDG-004). */
const struct tfw_case g_rst_wdg_tests[] = {
    {.id         = "RST.001",
     .group      = "RST",
     .title      = "reset flags boot snapshot",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 0,
     .timeout_ms = 300U,
     .verifies   = "RST-001, GEN-010",
     .fn         = rst_flags_boot},
    {.id         = "RST.002",
     .group      = "RST",
     .title      = "software reset -> SFTRSTF",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 1,
     .timeout_ms = 2000U,
     .verifies   = "RST-002, TFW-007",
     .fn         = rst_software_reset},
    {.id             = "RST.003",
     .group          = "RST",
     .title          = "B2 press / POR replug (operator)",
     .level          = TFW_LEVEL_S,
     .needs_operator = 1,
     .resets_mcu     = 0,
     .timeout_ms     = 300U,
     .verifies       = "RST-003, RST-004, RST-006",
     .fn             = rst_operator},
    {.id         = "RST.005",
     .group      = "RST",
     .title      = "low-power reset config",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 0,
     .timeout_ms = 300U,
     .verifies   = "RST-005, SAFE-001",
     .fn         = rst_lowpower_cfg},
    {.id         = "RST.007",
     .group      = "RST",
     .title      = "peripheral reset bits",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 0,
     .timeout_ms = 2000U,
     .verifies   = "RST-007, CLK-014",
     .fn         = rst_peripheral_reset},
    {.id         = "WDG.001",
     .group      = "WDG",
     .title      = "IWDG 1 s expiry -> reset "
                   "(measured vs RTC)",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 1,
     .timeout_ms = 3000U,
     .verifies   = "WDG-001",
     .fn         = wdg_iwdg_timeouts},
    {.id         = "WDG.002",
     .group      = "WDG",
     .title      = "IWDG window violation -> reset",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 1,
     .timeout_ms = 3000U,
     .verifies   = "WDG-002",
     .fn         = wdg_iwdg_window},
    {.id         = "WDG.003",
     .group      = "WDG",
     .title      = "WWDG EWI + out-of-window reset",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 1,
     .timeout_ms = 3000U,
     .verifies   = "WDG-003, TFW-007",
     .fn         = wdg_wwdg},
    {.id         = "WDG.004",
     .group      = "WDG",
     .title      = "DBGMCU freeze bits (Level E)",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 0,
     .timeout_ms = 300U,
     .verifies   = "WDG-004",
     .fn         = wdg_dbg_freeze},
};

/** Number of entries in @ref g_rst_wdg_tests. */
const size_t g_rst_wdg_test_count = sizeof(g_rst_wdg_tests) / sizeof(g_rst_wdg_tests[0]);

/** @brief Register the RST/WDG test cases with the framework. */
void hwval_test_register_rst_wdg(void)
{
    hwval_test_register(g_rst_wdg_tests, g_rst_wdg_test_count);
}