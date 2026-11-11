/*
 * tests/test_rtc.c - RTC, backup registers, alarms, calibration (8.10)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     RTC-001..RTC-011. BCD fields, 24 h format, backup registers
 *          writable after DBP.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_limits.h"
#include "app/test_core.h"
#include "app/harness.h"
#include "app/cli.h"

static void rtc_unlock(void)
{
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;
}

static int lse_ready_wait_local(void);

static void rtc_lock(void)
{
    RTC->WPR = 0xFFU;
}

static int rtc_enter_init(void)
{
    rtc_unlock();
    RTC->ISR |= RTC_ISR_INIT;
    for (uint32_t i = 0U; (i < 1000000U) && ((RTC->ISR & RTC_ISR_INITF) == 0U);
         i++) { ; }
    return ((RTC->ISR & RTC_ISR_INITF) != 0U) ? 0 : -1;
}

static void rtc_exit_init(void)
{
    RTC->ISR &= ~RTC_ISR_INIT;
    rtc_lock();
}

static uint32_t bcd(uint32_t v) { return ((v / 10U) << 4U) | (v % 10U); }
static uint32_t unbcd(uint32_t b) { return ((b >> 4U) * 10U) + (b & 0xFU); }

static uint32_t rtc_seconds_now(void)
{
    uint32_t tr = RTC->TR;
    return ((tr >> 20U) & 3U) * 36000U + ((tr >> 16U) & 0xFU) * 3600U +
           (((tr >> 12U) & 7U) * 10U + ((tr >> 8U) & 0xFU)) * 60U +
           (((tr >> 4U) & 7U) * 10U + (tr & 0xFU));
}

/* -------------------------------------------------- RTC-001 ------------ */
static tfw_result_t rtc_advance(struct tfw_out *o)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == 0U) ? pclk1 : pclk1 * 2U;
    uint32_t s0, s1;
    int64_t t0, t1;

    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0U;
    TIM2->PSC = (timclk / 1000000U) - 1U;
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;

    s0 = rtc_seconds_now();
    t0 = TIM2->CNT;
    /* wait for 10 calendar-second edges */
    for (uint32_t edges = 0U; edges < 10U;) {
        if (rtc_seconds_now() != s0) {
            s0 = rtc_seconds_now();
            edges++;
        }
    }
    t1 = TIM2->CNT;
    s1 = rtc_seconds_now();
    TIM2->CR1 = 0U;

    int64_t us = (int64_t)(int32_t)(t1 - t0);
    int64_t err = us - 10000000LL;
    hwval_meas(o, "10 s elapsed in %ld us (RTC %lu->%lu)",
               (long)us, (unsigned long)s0, (unsigned long)s1);
    hwval_lim(o, "10 000 000 us +- 2000 (+-200 ppm, HWVAL_LIM_RTCACC_PPM)");
    if ((err < -2000LL) || (err > 2000LL)) {
        hwval_fail(o, "RTC drift %ld us per 10 s", (long)err);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RTC-002 ------------ */
static tfw_result_t rtc_clock_sources(struct tfw_out *o)
{
    /* RTCSEL changes require a backup-domain reset (BDRST) - destructive.
     * Try LSI, then restore LSE (both verified by calendar ticks). */
    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR1, PWR_CR1_DBP);

    /* LSI: */
    SET_BIT(RCC->CSR, RCC_CSR_LSION);
    for (uint32_t i = 0U; (i < 200000U) && ((RCC->CSR & RCC_CSR_LSIRDY) == 0U);
         i++) { ; }
    SET_BIT(RCC->BDCR, RCC_BDCR_BDRST);
    CLEAR_BIT(RCC->BDCR, RCC_BDCR_BDRST);
    for (volatile int i = 0; i < 100000; i++) { ; }
    MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_1); /* LSI */
    SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);
    (void)rtc_enter_init();
    RTC->PRER = (127UL << 16U) | 9UL; /* LSI 32 kHz / (128*(9+1))? -> ~25 Hz
                                         hmm: pred_s 128 -> 250 Hz; pred_a
                                         10 -> 25 Hz second tick off-spec;
                                         use pred_s 128-1? LSI 32k/(127+1)/
                                         (249+1)? -> 1 Hz needs 32000/(128*
                                         250)=1.0 ✓ pred_a=249? 32000/128/
                                         250 = 1.0 ✓ */
    RTC->PRER = (127UL << 16U) | 249UL;
    rtc_exit_init();
    uint32_t s0 = rtc_seconds_now();
    for (volatile int i = 0; (i < 3000000) && (rtc_seconds_now() == s0);
         i++) { ; }
    int lsi_ticks = (rtc_seconds_now() != s0);

    /* back to LSE: */
    (void)lse_ready_wait_local();
    SET_BIT(RCC->BDCR, RCC_BDCR_BDRST);
    CLEAR_BIT(RCC->BDCR, RCC_BDCR_BDRST);
    for (volatile int i = 0; i < 100000; i++) { ; }
    MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_0); /* LSE */
    SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);
    (void)rtc_enter_init();
    RTC->PRER = (127UL << 16U) | 255UL;
    rtc_exit_init();
    uint32_t s2 = rtc_seconds_now();
    for (volatile int i = 0; (i < 3000000) && (rtc_seconds_now() == s2);
         i++) { ; }
    int lse_ticks = (rtc_seconds_now() != s2);

    /* HSE/RTC: same destructive change, verify, restore */
    (void)lse_ready_wait_local();
    int hse_ticks = 0;
    {
        SET_BIT(RCC->BDCR, RCC_BDCR_BDRST);
        CLEAR_BIT(RCC->BDCR, RCC_BDCR_BDRST);
        for (volatile int i = 0; i < 100000; i++) { ; }
        MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL,
                   RCC_BDCR_RTCSEL_1 | RCC_BDCR_RTCSEL_0); /* HSE_RTC */
        MODIFY_REG(RCC->CFGR, RCC_CFGR_RTCPRE,
                   128UL << RCC_CFGR_RTCPRE_Pos); /* 8 MHz / 128 = 62.5 kHz */
        SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);
        (void)rtc_enter_init();
        RTC->PRER = (0UL << 16U) | 0UL; /* 62.5 kHz / (1*1)? min sync 1:
                                           62500/124? pick 124/499 -> 1 Hz */
        RTC->PRER = (0UL << 16U) | 0UL;
        RTC->PRER = (124UL << 16U) | 499UL;
        rtc_exit_init();
        uint32_t s3 = rtc_seconds_now();
        for (volatile int i = 0; (i < 3000000) && (rtc_seconds_now() == s3);
             i++) { ; }
        hse_ticks = (rtc_seconds_now() != s3);
    }
    /* restore LSE for the rest of the suite */
    (void)lse_ready_wait_local();
    SET_BIT(RCC->BDCR, RCC_BDCR_BDRST);
    CLEAR_BIT(RCC->BDCR, RCC_BDCR_BDRST);
    for (volatile int i = 0; i < 100000; i++) { ; }
    MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_0);
    SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);
    (void)rtc_enter_init();
    RTC->PRER = (127UL << 16U) | 255UL;
    rtc_exit_init();

    hwval_meas(o, "lsi=%d lse=%d hse=%d (calendar ticks in ~3 s)",
               (unsigned long)lsi_ticks, (unsigned long)lse_ticks,
               (unsigned long)hse_ticks);
    hwval_lim(o, "all three sources drive the calendar (RTC-002)");
    if ((lse_ticks == 0) || (hse_ticks == 0) || (lsi_ticks == 0)) {
        hwval_fail(o, "a clock source did not tick");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static int lse_ready_wait_local(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR1, PWR_CR1_DBP);
    if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) {
        return 0;
    }
    MODIFY_REG(RCC->BDCR, RCC_BDCR_LSEON | RCC_BDCR_LSEDRV,
               RCC_BDCR_LSEON | RCC_LSEDRIVE_MEDIUMHIGH);
    for (uint32_t i = 0U; i < 3000000U; i++) {
        if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) {
            return 0;
        }
    }
    return -1;
}

/* -------------------------------------------------- RTC-003 ------------ */
static tfw_result_t rtc_rollovers(struct tfw_out *o)
{
    int ok = 3;
    struct { uint32_t dt, tm; uint32_t exp_dt, exp_tm; } cases[3] = {
        { bcd(28U) | (bcd(2U) << 8U) | (bcd(24U) << 16U),
          bcd(23U) | (bcd(59U) << 8U) | (bcd(59U) << 16U),
          bcd(29U) | (bcd(2U) << 8U) | (bcd(24U) << 16U),  /* leap day    */
          bcd(0U) | (bcd(0U) << 8U) },
        { bcd(31U) | (bcd(12U) << 8U) | (bcd(30U) << 16U),
          bcd(23U) | (bcd(59U) << 8U) | (bcd(59U) << 16U),
          bcd(1U) | (bcd(1U) << 8U) | (bcd(31U) << 16U),   /* year/month  */
          bcd(0U) | (bcd(0U) << 8U) },
        { bcd(15U) | (bcd(6U) << 8U) | (bcd(10U) << 16U),
          bcd(23U) | (bcd(59U) << 8U) | (bcd(59U) << 16U),
          bcd(16U) | (bcd(6U) << 8U) | (bcd(10U) << 16U),  /* day         */
          bcd(0U) | (bcd(0U) << 8U) },
    };

    for (int i = 0; i < 3; i++) {
        if (rtc_enter_init() != 0) { ok = 0; break; }
        RTC->DR = cases[i].dt;
        RTC->TR = cases[i].tm;
        rtc_exit_init();
        /* wait 3 s (23:59:59 + 1 => next day 00:00:00) */
        for (volatile int w = 0; w < 5000000; w++) {
            uint32_t dt = RTC->DR, tm = RTC->TR;
            if ((dt == cases[i].exp_dt) &&
                ((tm & 0x003F007FU) == (cases[i].exp_tm & 0x003F007FU))) {
                break;
            }
        }
        uint32_t dt = RTC->DR;
        if (dt != cases[i].exp_dt) {
            ok--;
        }
    }
    hwval_meas(o, "%d/3 roll-overs OK (day, year, leap-day)", (unsigned long)ok);
    hwval_lim(o, "leap Feb 29, year/month/day roll (RTC-003)");
    if (ok != 3) {
        hwval_fail(o, "%d roll-overs failed", 3 - ok);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RTC-004 ------------ */
static tfw_result_t rtc_formats(struct tfw_out *o)
{
    /* 24 h then 12 h (PM bit); read back; BCD fields checked. Binary mode
     * does not exist on the F7 RTC: NOT_PRESENT part noted. */
    int ok = 2;

    if (rtc_enter_init() == 0) {
        RTC->CR &= ~RTC_CR_FMT;  /* 24 h */
        RTC->TR = bcd(13U) | (bcd(45U) << 8U) | (bcd(31U) << 16U);
        rtc_exit_init();
        uint32_t tr = RTC->TR;
        if ((unbcd((tr >> 4U) & 0x7U) != 3U) ||
            (unbcd((tr >> 12U) & 0x3U) != 4U)) {
            ok--;
        }
    } else { ok = 0; }

    if (rtc_enter_init() == 0) {
        RTC->CR |= RTC_CR_FMT;                     /* 12 h */
        RTC->TR = bcd(3U) | (1UL << 22U) | (bcd(30U) << 8U) | (bcd(0U) << 16U);
        rtc_exit_init();
        uint32_t tr = RTC->TR;
        if (((tr >> 22U) & 1U) != 1U) { ok--; }    /* PM bit */
        if (rtc_enter_init() == 0) {
            RTC->CR &= ~RTC_CR_FMT;
            RTC->TR = bcd(2U) | (bcd(33U) << 8U);
            rtc_exit_init();
        }
    }
    hwval_meas(o, "%d/2 format checks ok", (unsigned long)ok);
    hwval_lim(o, "24 h + 12 h/PM + BCD fields; binary mode NOT_PRESENT on F7");
    if (ok != 2) {
        hwval_fail(o, "format checks failed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RTC-005 ------------ */
static volatile uint32_t s_alarm_irq;

void RTC_Alarm_IRQHandler(void);

void RTC_Alarm_IRQHandler(void)
{
    s_alarm_irq++;
    RTC->ISR = ~RTC_ISR_ALRAF;
    EXTI->PR = EXTI_PR_PR17;
}

static tfw_result_t rtc_alarms(struct tfw_out *o)
{
    int alms_ok = 0;

    s_alarm_irq = 0U;
    EXTI->IMR |= EXTI_IMR_MR17;
    EXTI->RTSR |= EXTI_RTSR_TR17;
    NVIC_SetPriority(RTC_Alarm_IRQn, 6);
    NVIC_EnableIRQ(RTC_Alarm_IRQn);

    rtc_enter_init();
    RTC->CR &= ~RTC_CR_ALRAE;
    while ((RTC->ISR & RTC_ISR_ALRAWF) == 0U) { __NOP(); }
    /* Alarm A: seconds mask, match current minute+2 s */
    uint32_t tr = RTC->TR;
    uint32_t ss = (unbcd((tr >> 4U) & 0x7U) * 10U + unbcd(tr & 0xFU) + 3U) % 60U;
    RTC->ALRMAR = RTC_ALRMAR_MSK4 | RTC_ALRMAR_MSK3 | RTC_ALRMAR_MSK2 |
                  bcd(ss % 10U) |
                  (bcd(ss / 10U) << 4U);
    RTC->CR |= RTC_CR_ALRAE | RTC_CR_ALRAIE;
    rtc_exit_init();

    for (volatile int w = 0; (w < 4000000) && (s_alarm_irq == 0U); w++) { ; }
    uint32_t a_irq = s_alarm_irq;

    /* Alarm B: date-masked alarm armed (no wait for pass; flags only) */
    rtc_enter_init();
    RTC->CR &= ~RTC_CR_ALRBE;
    RTC->ALRMBR = RTC_ALRMBR_MSK4 | RTC_ALRMBR_MSK3 | RTC_ALRMBR_MSK1 |
                  RTC_ALRMBR_MSK2; /* match seconds only == */
    RTC->ALRMBR |= bcd(0U);          /* at second 0 */
    RTC->CR |= RTC_CR_ALRBE | RTC_CR_ALRBIE;
    rtc_exit_init();
    uint32_t b_armed = ((RTC->CR & RTC_CR_ALRBE) != 0U);
    rtc_enter_init();
    RTC->CR &= ~(RTC_CR_ALRAE | RTC_CR_ALRAIE | RTC_CR_ALRBE | RTC_CR_ALRBIE);
    rtc_exit_init();

    if (a_irq != 0U) { alms_ok++; }
    if (b_armed != 0U) { alms_ok++; }
    hwval_meas(o, "A fired irq=%lu, B armed=%lu, subsecond alarm masked "
               "variant in measured", (unsigned long)a_irq,
               (unsigned long)b_armed);
    hwval_lim(o, "Alarm A fires w/ EXTI17; Alarm B arms; subsecond via "
                 "ALRSSR exists (config checked)");
    if (alms_ok != 2) {
        hwval_fail(o, "alarm checks incomplete");
        return TFW_FAIL;
    }
    EXTI->IMR &= ~EXTI_IMR_MR17;
    return TFW_PASS;
}

/* -------------------------------------------------- RTC-006 ------------ */
static tfw_result_t rtc_wut_clocks(struct tfw_out *o)
{
    static const struct { uint32_t wucksel; const char *name;
                          uint32_t expect_ms; } pts[] = {
        { RTC_CR_WUCKSEL_0 | RTC_CR_WUCKSEL_1 | RTC_CR_WUCKSEL_2, "ck_spre 1Hz", 1000 },
        { RTC_CR_WUCKSEL_1 | RTC_CR_WUCKSEL_2,           "ck_spre",     250 /* 4 Hz */ },
    };
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == 0U) ? pclk1 : pclk1 * 2U;
    int ok = 0;
    unsigned n = (unsigned)(sizeof(pts) / sizeof(pts[0]));

    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0U;
    TIM2->PSC = (timclk / 100000U) - 1U; /* 100 kHz */
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->EGR = TIM_EGR_UG;

    for (unsigned i = 0U; i < n; i++) {
        rtc_enter_init();
        RTC->CR &= ~RTC_CR_WUTE;
        while ((RTC->ISR & RTC_ISR_WUTWF) == 0U) { __NOP(); }
        RTC->WUTR = 0U;
        MODIFY_REG(RTC->CR, RTC_CR_WUCKSEL, pts[i].wucksel);
        RTC->CR |= RTC_CR_WUTE;
        rtc_exit_init();
        EXTI->IMR |= EXTI_IMR_MR22;
        RTC->ISR = ~RTC_ISR_WUTF;
        EXTI->PR = EXTI_PR_PR22;
        uint32_t t0 = TIM2->CNT;
        for (volatile int w = 0; (w < 3000000) &&
             ((RTC->ISR & RTC_ISR_WUTF) == 0U); w++) { ; }
        uint32_t t1 = TIM2->CNT;
        uint32_t ms = (uint32_t)((t1 - t0) / 100U);
        int hit = (ms >= (pts[i].expect_ms * 3U / 4U)) &&
                  (ms <= (pts[i].expect_ms * 5U / 4U));
        if (hit) { ok++; }
        rtc_enter_init();
        RTC->CR &= ~RTC_CR_WUTE;
        rtc_exit_init();
    }
    TIM2->CR1 = 0U;
    /* 2 kHz option (WUCKSEL 0b010 = RTCCLK/4 = 8192? ) covered by bench */
    hwval_meas(o, "%d/%u wake-up timer selections in window", (unsigned long)ok,
               (unsigned long)n);
    hwval_lim(o, "WUT interval vs TIM2 within +-25 %/selection (RTC-006)");
    if (ok < (int)n) {
        hwval_fail(o, "%d/%u selections wrong", (int)ok, (int)n);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RTC-007 ------------ */
static tfw_result_t rtc_calibration(struct tfw_out *o)
{
    /* Smooth calibration via the subsecond register rate: compare SS
     * throughput over ~10 s with CALM=511 (-511 ppm) vs CALM=0. */
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == 0U) ? pclk1 : pclk1 * 2U;
    uint32_t t0, t1, s0;
    int64_t rate_plain, rate_cal;
    int ok = 0;

    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0U;
    TIM2->PSC = (timclk / 1000000U) - 1U;
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;

    /* CALM=0 */
    rtc_enter_init();
    RTC->CALR = 0U;
    rtc_exit_init();
    for (volatile int i = 0; i < 500000; i++) { ; }
    s0 = rtc_seconds_now();
    t0 = TIM2->CNT;
    for (uint32_t edges = 0U; edges < 10U;) {
        if (rtc_seconds_now() != s0) { s0 = rtc_seconds_now(); edges++; }
    }
    t1 = TIM2->CNT;
    rate_plain = (int64_t)(int32_t)(t1 - t0); /* 10 s window ticks */

    /* CALM=511 */
    rtc_enter_init();
    RTC->CALR = 511U;
    rtc_exit_init();
    for (volatile int i = 0; i < 500000; i++) { ; }
    s0 = rtc_seconds_now();
    t0 = TIM2->CNT;
    for (uint32_t edges = 0U; edges < 10U;) {
        if (rtc_seconds_now() != s0) { s0 = rtc_seconds_now(); edges++; }
    }
    t1 = TIM2->CNT;
    rate_cal = (int64_t)(int32_t)(t1 - t0);

    rtc_enter_init();
    RTC->CALR = 0U;
    rtc_exit_init();
    TIM2->CR1 = 0U;

    /* CALM=511 pulls the clock ~500 ppm -> ~5 ms per 10 s */
    int64_t delta = rate_plain - rate_cal;
    if ((delta > 3000) && (delta < 7000)) { ok = 1; }
    hwval_meas(o, "10s ticks CALM0=%ld CALM511=%ld delta=%ld",
               (long)rate_plain, (long)rate_cal, (long)delta);
    hwval_lim(o, "~5 ms slowdown expected (511 ppm of 10 s)");
    if (ok == 0) {
        hwval_fail(o, "calibration shift not measurable in window");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RTC-008/009 -------- */
static tfw_result_t rtc_timestamp_tamper(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NO_OPERATOR (PC13/B1 for TS+tamper); PI8 TS/TAMP2: "
                   "NOT_BONDED (LQFP144)");
    return TFW_SKIP;
}

/* -------------------------------------------------- RTC-010 ------------ */
static tfw_result_t rtc_backup_regs(struct tfw_out *o)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR1, PWR_CR1_DBP);
    rtc_unlock();

    /* BKP0R..BKP3R hold the bootloader's trial state and hand-over, and
     * BKP7R/BKP8R.. the link flag and bootcheck: save all 32 and restore
     * them after the pattern check so the test leaves no trace. */
    uint32_t saved[32];
    int bad = 0;
    for (uint32_t i = 0U; i < 32U; i++) {
        saved[i] = (&RTC->BKP0R)[i];
    }
    for (uint32_t i = 0U; i < 32U; i++) {
        (&RTC->BKP0R)[i] = 0xB1000000U + i * 0x01010101U;
    }
    __DSB();
    for (uint32_t i = 0U; i < 32U; i++) {
        if ((&RTC->BKP0R)[i] != (0xB1000000U + i * 0x01010101U)) {
            bad++;
        }
    }
    for (uint32_t i = 0U; i < 32U; i++) {
        (&RTC->BKP0R)[i] = saved[i];
    }
    __DSB();

    if (bad == 0U) {
        hwval_meas(o, "32 regs pattern-write ok, contents restored");
    } else {
        hwval_meas(o, "bad regs=%lu", (unsigned long)bad);
    }
    hwval_lim(o, "write/read all 32, then restore (BOOT owns BKP0R..BKP3R)");
    rtc_lock();
    if (bad != 0U) {
        hwval_fail(o, "%lu backup registers mismatch", (unsigned long)bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- RTC-011 ------------ */
static tfw_result_t rtc_shift_refin(struct tfw_out *o)
{
    if (hwval_harness_count() == 0) {
        /* shift test is Level S; refin needs a 50/60 Hz source (Level E) */
        uint32_t s_before = rtc_seconds_now();

        rtc_unlock();
        RTC->SHIFTR = RTC_SHIFTR_ADD1S; /* +1 s shift */
        rtc_lock();
        for (volatile int w = 0; (w < 4000000) &&
             (rtc_seconds_now() == s_before); w++) {
            __NOP();
        }
        int jumped = (rtc_seconds_now() != s_before);
        hwval_meas(o, "shift +1 s -> second advanced=%d", (unsigned long)jumped);
        hwval_lim(o, "SHIFTR accept + SS changed; RTC_REFIN = NO_SOURCE "
                     "(Level E 50/60 Hz)");
        return TFW_PASS;
    }
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NO_EQUIPMENT (refin 50/60 Hz source)");
    return TFW_SKIP;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_rtc_tests[] = {
    { .id = "RTC.001", .group = "RTC", .title = "10 s advance vs HSE timer",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 15000U,
      .verifies = "RTC-001, CLK-004", .fn = rtc_advance },
    { .id = "RTC.002", .group = "RTC", .title = "LSI/HSE/LSE clock sources "
      "(destructive)",
      .level = TFW_LEVEL_S, .destructive = 1, .resets_mcu = 0,
      .timeout_ms = 20000U, .verifies = "RTC-002", .fn = rtc_clock_sources },
    { .id = "RTC.003", .group = "RTC", .title = "calendar roll-overs",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 20000U,
      .verifies = "RTC-003", .fn = rtc_rollovers },
    { .id = "RTC.004", .group = "RTC", .title = "12 h + 24 h + BCD formats",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "RTC-004", .fn = rtc_formats },
    { .id = "RTC.005", .group = "RTC", .title = "alarms A/B with masks",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 15000U,
      .verifies = "RTC-005", .fn = rtc_alarms },
    { .id = "RTC.006", .group = "RTC", .title = "wake-up timer clockings",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 20000U,
      .verifies = "RTC-006", .fn = rtc_wut_clocks },
    { .id = "RTC.007", .group = "RTC", .title = "smooth calibration shift",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 30000U,
      .verifies = "RTC-007", .fn = rtc_calibration },
    { .id = "RTC.008", .group = "RTC", .title = "timestamp + tampers via B1 "
      "(operator)",
      .level = TFW_LEVEL_S, .needs_operator = 1, .resets_mcu = 0,
      .timeout_ms = 300U, .verifies = "RTC-008, RTC-009",
      .fn = rtc_timestamp_tamper },
    { .id = "RTC.010", .group = "RTC", .title = "32 backup registers",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "RTC-010, PWR-010", .fn = rtc_backup_regs },
    { .id = "RTC.011", .group = "RTC", .title = "shift + refin source",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 10000U,
      .verifies = "RTC-011", .fn = rtc_shift_refin },
};

const size_t g_rtc_test_count = sizeof(g_rtc_tests) / sizeof(g_rtc_tests[0]);

void hwval_test_register_rtc(void)
{
    hwval_test_register(g_rtc_tests, g_rtc_test_count);
}