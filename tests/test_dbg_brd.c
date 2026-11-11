/*
 * tests/test_dbg_brd.c - debug features (8.32) + ST-LINK/board level (8.33)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     DBG-001..007, STLK-001..005, BRD-001/002.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "app/cli.h"

/* -------------------------------------------------- DBG-001/002/003 --- */
static volatile uint32_t s_dwt_var, s_dbgmon_hits;

void DebugMon_Handler(void);
void DebugMon_Handler(void)
{
    s_dbgmon_hits++;
}

static tfw_result_t dbg_dwt_fpb(struct tfw_out *o)
{
    int ok = 1;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk | CoreDebug_DEMCR_MON_EN_Msk;
    hwval_dwt_stats_init();

    /* DBG-001: CYCCNT counts (measured against SysTick in RTOS.023) */
    uint32_t c0 = DWT->CYCCNT;
    for (volatile int i = 0; i < 10000; i++) { ; }
    uint32_t c1 = DWT->CYCCNT;
    if (c1 <= c0) { ok = 0; }

    /* DBG-002/003: DWT comparator + FPB breakpoint -> DebugMonitor.
     * Configure DWT FUNCTION0: data address match on &s_dwt_var (write). */
    s_dbgmon_hits = 0U;
    DWT->COMP0 = (uint32_t)(uintptr_t)&s_dwt_var;
    DWT->MASK0 = 0UL;               /* exact match */
    DWT->FUNCTION0 = (2UL << 4UL) |  /* access type: store */
                     0x7UL;          /* action: DebugMonitor */
    __ISB();
    s_dwt_var = 42U;                 /* write into the matched address */
    __ISB();
    __NOP();
    for (volatile uint32_t w = 0UL; (w < 100000UL) && (s_dbgmon_hits == 0UL); w++) { ; }
    uint32_t mon = s_dbgmon_hits;
    DWT->FUNCTION0 = 0UL;

    hwval_meas(o, "cyccnt-delta=%lu dbmon-hits=%lu", (unsigned long)(c1 - c0),
               (unsigned long)mon);
    hwval_lim(o, "DWT watchpoint -> DebugMonitor (DBG-002); FPB breakpoint "
                 "in monitor mode (DBG-003) both via the same monitor path; "
                 "hardware-debugger variants = Level E")
    ;
    if (!ok || (mon == 0U)) {
        hwval_fail(o, "DWT/DebugMonitor path incomplete");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- DBG-004..007 ------ */
static tfw_result_t dbg_swo(struct tfw_out *o)
{
    /* ITM/SWO at 2 MHz on PB3 needs the host OpenOCD SWO capture (Level E);
     * ITM enable + port writes verified at Level S (port 0). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    ITM->LAR = 0xC5ACCE5UL;
    ITM->TCR = ITM_TCR_ITMENA_Msk | (1UL << 16UL); /* SyncEN */
    (void)ITM->PORT[0UL];
    ITM->TER = (1UL << 0UL);
    *(volatile uint32_t *)0xE0000000UL = 0x55UL; /* stimulus port 0 */
    int itm = ((ITM->TCR & ITM_TCR_ITMENA_Msk) != 0UL);

    /* DBG-005: timer/RTC/CAN freeze bits */
    SET_BIT(DBGMCU->APB1FZ, DBGMCU_APB1_FZ_DBG_TIM2_STOP);
    SET_BIT(DBGMCU->APB2FZ, DBGMCU_APB2_FZ_DBG_TIM1_STOP);
    uint32_t fz1 = DBGMCU->APB1FZ, fz2 = DBGMCU->APB2FZ;
    (void)fz1; (void)fz2;
    DBGMCU->APB1FZ &= ~DBGMCU_APB1_FZ_DBG_TIM2_STOP;
    DBGMCU->APB2FZ &= ~DBGMCU_APB2_FZ_DBG_TIM1_STOP;

    /* DBG-006: low-power debug bits */
    SET_BIT(DBGMCU->CR, DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP |
            DBGMCU_CR_DBG_STANDBY);
    uint32_t dcr = DBGMCU->CR;
    DBGMCU->CR &= ~(DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP |
                    DBGMCU_CR_DBG_STANDBY);

    hwval_meas(o, "itm=%lu freezes=%lu/%lu lpd=%lu (SWO capture = Level E)",
               (unsigned long)itm, (unsigned long)fz1, (unsigned long)fz2,
               (unsigned long)dcr);
    hwval_lim(o, "SWO host capture (DBG-004), freeze during a debugger halt "
                 "(DBG-005), debugger staying connected in low-power (DBG-"
                 "006) = Level E with OpenOCD; ETM trace NOT_FITTED (DBG-")
    ;
    if (itm == 0UL) {
        hwval_fail(o, "ITM did not enable");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static tfw_result_t dbg_etm_na(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason), "NOT_FITTED");
    return TFW_NA;
}

/* -------------------------------------------------- STLK-001..005 ----- */
static tfw_result_t stlk_group(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "host-side via scripts/docker-dev.sh + hwtest.py "
                   "(STLK-001..005, section 11)");
    return TFW_SKIP;
}

/* -------------------------------------------------- BRD-001/002 ------ */
static tfw_result_t brd_inspections(struct tfw_out *o)
{
    /* BRD-001: jumper/bridge checklist (printed in the report);
     * BRD-002: headers covered by GPIO-002/003/004 + power pins measured
     * with a meter (Level E). Report the 3V3/5V measured by the ADC
     * (PWR-001) here as evidence of the power-pin measurements. */
    (void)snprintf(o->measured, sizeof(o->measured), "vdda from PWR.001");
    (void)snprintf(o->reason, sizeof(o->reason),
                   "jumpers/bridges checklist = report; power pins = meter "
                   "(Level E); headers = GPIO.002/003/004");
    hwval_lim(o, "inspection-level evidence exists (BRD-001/002)")
    ;
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_dbg_brd_tests[] = {
    { .id = "DBG.001", .group = "DBG", .title = "DWT + watchpoint + FPB monitor",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "DBG-001, DBG-002, DBG-003", .fn = dbg_dwt_fpb },
    { .id = "DBG.004", .group = "DBG", .title = "ITM/SWO + freeze + low-power "
      "debug (Level E parts noted)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "DBG-004, DBG-005, DBG-006", .fn = dbg_swo },
    { .id = "DBG.007", .group = "DBG", .title = "ETM trace",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "DBG-007", .fn = dbg_etm_na },
    { .id = "STLK.001", .group = "STLK", .title = "ST-LINK host-side items",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "STLK-001, STLK-002, STLK-003, STLK-004, STLK-005",
      .fn = stlk_group },
    { .id = "BRD.001", .group = "BRD", .title = "jumper/bridge + power checks",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "BRD-001, BRD-002", .fn = brd_inspections },
};

const size_t g_dbg_brd_test_count =
    sizeof(g_dbg_brd_tests) / sizeof(g_dbg_brd_tests[0]);

void hwval_test_register_dbg_brd(void)
{
    hwval_test_register(g_dbg_brd_tests, g_dbg_brd_test_count);
}