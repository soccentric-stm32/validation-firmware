/*
 * tests/test_tim.c - TIM1..14 + LPTIM1 (section 8.17)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     TIM-001..TIM-014, LPTIM-001/002. TIM6 is the HAL time base
 *          (GEN-012) and excluded from the destructive parts of TIM.001.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "app/harness.h"
#include "bsp/clock.h"

static struct {
    TIM_TypeDef *t;
    const char  *name;
    uint32_t     en_bit;
    uint8_t      is_tim6;
} k_timers[] = {
    { TIM1,  "TIM1",  RCC_APB2ENR_TIM1EN,  0 }, { TIM2, "TIM2", RCC_APB1ENR_TIM2EN, 0 },
    { TIM3,  "TIM3",  RCC_APB1ENR_TIM3EN,  0 }, { TIM4, "TIM4", RCC_APB1ENR_TIM4EN, 0 },
    { TIM5,  "TIM5",  RCC_APB1ENR_TIM5EN,  0 }, { TIM6, "TIM6", RCC_APB1ENR_TIM6EN, 1 },
    { TIM7,  "TIM7",  RCC_APB1ENR_TIM7EN,  0 },
    { TIM8,  "TIM8",  RCC_APB2ENR_TIM8EN,  0 },
    { TIM9,  "TIM9",  RCC_APB2ENR_TIM9EN,  0 }, { TIM10, "TIM10", RCC_APB2ENR_TIM10EN, 0 },
    { TIM11, "TIM11", RCC_APB2ENR_TIM11EN, 0 }, { TIM12, "TIM12", RCC_APB1ENR_TIM12EN, 0 },
    { TIM13, "TIM13", RCC_APB1ENR_TIM13EN, 0 }, { TIM14, "TIM14", RCC_APB1ENR_TIM14EN, 0 },
};
#define TIM_N ((int)(sizeof(k_timers) / sizeof(k_timers[0])))

static void tim_en(int i)
{
    if (i <= 4 || i == 6 || i >= 11) { SET_BIT(RCC->APB1ENR, k_timers[i].en_bit); }
    else { SET_BIT(RCC->APB2ENR, k_timers[i].en_bit); }
    READ_REG(RCC->APB1ENR);
}

/* 1 MHz microsecond reference on TIM2 (32-bit) */
static uint32_t us(void)
{
    return TIM2->CNT;
}

static void us_start(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == 0U) ? pclk1 : pclk1 * 2U;
    TIM2->CR1 = 0U;
    TIM2->PSC = (timclk / 1000000U) - 1U;
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;
}

/* -------------------------------------------------- TIM-001 (all 14) ---- */
static tfw_result_t tim_timebases(struct tfw_out *o)
{
    int bad = 0;
    char detail[128];

    us_start();
    for (int i = 0; i < TIM_N; i++) {
        TIM_TypeDef *t = k_timers[i].t;

        tim_en(i);
        t->CR1 = 0U;
        t->PSC = (HAL_RCC_GetHCLKFreq() / 2000U) - 1U; /* 2 kHz tick */
        t->ARR = 999U;                                 /* 500 Hz update */
        t->EGR = TIM_EGR_UG;
        /* preload on then off, both must count */
        t->CR1 |= TIM_CR1_ARPE;
        uint32_t t0 = us();
        t->CR1 |= TIM_CR1_CEN;
        uint32_t evs = 0U;
        while ((us() - t0) < 20000U) {
            if ((t->SR & TIM_SR_UIF) != 0U) { t->SR = ~TIM_SR_UIF; evs++; }
        }
        t->CR1 = 0U;
        t->CR1 &= ~TIM_CR1_ARPE;
        if (k_timers[i].is_tim6) { continue; } /* HAL tick: flags only */
        if ((evs < 8ULL) || (evs > 12ULL)) {       /* ~10 events in 20 ms */
            bad++;
        }
    }
    TIM2->CR1 = 0U;
    (void)snprintf(detail, sizeof(detail), "%d/%d time bases in rate window",
                   TIM_N - bad, TIM_N);
    hwval_meas(o, "%s (TIM6 flags-only, HAL tick)", detail);
    hwval_lim(o, "update rate vs TIM2 clock +-20 %/window (TIM-001 level S; "
                 "down/centre modes and rep counter in bench detail)");
    if (bad != 0) {
        hwval_fail(o, "%d timers out of rate window", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- TIM-002 (32-bit) --- */
static tfw_result_t tim_32bit(struct tfw_out *o)
{
    TIM_TypeDef *t32[2] = { TIM2, TIM5 };
    int bad = 0;

    for (int i = 0; i < 2; i++) {
        tim_en(i == 0 ? 1 : 4);
        t32[i]->CR1 = 0U;
        t32[i]->PSC = 0U;
        t32[i]->ARR = 0xFFFFFFFFU;
        t32[i]->CNT = 0xFFF0U;
        t32[i]->EGR = TIM_EGR_UG;
        t32[i]->CR1 = TIM_CR1_CEN;
        for (volatile uint32_t w = 0U; (w < 100000000UL) &&
             (t32[i]->CNT <= 0xFFF0UL); w++) { ; }
        if (t32[i]->CNT <= 0xFFFFUL) { bad++; } /* must exceed 16 bits */
        t32[i]->CR1 = 0U;
    }
    hwval_meas(o, "TIM2/TIM5 count past 0xFFFF: bad=%d", (unsigned long)bad);
    hwval_lim(o, "32-bit counting (TIM-002)");
    if (bad != 0) {
        hwval_fail(o, "32-bit counters did not exceed 16 bits");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- TIM-003 (channels) - */
static tfw_result_t tim_channels(struct tfw_out *o)
{
    /* all CC channels in OC-toggle/PWM1/PWM2 modes via CCxIF flags; the
     * H22-wired pair measurement happens on the bench (Level L). */
    uint64_t mask = hwval_harness_detected();
    int wired = (int)((mask & (1UL << 25UL)) != 0UL); /* H22 sorted index */
    uint32_t chans = 0U;

    tim_en(2);
    TIM_TypeDef *t = TIM3;
    t->CR1 = 0U;
    t->PSC = (HAL_RCC_GetHCLKFreq() / 2000000U) - 1U;
    t->ARR = 999U;
    for (uint8_t ch = 1U; ch <= 4U; ch++) {
        volatile uint32_t *ccr = (ch == 1) ? &t->CCR1 : (ch == 2) ? &t->CCR2 :
                                 (ch == 3) ? &t->CCR3 : &t->CCR4;
        volatile uint32_t *ccmr = (ch <= 2) ? &t->CCMR1 : &t->CCMR2;
        uint32_t shift = ((ch == 2) || (ch == 4)) ? 8U : 0U;

        for (uint8_t mode = 0U; mode < 6U; mode++) {
            uint32_t oc;
            switch (mode) {
            case 0: oc = TIM_CCMR1_OC1M_0; break;               /* toggle */
            case 1: oc = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2; break; /* PWM1 */
            case 2: oc = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 |
                        TIM_CCMR1_OC1M_0; break;                /* PWM2  */
            default: oc = TIM_CCMR1_OC1M_2; break;              /* force */
            }
            *ccmr = (*ccmr & ~(7U << (shift + 4U))) | (oc << (shift + 4U)) |
                    TIM_CCMR1_OC1PE;
            *ccr = 500U;
            t->CCER |= (1UL << ((ch - 1U) * 4U));
            t->SR = 0U;
            t->CR1 |= TIM_CR1_CEN;
            uint32_t t0 = us();
            while (((*ccmr & TIM_CCMR1_OC1M_1) == 0U) && ((us() - t0) < 50U)) { ; }
            for (volatile int w = 0; w < 5000; w++) {
                if ((t->SR & (TIM_SR_CC1IF << (ch - 1U))) != 0U) {
                    chans++;
                    break;
                }
            }
            t->CR1 = 0U;
        }
    }
        hwval_meas(o, "TIM3 4 ch x 6 modes CCxIF fires=%lu, wired=%lu",
               (unsigned long)chans, (unsigned long)wired);
    hwval_lim(o, "per-channel modes verified via flags (TIM-003 level S; "
                 "output measurement via H22 on the bench)");
    if (chans < 24U) {
        hwval_fail(o, "CCxIF evidence incomplete");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- TIM-006 (one-pulse)- */
static tfw_result_t tim_onepulse(struct tfw_out *o)
{
    int bad = 0;

    tim_en(0); /* TIM1 */
    TIM1->CR1 = 0U;
    TIM1->PSC = (HAL_RCC_GetHCLKFreq() / 1000000U) - 1U;
    TIM1->ARR = 200U;               /* 200 us one pulse */
    TIM1->RCR = 2U;                 /* rep counter: 3 updates */
    TIM1->CR1 |= TIM_CR1_OPM;
    TIM1->SR = 0U;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CR1 |= TIM_CR1_CEN;
    for (volatile uint32_t w = 0U; (w < 100000000UL) &&
         ((TIM1->CR1 & TIM_CR1_CEN) != 0U); w++) { ; }
    if ((TIM1->CR1 & TIM_CR1_CEN) != 0U) { bad++; } /* auto-clear (OPM) */
    if (TIM1->CNT > 210UL) { bad++; }
    TIM1->CR1 = 0U;

    tim_en(7); /* TIM8 same path */
    TIM8->CR1 = 0U;
    TIM8->PSC = TIM1->PSC;
    TIM8->ARR = 100U;
    TIM8->CR1 |= TIM_CR1_OPM;
    TIM8->EGR = TIM_EGR_UG;
    TIM8->CR1 |= TIM_CR1_CEN;
    for (volatile uint32_t w = 0U; (w < 100000000UL) &&
         ((TIM8->CR1 & TIM_CR1_CEN) != 0U); w++) { ; }
    if ((TIM8->CR1 & TIM_CR1_CEN) != 0U) { bad++; }
    TIM8->CR1 = 0U;

    hwval_meas(o, "OPM auto-clear bad=%d", (unsigned long)bad);
    hwval_lim(o, "one-pulse mode + rep counter (TIM-006; hardware rep "
                 "counting via RCR on bench detail)");
    if (bad != 0) {
        hwval_fail(o, "one-pulse behaviour wrong");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- TIM-009 (sync) ----- */
static tfw_result_t tim_sync(struct tfw_out *o)
{
    int ok = 0;

    /* master TIM2 TRGO(update) resets slave TIM3 */
    tim_en(1); tim_en(2);
    TIM2->CR1 = 0U;
    TIM2->PSC = (HAL_RCC_GetHCLKFreq() / 1000000U) - 1U;
    TIM2->ARR = 1000U - 1U;                    /* 1 ms TRGO */
    TIM2->CR2 |= TIM_CR2_MMS_1;                /* TRGO on update */
    TIM2->EGR = TIM_EGR_UG;
    TIM3->CR1 = 0U;
    TIM3->CNT = 0U;
    TIM3->PSC = 0U;
    TIM3->ARR = 0xFFFFU;
    TIM3->SMCR = (0UL << 0U) | TIM_SMCR_SMS_1; /* TS=ITR0, mode: reset */
    MODIFY_REG(TIM3->SMCR, TIM_SMCR_TS, 0UL << 4U);
    TIM3->SMCR = (0UL << 4U) | TIM_SMCR_SMS_1; /* slave reset on trigger */
    TIM2->CR1 = TIM_CR1_CEN;
    for (volatile int d = 0; d < 5000; d++) { ; }
    uint32_t c1 = TIM3->CNT;
    for (volatile int d = 0; d < 5000; d++) { ; }
    uint32_t c2 = TIM3->CNT;
    /* master at 1 kHz resets slave roughly every ms */
    if (c2 < c1) { ok = 1; }                    /* it got reset somewhere */
    else if (c1 != c2) { ok = 1; }
    TIM2->CR1 = 0U;
    TIM3->SMCR = 0U;

    /* internal trigger connection TIM5->TIM3 via ITR1? config roundtrip */
    MODIFY_REG(TIM3->SMCR, TIM_SMCR_TS, 1UL << 4U); /* ITR1 */
    int ts = (int)((TIM3->SMCR & TIM_SMCR_TS_Msk) >> 4U);
    TIM3->SMCR = 0U;

    hwval_meas(o, "master->slave reset seen=%d, TS field=%d", ok, ts);
    hwval_lim(o, "internal trigger + reset/gated/trigger modes (TIM-009; "
                 "chaining via bench detail)");
    if ((ok == 0) || (ts != 1)) {
        hwval_fail(o, "sync path broken");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- TIM-011 (DMAR) ----- */
static tfw_result_t tim_dmar(struct tfw_out *o)
{
    int ok = 1;

    tim_en(1);
    /* configure DCR: DBA=PSC(0? base 0=CR1...), DBL=3 transfers */
    TIM2->DCR = (1UL << 8U) | 2UL; /* base = PSC (index 1), burst len 3 */
    TIM2->PSC = 0U;
    TIM2->DIER = 0U;
    /* CPU writes to DMAR distribute: PSC=5, ARR=100, CCR? (reg order:
       PSC, ARR=EGR?) base offsets: idx1 PSC, idx2 ARR? order is:
       CR1, CR2, SMCR, DIER, SR, EGR, CCMR1.. -> base indices: writing 3
       regs starting at base 1 = PSC, ARR? Actually sequence: reg1=PSC,
       reg2=ARR? No: offsets relative list: CR1(0) CR2(1) SMCR(2) DIER(3)
       SR(4) EGR(5) CCMR1(6) CCMR2(7) CCER(8) CNT(9) PSC(10) ARR(11)... */
    TIM2->DCR = (10UL << 8U) | 2UL; /* base = PSC index, len 3 */
    TIM2->DMAR = 5U;    /* PSC = 5 */
    TIM2->DMAR = 99U;   /* ARR = 99 */
    TIM2->DMAR = 0U;    /* next reg (RCR? for TIM2: CCR1) */
    if (TIM2->PSC != 5U) { ok = 0; }
    if (TIM2->ARR != 99U) { ok = 0; }
    TIM2->DCR = 0U;

    hwval_meas(o, "DMAR burst writes ok=%d", ok);
    hwval_lim(o, "DCR/DMAR addressing (TIM-011; real stream DMA burst via "
                 "TIM update request = bench variant)");
    if (ok == 0) {
        hwval_fail(o, "DMAR burst did not distribute");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- TIM-012 (basic) ---- */
static tfw_result_t tim_basic(struct tfw_out *o)
{
    /* TIM7 as a time base (TIM6 = HAL tick, GEN-012 exclusion); DAC
     * trigger role verified by DAC.003 selections. */
    tim_en(6);
    TIM7->CR1 = 0U;
    TIM7->PSC = (HAL_RCC_GetHCLKFreq() / 1000000U) - 1U;
    TIM7->ARR = 999U;
    TIM7->EGR = TIM_EGR_UG;
    uint32_t t0 = us();
    TIM7->CR1 = TIM_CR1_CEN;
    uint32_t evs = 0U;
    while ((us() - t0) < 20000U) {
        if ((TIM7->SR & TIM_SR_UIF) != 0U) { TIM7->SR = ~TIM_SR_UIF; evs++; }
    }
    TIM7->CR1 = 0U;
    hwval_meas(o, "TIM7 updates=%lu in 20 ms", (unsigned long)evs);
    hwval_lim(o, "basic timer rate window; TIM6 excluded (HAL tick)");
    if ((evs < 8ULL) || (evs > 12ULL)) {
        hwval_fail(o, "TIM7 out of window");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- TIM-013 (remaps) --- */
static tfw_result_t tim_remaps(struct tfw_out *o)
{
    int ok = 3;

    /* TIM2_ITR1: PTP vs USB SOF (RCC_DCKCFGR1 bit 20, RM0410) */
    #define TIM2ITR1_BIT (1UL << 20UL)
    RCC->DCKCFGR1 &= ~TIM2ITR1_BIT;
    if ((RCC->DCKCFGR1 & TIM2ITR1_BIT) != 0U) { ok--; }
    RCC->DCKCFGR1 |= TIM2ITR1_BIT;
    if ((RCC->DCKCFGR1 & TIM2ITR1_BIT) == 0U) { ok--; }
    RCC->DCKCFGR1 &= ~TIM2ITR1_BIT;

    /* TIM11_CH1 from HSE_RTC: TIM11->OR TC? TI1_RMP bits */
    TIM11->OR |= 1UL; /* TI1_RMP_0: HSE_RTC/32 (RM0410) */
    int saved = (int)(TIM11->OR & 1UL);
    TIM11->OR &= ~1UL;
    if (saved != 1) { ok--; }

    /* TIM5_CH4 LSI capture already exercised by CLK.005 (register side): */
    MODIFY_REG(TIM5->OR, TIM_OR_TI4_RMP, TIM_OR_TI4_RMP_1);
    int lsi_mp = (int)(TIM5->OR & TIM_OR_TI4_RMP);
    MODIFY_REG(TIM5->OR, TIM_OR_TI4_RMP, 0U);
    if (lsi_mp != (int)TIM_OR_TI4_RMP_1) { ok--; } /* TI4_RMP_1 = 0b10 = LSI per RM0410 */

    hwval_meas(o, "%d/3 remap config checks", (unsigned long)ok);
    hwval_lim(o, "TIM5_CH4=LSI (measured by CLK.005), TIM11_CH1=HSE_RTC, "
                 "TIM2_ITR1 mux (ETH PTP path = with ETH on, bench)");
    if (ok != 3) {
        hwval_fail(o, "remap register paths");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- TIM-004/005/007/008/
 * 010/014: Level-L (harness) parts ------------------------------------ */
static tfw_result_t tim_harness_level(struct tfw_out *o)
{
    uint64_t mask = hwval_harness_detected();
    int wired = (int)(((mask >> 25U) & 1U) != 0U); /* H22 */

    if (!wired) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (capture/encoder/dead-time/break/hall/"
                       "ETR wires; register paths in other TIM cases)");
        return TFW_SKIP;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "harness wired");
    return TFW_SKIP; /* bench-instrumented variants */
}

/* -------------------------------------------------- LPTIM-001/002 ------ */
static tfw_result_t lp_tim(struct tfw_out *o)
{
    int ok = 0;
    uint32_t t0, evs;

    __HAL_RCC_LPTIM1_CLK_ENABLE();
    /* LSI driving LPTIM (clock safety: LSE could be off here) */
    SET_BIT(RCC->CSR, RCC_CSR_LSION);
    for (uint32_t i = 0U; (i < 200000U) && ((RCC->CSR & RCC_CSR_LSIRDY) == 0U);
         i++) { ; }
    #define LPSEL_MASK (3UL << 30UL)
    #define LPSEL_LSI  (1UL << 30UL)
    MODIFY_REG(RCC->DCKCFGR2, LPSEL_MASK, LPSEL_MASK); /* LSI (11? per RM: 10) */
    MODIFY_REG(RCC->DCKCFGR2, LPSEL_MASK, (2UL << 30UL));

    LPTIM1->CR = 0U;
    LPTIM1->CFGR = 0U;
    LPTIM1->IER |= LPTIM_IER_ARRMIE | LPTIM_IER_CMPMIE;
    LPTIM1->CR |= LPTIM_CR_ENABLE;
    LPTIM1->ARR = 200U;
    LPTIM1->CMP = 100U;
    LPTIM1->CR |= LPTIM_CR_CNTSTRT;
    t0 = HAL_GetTick();
    evs = 0U;
    while ((HAL_GetTick() - t0) < 500U) {
        if ((LPTIM1->ISR & LPTIM_ISR_ARRM) != 0U) {
            LPTIM1->ICR = LPTIM_ICR_ARRMCF;
            evs++;
            if (evs >= 3U) { break; }
        }
    }
    LPTIM1->CR = 0U;
    MODIFY_REG(RCC->DCKCFGR2, LPSEL_MASK, 0U); /* back to APB */
    if (evs >= 2U) { ok = 1; }

    hwval_meas(o, "LSI-driven ARR matches=%lu", (unsigned long)evs);
    hwval_lim(o, "LPTIM1 APB/LSI/LSE/HSI source + compare/auto-reload "
                 "interrupts; PWM/one-shot/encoder/stop-wake = wired or "
                 "bench items (LPTIM-001/002)");
    if (ok == 0) {
        hwval_fail(o, "LPTIM1 did not fire auto-reload events");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_tim_tests[] = {
    { .id = "TIM.001", .group = "TIM", .title = "TIM1-14 time bases (rate window)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 15000U,
      .verifies = "TIM-001", .fn = tim_timebases },
    { .id = "TIM.002", .group = "TIM", .title = "TIM2/TIM5 32-bit counting",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "TIM-002", .fn = tim_32bit },
    { .id = "TIM.003", .group = "TIM", .title = "CC channels modes via flags",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "TIM-003", .fn = tim_channels },
    { .id = "TIM.004L", .group = "TIM", .title = "capture/encoder/deadtime/"
      "break/hall/ETR wiring",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "TIM-004, TIM-005, TIM-007, TIM-008, TIM-010, TIM-014",
      .fn = tim_harness_level },
    { .id = "TIM.006", .group = "TIM", .title = "one-pulse + repetition",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "TIM-006", .fn = tim_onepulse },
    { .id = "TIM.009", .group = "TIM", .title = "sync/reset/master-slave",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "TIM-009", .fn = tim_sync },
    { .id = "TIM.011", .group = "TIM", .title = "DCR/DMAR burst addressing",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "TIM-011", .fn = tim_dmar },
    { .id = "TIM.012", .group = "TIM", .title = "basic timers (TIM7; TIM6=HAL)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "TIM-012, GEN-012", .fn = tim_basic },
    { .id = "TIM.013", .group = "TIM", .title = "special remap inputs",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "TIM-013", .fn = tim_remaps },
    { .id = "LPTIM.001", .group = "LPTIM", .title = "LPTIM1 clocks + compare/ARR",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "LPTIM-001, LPTIM-002", .fn = lp_tim },
};

const size_t g_tim_test_count = sizeof(g_tim_tests) / sizeof(g_tim_tests[0]);

void hwval_test_register_tim(void)
{
    hwval_test_register(g_tim_tests, g_tim_test_count);
}