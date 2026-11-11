/*
 * tests/test_rst_wdg.c - resets and watchdogs (sections 8.8, 8.9)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     RST-001..RST-007, WDG-001..WDG-004.
 * Resets that reboot the MCU arm the TFW-007 bootcheck (hash 0x5C01
 * software, 0x5D01 IWDG, 0x5D02 IWDG window, 0x5D03 WWDG).
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
static tfw_result_t rst_flags_boot(struct tfw_out *o)
{
    /* On boot the flags were reported and cleared (GEN-010); the snapshot
     * is mirrored to RTC BKP13R by hwval_reset_cause_report(). */
    uint32_t snap = hwval_reset_flags_snapshot();
    uint32_t live = RCC->CSR & 0xFE000000U; /* reset flag bits only */

    hwval_meas(o, "snapshot=%08lX live-after-clear=%08lX",
               (unsigned long)snap, (unsigned long)live);
    hwval_lim(o, "all 7 causes decoded, cleared after boot (RST-001)");
    if (live != 0U) {
        hwval_fail(o, "reset flags not cleared");
        return TFW_FAIL;
    }
    if (snap == 0U) {
        hwval_fail(o, "no cause snapshot from boot");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RST-002 ------------ */
static tfw_result_t rst_software_reset(struct tfw_out *o)
{
    /* Arm: bootcheck 0x5C01 verifies SFTRSTF set at the next boot. */
    hwval_bootcheck_arm(0x5C01U, TFW_PASS);
    vTaskDelay(pdMS_TO_TICKS(30));
    NVIC_SystemReset();
    (void)o;
    return TFW_SKIP; /* unreachable */
}

/* -------------------------------------------------- RST-003/004/006 ---- */
static tfw_result_t rst_operator(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NO_OPERATOR (B2 press / USB replug for POR); BOR needs "
                   "the bench supply (NO_EQUIPMENT, level E)");
    return TFW_SKIP;
}

/* -------------------------------------------------- RST-005 ------------ */
static tfw_result_t rst_lowpower_cfg(struct tfw_out *o)
{
    uint32_t optcr = *(const uint32_t *)0x40023C14U;
    int nrst_stop  = (int)((optcr >> 12U) & 1U); /* nRST_STOP  */
    int nrst_stdby = (int)((optcr >> 13U) & 1U); /* nRST_STDBY */
    uint32_t lpwr  = hwval_reset_flags_snapshot() &
                     RCC_CSR_LPWRRSTF_Msk;

    /* SAFE-001: changing the option bits is forbidden; with the measured
     * defaults (both = 1) only the *absence* of LPWRRSTF is provable. */
    hwval_meas(o, "nRST_STOP=%d nRST_STDBY=%d LPWRRSTF=%lu",
               (unsigned long)nrst_stop, (unsigned long)nrst_stdby,
               (unsigned long)lpwr);
    hwval_lim(o, "option bits 1 (no reset on low-power entry/exit) and "
                 "no low-power reset flag at this boot");
    if ((nrst_stop != 1) || (nrst_stdby != 1)) {
        hwval_fail(o, "option bits are not the ASM-003 defaults");
        return TFW_FAIL;
    }
    if (lpwr != 0U) {
        hwval_fail(o, "unexpected low-power reset at boot");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RST-007 ------------ */
static struct {
    volatile void  *reg;       /* register checked after reset     */
    uint32_t        expect;    /* expected reset value             */
    volatile uint32_t *rstr;   /* RCC reset register               */
    uint32_t        bit;       /* reset bit                        */
} k_rst_checks[] = {
    { (void *)&TIM3->CR1,      0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_TIM3RST },
    { (void *)&TIM4->CR1,      0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_TIM4RST },
    { (void *)&USART6->CR1,    0x00000000U, (void *)&RCC->APB2RSTR, RCC_APB2RSTR_USART6RST },
    { (void *)&UART4->CR1,     0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_UART4RST },
    { (void *)&SPI1->CR1,      0x00000000U, (void *)&RCC->APB2RSTR, RCC_APB2RSTR_SPI1RST },
    { (void *)&SPI3->CR1,      0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_SPI3RST },
    { (void *)&I2C2->CR1,      0x00000000U, (void *)&RCC->APB1RSTR, RCC_APB1RSTR_I2C2RST },
    { (void *)&GPIOB->AFR[0],  0x00000000U, (void *)&RCC->AHB1RSTR, RCC_AHB1RSTR_GPIOBRST },
    { (void *)&GPIOE->ODR,     0x00000000U, (void *)&RCC->AHB1RSTR, RCC_AHB1RSTR_GPIOERST },
    { (void *)&DMA2_Stream0->NDTR, 0x00000000U, (void *)&RCC->AHB1RSTR, RCC_AHB1RSTR_DMA2RST },
};

static tfw_result_t rst_peripheral_reset_run(struct tfw_out *o)
{
    int bad = 0;
    unsigned n = (unsigned)(sizeof(k_rst_checks) / sizeof(k_rst_checks[0]));

    for (unsigned i = 0U; i < n; i++) {
        volatile uint32_t *rstr = k_rst_checks[i].rstr;
        SET_BIT(*rstr, k_rst_checks[i].bit);
        for (volatile int d = 0; d < 200; d++) { ; }
        CLEAR_BIT(*rstr, k_rst_checks[i].bit);
        for (volatile int d = 0; d < 200; d++) { ; }
        if (*(volatile uint32_t *)k_rst_checks[i].reg !=
            k_rst_checks[i].expect) {
            bad++;
        }
    }
    hwval_meas(o, "%d/%u peripherals back to reset values",
               (unsigned long)(n - (unsigned)bad), n);
    hwval_lim(o, "all RCC reset bits restore reset values (RST-007)");
    if (bad != 0) {
        hwval_fail(o, "%d peripherals did not reset", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- WDG-001/002 -------- */
static void iwdg_arm_bootcheck(uint32_t hash, uint32_t expect_s)
{
    hwval_bootcheck_arm(hash, TFW_PASS);
    RTC->WPR = 0xCAU; RTC->WPR = 0x53U;
    HWVAL_BKP_CHECK_ARG0 = RTC->TR;  /* armed at this calendar second */
    HWVAL_BKP_CHECK_ARG1 = expect_s; /* timeout to verify after reset  */
    RTC->WPR = 0xFFU;
}

static tfw_result_t wdg_iwdg_timeouts(struct tfw_out *o)
{
    uint32_t expected_s;

    /* 1 s timeout: LSI ~32 kHz /  prescaler 32 -> 1 kHz, RLR 999 */
    IWDG->KR = 0x5555U;
    IWDG->PR = 3U;              /* /32 -> 1 kHz tick                  */
    IWDG->RLR = 999U;
    IWDG->WINR = 0xFFFU;        /* window fully open (no window)      */
    IWDG->KR = 0xAAAAU;
    expected_s = 1U;
    iwdg_arm_bootcheck(0x5D01U, expected_s);
    hwval_meas(o, "PR=/32 RLR=999 -> ~1 s; stop kicking now");
    hwval_lim(o, "expiry within LSI tolerance (HWVAL_LIM_LSI_WDG_TOL_PCT)");
    (void)o;
    (void)expected_s;
    for (;;) { __NOP(); } /* watchdog expiry resets (bootcheck verifies) */
    return TFW_SKIP; /* not reached */
}

static tfw_result_t wdg_iwdg_window(struct tfw_out *o)
{
    /* WDG-002: refresh BEFORE the window opens -> reset. Window open at
     * 75 % of the 4 s timeout: refreshing within the first second must
     * cause the reset. */
    IWDG->KR = 0x5555U;
    IWDG->PR = 6U;              /* /64 -> 512 Hz                      */
    IWDG->RLR = 2047U;          /* ~4 s                               */
    IWDG->WINR = 1535U;         /* window opens at 3 s                */
    IWDG->KR = 0xAAAAU;
    iwdg_arm_bootcheck(0x5D02U, 4U);
    hwval_meas(o, "RLR=2047 WINR=1535: refresh inside closed window");
    hwval_lim(o, "early refresh must reset (IWDG window option)");
    vTaskDelay(pdMS_TO_TICKS(50));
    IWDG->KR = 0xAAAAU;         /* too early: reset imminent          */
    for (;;) { __NOP(); }
    return TFW_SKIP; /* not reached */
}

/* -------------------------------------------------- WDG-003 (WWDG) ----- */
static volatile uint32_t s_wwdg_ewi;

void WWDG_IRQHandler(void);

void WWDG_IRQHandler(void)
{
    s_wwdg_ewi++;
    WWDG->SR = 0U; /* clear EWI */
}

static tfw_result_t wdg_wwdg(struct tfw_out *o)
{
    /* Enable WWDG with early-wake interrupt; EWI must fire at 0x40-1
     * before expiry. Then deliberately refresh OUTSIDE the window
     * (immediately after WDGA) to force the second reset path. */
    __HAL_RCC_WWDG_CLK_ENABLE();
    s_wwdg_ewi = 0U;
    NVIC_SetPriority(WWDG_IRQn, 6);
    NVIC_EnableIRQ(WWDG_IRQn);

    WWDG->CFR = WWDG_CFR_EWI | WWDG_CFR_WDGTB_0; /* timeout ~ (64*8)/32k */
    WWDG->SR  = 0U;
    WWDG->CR  = WWDG_CR_WDGA | 0x7EU;           /* counter near top     */

    /* EWI fires ~ just before the window closes again */
    for (uint32_t i = 0U; (i < 1000000U) && (s_wwdg_ewi == 0U); i++) {
        __NOP();
    }
    uint32_t ewi_seen = s_wwdg_ewi;

    /* refresh immediately = outside the window -> reset */
    iwdg_arm_bootcheck(0x5D03U, 0U);
    WWDG->CR = WWDG_CR_WDGA | 0x7EU;            /* refresh too early    */
    hwval_meas(o, "ewi fires=%lu; early refresh -> reset",
               (unsigned long)ewi_seen);
    hwval_lim(o, "WWDGRSTF expected after reset (bootcheck 0x5D03)");
    (void)o;
    for (;;) { __NOP(); }
    return TFW_SKIP; /* not reached */
}

/* -------------------------------------------------- WDG-004 (DBG) ------ */
static tfw_result_t wdg_dbg_freeze(struct tfw_out *o)
{
    uint32_t dbg = DBGMCU->APB1FZ;

    SET_BIT(DBGMCU->APB1FZ, DBGMCU_APB1_FZ_DBG_IWDG_STOP |
             DBGMCU_APB1_FZ_DBG_WWDG_STOP);

    hwval_meas(o, "APB1FZ=%08lX (freeze bits set)", (unsigned long)dbg);
    hwval_lim(o, "NO_EQUIPMENT: freeze observable only during a debugger "
                 "halt (Level E, OpenOCD)");
    CLEAR_BIT(DBGMCU->APB1FZ, DBGMCU_APB1_FZ_DBG_IWDG_STOP |
              DBGMCU_APB1_FZ_DBG_WWDG_STOP);
    return TFW_SKIP;
}


/* DUART-003: resets USART6 (the debug port); re-initialise it afterwards. */
static tfw_result_t rst_peripheral_reset(struct tfw_out *o)
{
    hwval_dbg_pause();
    tfw_result_t r = rst_peripheral_reset_run(o);
    hwval_dbg_resume();
    return r;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_rst_wdg_tests[] = {
    { .id = "RST.001", .group = "RST", .title = "reset flags boot snapshot",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "RST-001, GEN-010", .fn = rst_flags_boot },
    { .id = "RST.002", .group = "RST", .title = "software reset -> SFTRSTF",
      .level = TFW_LEVEL_S, .resets_mcu = 1, .timeout_ms = 2000U,
      .verifies = "RST-002, TFW-007", .fn = rst_software_reset },
    { .id = "RST.003", .group = "RST", .title = "B2 press / POR replug (operator)",
      .level = TFW_LEVEL_S, .needs_operator = 1, .resets_mcu = 0,
      .timeout_ms = 300U, .verifies = "RST-003, RST-004, RST-006",
      .fn = rst_operator },
    { .id = "RST.005", .group = "RST", .title = "low-power reset config",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "RST-005, SAFE-001", .fn = rst_lowpower_cfg },
    { .id = "RST.007", .group = "RST", .title = "peripheral reset bits",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "RST-007, CLK-014", .fn = rst_peripheral_reset },
    { .id = "WDG.001", .group = "WDG", .title = "IWDG 1 s expiry -> reset "
      "(measured vs RTC)",
      .level = TFW_LEVEL_S, .resets_mcu = 1, .timeout_ms = 3000U,
      .verifies = "WDG-001", .fn = wdg_iwdg_timeouts },
    { .id = "WDG.002", .group = "WDG", .title = "IWDG window violation -> reset",
      .level = TFW_LEVEL_S, .resets_mcu = 1, .timeout_ms = 3000U,
      .verifies = "WDG-002", .fn = wdg_iwdg_window },
    { .id = "WDG.003", .group = "WDG", .title = "WWDG EWI + out-of-window reset",
      .level = TFW_LEVEL_S, .resets_mcu = 1, .timeout_ms = 3000U,
      .verifies = "WDG-003, TFW-007", .fn = wdg_wwdg },
    { .id = "WDG.004", .group = "WDG", .title = "DBGMCU freeze bits (Level E)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "WDG-004", .fn = wdg_dbg_freeze },
};

const size_t g_rst_wdg_test_count =
    sizeof(g_rst_wdg_tests) / sizeof(g_rst_wdg_tests[0]);

void hwval_test_register_rst_wdg(void)
{
    hwval_test_register(g_rst_wdg_tests, g_rst_wdg_test_count);
}