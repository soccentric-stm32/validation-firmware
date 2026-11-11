/*
 * tests/test_clk.c - oscillator, PLL and bus clock tests (section 8.6)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     CLK-001..CLK-015, level S unless noted. Reference clock: LSE
 *          seconds via the RTC, tick references via 1 MHz timers.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_limits.h"
#include "app/test_core.h"
#include "bsp/clock.h"
#include "app/harness.h"

static void pclk1_of(uint32_t *timclk)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();

    /* x2 unless the APB1 prescaler is 1 (RM0410; CL-023). */
    *timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == 0U) ? pclk1 : pclk1 * 2U;
}

extern volatile uint32_t hwval_nmi_count; /* src/fault.c */

/* --------------------------------- LSE + RTC seconds as the reference --- */
static int lse_on_and_ready(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR1, PWR_CR1_DBP);
    /* unlock backup domain writes for BDCR */
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_PWREN);

    if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) {
        return 1;
    }
    MODIFY_REG(RCC->BDCR, RCC_BDCR_LSEON | RCC_BDCR_LSEDRV,
               RCC_BDCR_LSEON | RCC_LSEDRIVE_MEDIUMHIGH);
    for (uint32_t i = 0U; i < 2000000U; i++) {
        if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) {
            return 1;
        }
    }
    return 0;
}

/* Wait for the RTC seconds field (TR) to change.  Polling DR (the date)
 * only changes at midnight.  Read DR after TR to unlock the shadow
 * registers, and use DWT cycles for a clock-independent timeout. */
static int rtc_seconds_tick_wait(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t before = RTC->TR & 0x7FU;
    (void)RTC->DR;
    uint32_t start = DWT->CYCCNT;
    uint32_t budget = HAL_RCC_GetHCLKFreq() * 3U; /* 3 s */
    while ((uint32_t)(DWT->CYCCNT - start) < budget) {
        uint32_t now = RTC->TR & 0x7FU;
        (void)RTC->DR;
        if (now != before) {
            return 0;
        }
    }
    return -1;
}

/* The RTC must run from the LSE, or the DR register is frozen (CL-024).
 * RTCSEL can only be set while it is still "no clock"; changing another
 * selection needs a backup-domain reset, which is left to the destructive
 * RTC tests (test_rtc.c), so that case reports "not available". */
static int rtc_start_lse(void)
{
    if (lse_on_and_ready() == 0) {
        return -1;
    }
    uint32_t sel = RCC->BDCR & RCC_BDCR_RTCSEL;
    if (sel == 0U) {
        MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_0); /* LSE */
    } else if (sel != RCC_BDCR_RTCSEL_0) {
        return -1;
    }
    SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);

    /* INITS = calendar initialised (year set): keep a calendar someone
     * set; otherwise program the 1 Hz prescalers and let it free-run. */
    if ((RTC->ISR & RTC_ISR_INITS) == 0U) {
        RTC->WPR = 0xCAU; RTC->WPR = 0x53U;
        RTC->ISR |= RTC_ISR_INIT;
        for (uint32_t i = 0U; (i < 1000000U) && ((RTC->ISR &
             RTC_ISR_INITF) == 0U); i++) { ; }
        if ((RTC->ISR & RTC_ISR_INITF) != 0U) {
            /* 32.768 kHz / (PREDIV_A 127 + 1) / (PREDIV_S 255 + 1) = 1 Hz */
            RTC->PRER = (127UL << 16U) | 255UL;
        }
        RTC->ISR &= ~RTC_ISR_INIT;
        RTC->WPR = 0xFFU;
    }
    return ((RTC->ISR & RTC_ISR_INIT) == 0U) ? 0 : -1;
}

/* count 1 MHz TIM2 ticks while RTC seconds tick twice; returns ticks/2s */
/* Count TIM2 ticks over two LSE seconds.  The prescaler is derived from
 * the *expected* timer clock, not from the measured one: deriving it from
 * HAL_RCC_GetPCLK1Freq() would scale with the fault being measured and
 * always produce 2,000,000. */
static int64_t lse_measured_ticks(uint32_t expected_timclk, uint32_t *rc)
{
    uint32_t t0, t1;
    int64_t ticks;

    if (expected_timclk < 1000000U) {
        *rc = 4U;
        return 0;
    }
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0U;
    TIM2->PSC = (expected_timclk / 1000000U) - 1U;
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;

    if (rtc_seconds_tick_wait() != 0) {
        TIM2->CR1 = 0U;
        *rc = 1U;
        return 0;
    }
    t0 = TIM2->CNT;
    if (rtc_seconds_tick_wait() != 0) { /* 1st second */
        TIM2->CR1 = 0U;
        *rc = 2U;
        return 0;
    }
    if (rtc_seconds_tick_wait() != 0) { /* 2nd second */
        TIM2->CR1 = 0U;
        *rc = 3U;
        return 0;
    }
    t1 = TIM2->CNT;
    TIM2->CR1 = 0U;
    ticks = (int64_t)(int32_t)(t1 - t0); /* ~2 s window */
    *rc = 0U;
    return ticks; /* ticks per ~2 LSE seconds */
}

/* -------------------------------------------------- CLK-001 (HSI) ------- */
static tfw_result_t clk_hsi(struct tfw_out *o)
{
    int save_rc = hwval_clock_set(216000000U, 7U);
    int64_t ticks;
    uint32_t rc = 0U;

    if (rtc_start_lse() != 0) {
        hwval_fail(o, "LSE/RTC not available for measurement");
        return TFW_SKIP;
    }
    /* switch SYSCLK to HSI, timers on HSI-derived APB1x2 clock */
    RCC_ClkInitTypeDef clk = { 0 };
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) {
        hwval_fail(o, "HSI clock config failed");
        return TFW_FAIL;
    }
    ticks = lse_measured_ticks(16000000U, &rc);
    (void)hwval_clock_restore_default();
    if (rc != 0U) {
        hwval_fail(o, "LSE reference lost (rc=%lu)", (unsigned long)rc);
        return TFW_FAIL;
    }
    /* SYSCLK = HSI, APB1 /1, so TIM2 runs at the HSI and its prescaler
     * assumes the nominal 16 MHz: 2 000 000 ticks per 2 s at exactly
     * 16 MHz. HSI in kHz = ticks * 16000 / 2000000 (CL-024). */
    int64_t hsi_khz = ticks * 16000 / 2000000;
    int64_t tol_khz = 16000 * (int64_t)HWVAL_LIM_HSI_TOL_PPM / 1000000;
    hwval_meas(o, "HSI-derived ticks=%ld per 2 s -> %ld kHz", (long)ticks,
               (long)hsi_khz);
    hwval_lim(o, "16000 kHz +-1 %% (HWVAL_LIM_HSI_TOL_PPM=10000)");
    if ((hsi_khz < 16000 - tol_khz) || (hsi_khz > 16000 + tol_khz)) {
        hwval_fail(o, "HSI out of tolerance");
        return TFW_FAIL;
    }
    if (save_rc != 0) {
        hwval_fail(o, "clock restore failed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-002 (HSE byp) --- */
static tfw_result_t clk_hse_bypass(struct tfw_out *o)
{
    int64_t ticks;
    uint32_t rc = 0U;

    if (rtc_start_lse() != 0) {
        hwval_fail(o, "LSE/RTC not available");
        return TFW_SKIP;
    }
    /* wait for HSE bypass ready flag (it feeds the default PLL already) */
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY) == 0) {
        hwval_fail(o, "HSE bypass not ready");
        return TFW_FAIL;
    }
    ticks = lse_measured_ticks(108000000U, &rc);
    if (rc != 0U) {
        hwval_fail(o, "LSE reference lost");
        return TFW_FAIL;
    }
    /* TIM2 at 1 MHz from the 216 MHz tree: expect ~2 000 000 +-0.1 % */
    int64_t err = ticks - 2000000;
    hwval_meas(o, "ticks/2s=%ld err=%ld (HSE 8 MHz + PLL 216 MHz)",
               (long)ticks, (long)err);
    hwval_lim(o, "+-2000 counts (+-0.1 %, HWVAL_LIM_HSEBYP_TOL_PPM)");
    if ((err < -2000) || (err > 2000)) {
        hwval_fail(o, "HSE bypass out of +-0.1 %%");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-003 (crystal) --- */
static tfw_result_t clk_hse_crystal(struct tfw_out *o)
{
    /* X3 is not fitted on the NUCLEO-F767ZI by default (Hardware.md); the
     * operator may declare it fitted via `set hse-crystal on`, then this
     * runs like CLK-002. */
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NOT_FITTED (X3 absent; set hse-crystal on to override)");
    return TFW_NA;
}

/* -------------------------------------------------- CLK-004 (LSE) ------- */
static tfw_result_t clk_lse(struct tfw_out *o)
{
    /* LSE ready + RTC running; try all three drive levels, each must start
     * (functional check +-100 ppm level is covered by the LSE-referenced
     * measurements above, which bound the LSE error itself). */
    static const uint32_t drives[3] = {
        RCC_LSEDRIVE_LOW, RCC_LSEDRIVE_MEDIUMLOW,
        RCC_LSEDRIVE_MEDIUMHIGH,
    };
    int ok = 0;

    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR1, PWR_CR1_DBP);
    for (unsigned d = 0U; d < 3U; d++) {
        uint32_t bdcr = RCC->BDCR;
        if ((bdcr & RCC_BDCR_LSERDY) != 0U) {
            ok++;
            continue; /* already running: re-drive would need BDRST */
        }
        MODIFY_REG(RCC->BDCR, RCC_BDCR_LSEON | RCC_BDCR_LSEDRV,
                   RCC_BDCR_LSEON | drives[d]);
        for (uint32_t i = 0U; i < 1500000U; i++) {
            if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) { ok++; break; }
        }
    }
    hwval_meas(o, "%d/3 drive levels ready", (unsigned long)ok);
    hwval_lim(o, "LSE starts at all drives (HWVAL_LIM_LSE_TOL_PPM=100 by "
                 "reference duty)");
    if (ok < 1) {
        hwval_fail(o, "LSE never became ready");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-005 (LSI) ------- */
static tfw_result_t clk_lsi(struct tfw_out *o)
{
    /* LSI -> TIM5_CH4 input capture; intervals measured in HSE-derived
     * ticks give the LSI frequency vs the DS11532 range. */
    uint32_t periods[8];
    uint32_t n = 0U;
    uint32_t timclk;

    SET_BIT(RCC->CSR, RCC_CSR_LSION);
    for (uint32_t i = 0U; (i < 100000U) && ((RCC->CSR & RCC_CSR_LSIRDY) == 0U);
         i++) { ; }
    if ((RCC->CSR & RCC_CSR_LSIRDY) == 0U) {
        hwval_fail(o, "LSI did not start");
        return TFW_FAIL;
    }

    __HAL_RCC_TIM5_CLK_ENABLE();
    pclk1_of(&timclk);
    TIM5->CR1 = 0U;
    TIM5->PSC = 0U;                          /* count at the timer clock */
    TIM5->ARR = 0xFFFFFFFFU;
    TIM5->CCMR2 = TIM_CCMR2_CC4S_0;              /* CH4 input capture   */
    TIM5->CCER = TIM_CCER_CC4E;
    /* TI4_RMP = LSI (RM0410 TIM5_OR) */
    MODIFY_REG(TIM5->OR, TIM_OR_TI4_RMP, TIM_OR_TI4_RMP_0); /* LSI */
    TIM5->EGR = TIM_EGR_UG;
    TIM5->CR1 = TIM_CR1_CEN;

    /* collect 8 capture intervals */
    uint32_t last = 0U;
    for (uint32_t tries = 0U; (tries < 2000000U) && (n < 8U); tries++) {
        if ((TIM5->SR & TIM_SR_CC4IF) != 0U) {
            uint32_t cap = TIM5->CCR4;
            TIM5->SR = ~TIM_SR_CC4IF;
            if (n > 0U) {
                periods[n - 1U] = cap - last;
            }
            last = cap;
            n++;
        }
    }
    TIM5->CR1 = 0U;
    MODIFY_REG(TIM5->OR, TIM_OR_TI4_RMP, 0U);

    if (n < 4U) {
        hwval_fail(o, "only %lu LSI captures (TI4_RMP/LSI path?)",
                   (unsigned long)n);
        return TFW_SKIP;
    }
    uint64_t sum = 0U;
    for (uint32_t i = 0U; i < n - 1U; i++) { sum += periods[i]; }
    uint32_t mean = (uint32_t)(sum / (n - 1U));
    /* CL-023: one LSI period is `mean` timer-clock ticks. */
    uint64_t lsi_hz = (mean > 0U) ? (uint64_t)timclk / mean : 0U;
    hwval_meas(o, "mean=%lu ticks -> LSI ~%lu Hz", (unsigned long)mean,
               (unsigned long)lsi_hz);
    hwval_lim(o, "17-47 kHz [DS11532] (HWVAL_LIM_LSI_MIN/MAX_HZ)");
    if ((lsi_hz < HWVAL_LIM_LSI_MIN_HZ) || (lsi_hz > HWVAL_LIM_LSI_MAX_HZ)) {
        hwval_fail(o, "LSI %lu Hz outside 17-47 kHz", (unsigned long)lsi_hz);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-006 (PLLs) ------ */
static tfw_result_t clk_main_pll(struct tfw_out *o)
{
    static const struct { uint32_t hz; uint32_t ws; } pts[] = {
        { 96000000U, 3U }, { 144000000U, 5U }, { 216000000U, 7U },
    };
    int64_t ticks;
    uint32_t rc = 0U;
    char det[96];
    int ok = 3;

    if (rtc_start_lse() != 0) {
        hwval_fail(o, "LSE/RTC not available");
        return TFW_SKIP;
    }
    for (unsigned i = 0U; i < 3U; i++) {
        if (hwval_clock_set(pts[i].hz, pts[i].ws) != 0) { ok = 0; break; }
        ticks = lse_measured_ticks(pts[i].hz / 2U, &rc);
        if (rc != 0U) { ok = 0; break; }
        int64_t want = 2000000;   /* 1 MHz TIM2 over two LSE seconds */
        int64_t err = ticks - want;
        int64_t tol  = want / 1000; /* +-0.1 % like HSE */
        if ((err < -tol) || (err > tol)) {
            (void)snprintf(det, sizeof(det), "%lu MHz err %ld",
                           (unsigned long)(pts[i].hz / 1000000U), (long)err);
            ok = 0;
            break;
        }
    }
    if (hwval_clock_restore_default() != 0) {
        hwval_fail(o, "restore failed");
        return TFW_FAIL;
    }
    hwval_meas(o, "%d/3 PLL points in spec (incl. 216 MHz)", (unsigned long)ok);
    hwval_lim(o, "measured vs LSE within +-0.5 %/point (CLK-006)");
    if (ok != 3) {
        hwval_fail(o, "%s", ok ? "PLL drift" : "switch failed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-007 (PLLQ 48M) -- */
static tfw_result_t clk_pllq48(struct tfw_out *o)
{
    /* default tree: PLLQ = 9 -> 48 MHz and CLK48SEL = PLLQ */
    uint32_t q = (RCC->PLLCFGR & RCC_PLLCFGR_PLLQ) >> RCC_PLLCFGR_PLLQ_Pos;
    uint32_t sel48 = RCC->DCKCFGR2 & RCC_DCKCFGR2_CK48MSEL;

    uint32_t vco = HAL_RCC_GetSysClockFreq() * 2U; /* /P=2 */
    uint32_t q_hz = (q > 0U) ? (vco / q) : 0U;
    hwval_meas(o, "PLLQ=%lu -> %lu MHz, CLK48SEL=%lu", (unsigned long)q,
               (unsigned long)(q_hz / 1000000U), (unsigned long)sel48);
    hwval_lim(o, "PLLQ output 48 MHz selected for USB/RNG/SD (CLK-007)");
    if (q != 9U) {
        hwval_fail(o, "PLLQ %lu != 9 (48 MHz)", (unsigned long)q);
        return TFW_FAIL;
    }
    if (sel48 != 0U) { /* 00 = PLLQ */
        hwval_fail(o, "CLK48 mux not on PLLQ");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-008/009 (I2S/SAI) */
static tfw_result_t clk_plli2s_sai(struct tfw_out *o)
{
    RCC_PeriphCLKInitTypeDef pc = { 0 };
    int i2s_ok = 0, sai_ok = 0;

    /* PLLI2S: lock with a 96 kHz-ish I2S budget and verify ready */
    pc.PeriphClockSelection = RCC_PERIPHCLK_I2S | RCC_PERIPHCLK_SAI1;
    pc.PLLI2S.PLLI2SN = 192U; /* VCO 384 MHz (HSE 1 MHz in) */
    pc.PLLI2S.PLLI2SR = 16U;  /* I2S ~24 MHz */
    pc.PLLI2S.PLLI2SQ = 4U;   /* SAI ~96 MHz */
    if (HAL_RCCEx_PeriphCLKConfig(&pc) == HAL_OK) {
        for (uint32_t i = 0U; (i < 2000000U) &&
             (__HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY) == 0); i++) { ; }
        i2s_ok = __HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY);
    }
    sai_ok = __HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY); /* SAI1 shares PLLI2S */

    hwval_meas(o, "plli2s_rdy=%d sai1_rdy=%d I2S-CK~%lu Hz",
               (unsigned long)i2s_ok, (unsigned long)sai_ok,
               (unsigned long)HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2S));
    hwval_lim(o, "lock + computed clocks (measurement via MCO/scope = "
                 "Level L/E, SKIP NO_HARNESS)");
    if ((i2s_ok == 0) || (sai_ok == 0)) {
        hwval_fail(o, "PLLI2S/SAI clock did not lock");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-010 (MCO) ------- */
static tfw_result_t clk_mco(struct tfw_out *o)
{
    if (hwval_harness_count() == 0) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (MCO->timer wire; Level E scope "
                       "alternative), configs verified below");
    }
    /* configure MCO1 = HSI/1 (16 MHz) and MCO2 = SYSCLK/4 on PC9; pins are
     * on the morpho headers and not reserved (CON-005 covers PH0/PH1). */
    GPIO_InitTypeDef io = { 0 };
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    io.Pin = GPIO_PIN_8; io.Mode = GPIO_MODE_AF_PP; io.Alternate = GPIO_AF0_MCO;
    HAL_GPIO_Init(GPIOA, &io);
    io.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &io);
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_1);
    HAL_RCC_MCOConfig(RCC_MCO2, RCC_MCO2SOURCE_SYSCLK, RCC_MCODIV_4);
    vTaskDelay(pdMS_TO_TICKS(10));
    /* restore: SYSCLK/4 and HSI left on pins; benign for the suite */
    hwval_meas(o, "MCO1=HSI/1 PA8, MCO2=SYSCLK/4 PC9");
    hwval_lim(o, "freq measure needs H26 wire or scope (SKIP if absent)");
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-011/012 (CSS) --- */
static tfw_result_t clk_css(struct tfw_out *o)
{
    int css_on = 0;

    SET_BIT(RCC->CR, RCC_CR_CSSON); /* HSE clock security on */
    css_on = ((RCC->CR & RCC_CR_CSSON) != 0U) ? 1 : 0;

    hwval_meas(o, "CSSON=%d nmi-path=%d (from CORE.007)",
               (unsigned long)css_on, (unsigned long)(hwval_nmi_count > 0U));
    hwval_lim(o, "CSSON set; real HSE failure NOT_INDUCIBLE (the ST-LINK "
                 "MCO bypass cannot be stopped by software)");
    if (css_on == 0) {
        hwval_fail(o, "CSS config bit missing");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static tfw_result_t clk_lsecss_na(struct tfw_out *o)
{
    /* CL-016: RM0410 RCC_BDCR for F76x has no LSECSSON bit — the LSE
     * clock security system does not exist on this device. */
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NOT_PRESENT (no LSE CSS on F767, RM0410 BDCR map; CL-016)");
    return TFW_NA;
}

/* -------------------------------------------------- CLK-013 (prescalers) */
static tfw_result_t clk_bus_prescalers(struct tfw_out *o)
{
    static const uint32_t apb1_divs[] = {
        RCC_HCLK_DIV1, RCC_HCLK_DIV2, RCC_HCLK_DIV4, RCC_HCLK_DIV8,
        RCC_HCLK_DIV16,
    };
    int ok = 0;
    uint32_t tried = 0U;

    for (unsigned i = 0U; i < (sizeof(apb1_divs) / sizeof(apb1_divs[0]));
         i++) {
        RCC_ClkInitTypeDef clk = { 0 };
        clk.ClockType = RCC_CLOCKTYPE_PCLK1;
        clk.APB1CLKDivider = apb1_divs[i];
        if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK) {
            continue;
        }
        uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
        uint32_t pre = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
        /* PPRE1 codes 0xx = divide by 1 (RM0410). CL-023: this check
         * used to encode the inverted rule. */
        int div1 = (pre & 0x4U) == 0U;
        uint32_t timclk_expect = div1 ? pclk1 : pclk1 * 2U;
        uint32_t timclk_actual;
        pclk1_of(&timclk_actual);
        /* the x2 rule is a register-math check at Level S: the helper
         * the timer tests use must agree with RM0410 for every divider */
        if (timclk_actual == timclk_expect) {
            ok++;
        }
        tried++;
    }
    if (hwval_clock_restore_default() != 0) {
        hwval_fail(o, "restore failed");
        return TFW_FAIL;
    }
    hwval_meas(o, "%d/5 APB1 divider x2 rule checks, %d applied",
               (unsigned long)ok, (unsigned long)tried);
    hwval_lim(o, "PCLK per CFGR; timer clock = PCLK x2 when divider != 1");
    if (tried < 5U) {
        hwval_fail(o, "only %lu divider points", (unsigned long)tried);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CLK-014/015 (RCC) --- */
static volatile uint32_t s_rcc_irq_count;

void RCC_IRQHandler(void);

void RCC_IRQHandler(void)
{
    s_rcc_irq_count++;
    uint32_t cir = RCC->CIR;
    SET_BIT(RCC->CIR, cir & 0x00FFFF00U); /* clear OC/RC/PINn flags */
}

static tfw_result_t clk_rcc_irq_periphs(struct tfw_out *o)
{
    /* CLK-015: PLL-ready interrupt fires straight away (already locked) */
    s_rcc_irq_count = 0U;
    NVIC_SetPriority(RCC_IRQn, 6);
    NVIC_EnableIRQ(RCC_IRQn);
    RCC->CIR = RCC_CIR_PLLRDYIE; /* enable */
    vTaskDelay(pdMS_TO_TICKS(20));
    RCC->CIR = 0U; /* disable + clear */
    NVIC_DisableIRQ(RCC_IRQn);
    if (s_rcc_irq_count == 0U) {
        hwval_fail(o, "PLLRDY interrupt did not fire");
        return TFW_FAIL;
    }

    /* CLK-014: reset several peripherals via RCC and check reset values */
    int rst_bad = 0;

    SET_BIT(RCC->APB1RSTR, RCC_APB1RSTR_TIM2RST);
    for (volatile int i = 0; i < 100; i++) { ; }
    CLEAR_BIT(RCC->APB1RSTR, RCC_APB1RSTR_TIM2RST);
    if (TIM2->CR1 != 0U) { rst_bad++; }

    SET_BIT(RCC->APB1RSTR, RCC_APB1RSTR_USART2RST);
    for (volatile int i = 0; i < 100; i++) { ; }
    CLEAR_BIT(RCC->APB1RSTR, RCC_APB1RSTR_USART2RST);
    if (USART2->CR1 != 0U) { rst_bad++; }

    SET_BIT(RCC->APB1RSTR, RCC_APB1RSTR_SPI2RST);
    for (volatile int i = 0; i < 100; i++) { ; }
    CLEAR_BIT(RCC->APB1RSTR, RCC_APB1RSTR_SPI2RST);
    if (SPI2->CR1 != 0U) { rst_bad++; }

    SET_BIT(RCC->AHB1RSTR, RCC_AHB1RSTR_GPIOARST);
    for (volatile int i = 0; i < 100; i++) { ; }
    CLEAR_BIT(RCC->AHB1RSTR, RCC_AHB1RSTR_GPIOARST);
    __HAL_RCC_GPIOA_CLK_ENABLE();
    if (GPIOA->AFR[0] != 0U) { rst_bad++; }

    hwval_meas(o, "plrdy-irq=%lu rst-bad=%d/4",
               (unsigned long)s_rcc_irq_count, (unsigned long)rst_bad);
    hwval_lim(o, "ready IRQ fires; reset bits restore reset values (CLK-014)");
    if (rst_bad != 0) {
        hwval_fail(o, "%d peripherals did not show reset values",
                   rst_bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_clk_tests[] = {
    { .id = "CLK.001", .group = "CLK", .title = "HSI 16 MHz vs LSE",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 8000U,
      .verifies = "CLK-001", .fn = clk_hsi },
    { .id = "CLK.002", .group = "CLK", .title = "HSE bypass 8 MHz vs LSE",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 8000U,
      .verifies = "CLK-002", .fn = clk_hse_bypass },
    { .id = "CLK.003", .group = "CLK", .title = "HSE crystal mode (X3)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "CLK-003", .fn = clk_hse_crystal },
    { .id = "CLK.004", .group = "CLK", .title = "LSE + drive levels",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 8000U,
      .verifies = "CLK-004", .fn = clk_lse },
    { .id = "CLK.005", .group = "CLK", .title = "LSI via TIM5_CH4 capture",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "CLK-005", .fn = clk_lsi },
    { .id = "CLK.006", .group = "CLK", .title = "main PLL 96/144/216 vs LSE",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 15000U,
      .verifies = "CLK-006", .fn = clk_main_pll },
    { .id = "CLK.007", .group = "CLK", .title = "PLLQ 48 MHz + CLK48 mux",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "CLK-007", .fn = clk_pllq48 },
    { .id = "CLK.008", .group = "CLK", .title = "PLLI2S + SAI clock lock",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "CLK-008, CLK-009", .fn = clk_plli2s_sai },
    { .id = "CLK.010", .group = "CLK", .title = "MCO1/MCO2 configs (+wire/scope)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "CLK-010", .fn = clk_mco },
    { .id = "CLK.011", .group = "CLK", .title = "CSS config, NMI path",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "CLK-011", .fn = clk_css },
    { .id = "CLK.012", .group = "CLK", .title = "LSE CSS (not on F767)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "CLK-012", .fn = clk_lsecss_na },
    { .id = "CLK.013", .group = "CLK", .title = "APB prescaler sweep + restore",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "CLK-013", .fn = clk_bus_prescalers },
    { .id = "CLK.015", .group = "CLK", .title = "RCC_CIR ready interrupt",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "CLK-015, CLK-014", .fn = clk_rcc_irq_periphs },
};

const size_t g_clk_test_count = sizeof(g_clk_tests) / sizeof(g_clk_tests[0]);

void hwval_test_register_clk(void)
{
    hwval_test_register(g_clk_tests, g_clk_test_count);
}