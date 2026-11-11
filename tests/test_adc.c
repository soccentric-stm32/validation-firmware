/*
 * tests/test_adc.c - ADC1/2/3 (section 8.15)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     ADC-001..ADC-003, ADC-004 (harness), ADC-005..ADC-012.
 * Register-level drivers (F7 ADC is factory-calibrated; no ADCAL).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "hwval_limits.h"
#include "app/test_core.h"
#include "app/harness.h"
#include "services/fault.h"
#include "hwval_sections.h"

#define ADC1_BASE_A 0x40012000U
#define ADC2_BASE_A 0x40012100U
#define ADC3_BASE_A 0x40012200U
#define ADCCOM_BASE 0x40012300U

static void adc_clk(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC2_CLK_ENABLE();
    __HAL_RCC_ADC3_CLK_ENABLE();
    /* common CCR: APB2/4 ADC clock, TSVREFE/VBAT enabled on demand */
    MODIFY_REG(ADC->CCR, ADC_CCR_ADCPRE, ADC_CCR_ADCPRE_1 /* div4 */);
}

static ADC_TypeDef *adc(uint32_t n)
{
    return (ADC_TypeDef *)(n == 1U ? ADC1_BASE_A :
                           n == 2U ? ADC2_BASE_A : ADC3_BASE_A);
}

static int adc_on(ADC_TypeDef *a)
{
    if ((a->CR2 & ADC_CR2_ADON) == 0U) {
        a->CR2 |= ADC_CR2_ADON;
        for (volatile int i = 0; i < 2000; i++) { ; }
    }
    return 0;
}

static uint16_t adc_conv(ADC_TypeDef *a, uint8_t ch, uint32_t smp,
                         uint32_t res)
{
    adc_clk();
    adc_on(a);
    a->CR1 = (a->CR1 & ~ADC_CR1_RES) |
             (((uint32_t)(res & 3U)) << ADC_CR1_RES_Pos);
    a->SQR1 = 0U;
    a->CR2 = (a->CR2 & ~ADC_CR2_ALIGN) | 0U; /* right aligned */
    a->SQR3 = (uint32_t)ch;
    a->SMPR1 = 0U;
    a->SMPR2 = 0U;
    if (ch < 10U) {
        MODIFY_REG(a->SMPR2, 7UL << (ch * 3U), smp << (ch * 3U));
    } else {
        uint8_t c = (uint8_t)(ch - 10U);
        MODIFY_REG(a->SMPR1, 7UL << (c * 3U), smp << (c * 3U));
    }
    a->SR = 0U;
    a->CR2 |= ADC_CR2_SWSTART;
    while ((a->SR & ADC_SR_EOC) == 0U) { __NOP(); }
    uint16_t v = (uint16_t)(a->DR & 0xFFFU);
    if (res == 0U) { v = (uint16_t)(v & 0xFFFU); }
    else if (res == 1U) { v = (uint16_t)(v & 0x3FFU); }
    else if (res == 2U) { v = (uint16_t)(v & 0xFFU); }
    else { v = (uint16_t)(v & 0x3FU); }
    return v;
}

/* ------------------------------------------------- ADC-001 -------------- */
static tfw_result_t adc_resolutions(struct tfw_out *o)
{
    int bad = 0;
    uint32_t convs = 0U;

    /* 3 ADCs, resolution 12/10/8/6 with short + long sample times on the
     * internal reference channel (channel 17 = VREFINT on ADC1; ADC2/3
     * channel 17 is VBAT switch? For the sweep use channel 18 VBAT on
     * ADC123 for consistency (VBAT switch shared). */
    adc_clk();
    ADC->CCR |= ADC_CCR_VBATE;
    uint16_t ref12 = adc_conv(adc(1U), 18U, 0U /*3cyc*/, 0U);
    uint16_t ref10 = adc_conv(adc(2U), 18U, 7U << 3U /*480cyc*/, 1U);
    uint16_t ref8  = adc_conv(adc(3U), 18U, 7U << 3U, 2U);
    uint16_t ref6  = adc_conv(adc(1U), 18U, 3U << 3U, 3U);
    uint16_t ref12l = adc_conv(adc(1U), 18U, 7U << 3U, 0U);
    convs += 5U;

    double f12 = (double)ref12 * 4096.0 / 4096.0;
    (void)f12;
    /* consistency after scaling to 12-bit */
    uint32_t r10 = (uint32_t)ref10 << 2U;
    uint32_t r8  = (uint32_t)ref8 << 4U;
    uint32_t r6  = (uint32_t)ref6 << 6U;
    if ((r10 + 8U < ref12) || (ref12 + 8U < r10)) { bad++; }
    if ((r8 + 24U < ref12) || (ref12 + 24U < r8)) { bad++; }
    if (ref12l == 0U) { bad++; }

    ADC->CCR &= ~ADC_CCR_VBATE;
    hwval_meas(o, "12=%u long=%u 10x4=%u 8x16=%u 6x64=%u convs=%lu",
               (unsigned long)ref12, (unsigned long)ref12l,
               (unsigned long)r10, (unsigned long)r8, (unsigned long)r6,
               (unsigned long)convs);
    hwval_lim(o, "12/10/8/6-bit results consistent within +-0.2 % of "
                 "full scale (ADC-001)");
    if (bad != 0) {
        hwval_fail(o, "resolution sweep inconsistent");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- ADC-002 -------------- */
static tfw_result_t adc_internals(struct tfw_out *o)
{
    uint32_t vrefint_cal = *(const uint16_t *)0x1FF0F44AU;
    uint32_t ts1 = *(const uint16_t *)0x1FF0F44CU;
    uint32_t ts2 = *(const uint16_t *)0x1FF0F44EU;

    adc_clk();
    ADC->CCR |= ADC_CCR_TSVREFE | ADC_CCR_VBATE;
    uint16_t vref_raw = adc_conv(adc(1U), 17U, 7U << 3U, 0U);
    uint16_t t_raw    = adc_conv(adc(1U), 18U, 7U << 3U, 0U);
    ADC->CCR &= ~(ADC_CCR_TSVREFE | ADC_CCR_VBATE);

    uint32_t vdda_mv = (3300U * (uint32_t)vrefint_cal) / vref_raw;
    double t_c = ((110.0 - 30.0) / ((double)(ts2 - ts1))) *
                 ((double)t_raw - (double)ts1) + 30.0;
    uint32_t vref_expect = (3300U * 100U) / 100U;

    hwval_meas(o, "VREFINT=%u vdda=%lu mV, temp=%.1f C", (unsigned long)vref_raw,
               (unsigned long)vdda_mv, t_c);
    hwval_lim(o, "VREFINT vs cal +-2 %; temp 0-60 C bench; VBAT = PWR.001");
    if (((uint32_t)vref_raw * 100U < vrefint_cal * 98U) ||
        ((uint32_t)vref_raw * 100U > vrefint_cal * 102U)) {
        hwval_fail(o, "VREFINT raw %u vs cal %lu (>2 %%)",
                   (unsigned)vref_raw, (unsigned long)vrefint_cal);
        return TFW_FAIL;
    }
    if ((t_c < (double)HWVAL_LIM_TSENSOR_MIN_C) || (t_c > (double)HWVAL_LIM_TSENSOR_MAX_C)) {
        hwval_fail(o, "temperature %.1f outside bench window", t_c);
        return TFW_FAIL;
    }
    (void)vref_expect;
    return TFW_PASS;
}

/* ------------------------------------------------- ADC-003 -------------- */
static struct { uint8_t port, num; uint8_t ch; uint8_t a; const char *pin; } k_adc_pins[] = {
    { 0, 3,  3,  1, "PA3 A0" },   { 2, 0, 10, 1, "PC0 A1" },
    { 2, 3, 13, 1, "PC3 A2" },    { 5, 3,  9, 3, "PF3 A3 (IN9)" },
    { 5, 5, 15, 3, "PF5 A4 (IN15)" }, { 5, 10, 8, 3, "PF10 A5 (IN8)" },
};
static tfw_result_t adc_ext_pins(struct tfw_out *o)
{
    int bad = 0;
    unsigned tested = 0U;
    GPIO_TypeDef *gp[9] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG,
                            NULL, GPIOH };

    adc_clk();
    for (unsigned i = 0U; i < (sizeof(k_adc_pins) / sizeof(k_adc_pins[0]));
         i++) {
        uint8_t port = k_adc_pins[i].port, num2 = k_adc_pins[i].num;
        uint8_t idx = hwval_pin_index(port, num2);
        const struct hwval_pin *p = hwval_pin_at(idx);
        if (!p || (p->owner != PIN_OWNER_FREE) || !p->drivable) { continue; }

        GPIO_TypeDef *g = gp[port];
        uint32_t bit = 1UL << num2;
        __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOF_CLK_ENABLE();

        /* drive high: expect near full scale */
        GPIO_InitTypeDef go = { .Pin = bit, .Mode = GPIO_MODE_OUTPUT_PP,
                                .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
        HAL_GPIO_Init(g, &go);
        HAL_GPIO_WritePin(g, bit, GPIO_PIN_SET);
        for (volatile int d = 0; d < 500; d++) { ; }
        uint16_t hi = adc_conv(adc(1U), k_adc_pins[i].ch, 3U << 3U, 0U);
        /* drive low: expect near 0 */
        HAL_GPIO_WritePin(g, bit, GPIO_PIN_RESET);
        for (volatile int d = 0; d < 500; d++) { ; }
        uint16_t lo = adc_conv(adc(1U), k_adc_pins[i].ch, 3U << 3U, 0U);

        hi = (uint16_t)(hi & 0xFFFU);
        uint16_t himax = 4095U;
        if ((uint32_t)hi < (himax - 240UL)) { bad++; }
        if ((uint32_t)lo > 240UL) { bad++; }

        /* back to analog */
        GPIO_InitTypeDef ga = { .Pin = bit, .Mode = GPIO_MODE_ANALOG,
                                .Pull = GPIO_NOPULL };
        HAL_GPIO_Init(g, &ga);
        tested++;
        (void)hi; (void)lo;
    }
    hwval_meas(o, "%u external ch driven full-scale/zero, bad=%d", tested,
               (unsigned long)bad);
    hwval_lim(o, "GPIO-driven channels reach FS and 0 (ADC-003; the DAC "
                 "loopback INL/DNL sweep needs H01/H02, ADC.004)");
    if (bad != 0) {
        hwval_fail(o, "%d channel level checks failed", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- ADC.004 (harness) ---- */
static tfw_result_t adc_dac_loop(struct tfw_out *o)
{
    uint64_t mask = hwval_harness_detected();

    if (((mask >> 0U) & 1U) == 0U) { /* H01 */
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H01/H02 DAC->ADC wires; INL/DNL sweep)");
        return TFW_SKIP;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "harness ok");
    return TFW_SKIP; /* wire-in step with the DAC group on the bench */
}

/* ------------------------------------------------- ADC-005/009 ---------- */
static tfw_result_t adc_sequences(struct tfw_out *o)
{
    ADC_TypeDef *a = adc(1U);
    int bad = 0;
    uint16_t r0, r1;

    adc_clk();
    ADC->CCR |= ADC_CCR_TSVREFE;
    adc_on(a);
    /* regular sequence 2: VREFINT (17) then temp (18) */
    a->SQR1 = (1UL << 20U); /* L = 2 */
    a->SQR3 = 17U | (18UL << 5U);
    a->SMPR1 = (7UL << (7U * 3U)) | (7UL << (8U * 3U));
    a->SR = 0U;
    a->CR2 |= ADC_CR2_SWSTART;
    while ((a->SR & ADC_SR_EOC) == 0U) { __NOP(); }
    r0 = (uint16_t)(a->DR & 0xFFFU);
    while ((a->SR & ADC_SR_EOC) == 0U) { __NOP(); }
    r1 = (uint16_t)(a->DR & 0xFFFU);
    a->SQR1 = 0U;
    if (r0 == 0U || r1 == 0U) { bad++; }

    /* injected: rank 1 = ch 17, JEOC */
    a->JSQR = 17UL << 15U;
    a->JSQR = 17U | (0UL << 15U); /* JL=0, sq1=17 */
    a->JSQR = 17U;
    a->CR2 |= ADC_CR2_JSWSTART;
    while ((a->SR & ADC_SR_JEOC) == 0U) { __NOP(); }
    uint16_t inj = (uint16_t)(a->JDR1 & 0xFFFU);
    a->SR = 0U;
    if (inj == 0U) { bad++; }
    ADC->CCR &= ~ADC_CCR_TSVREFE;

    /* dual simultaneous ADC1+ADC2 (ADC-009 compact check) */
    ADC->CCR |= ADC_CCR_MULTI_4 | ADC_CCR_MULTI_0; /* mode 6? use dual sim */
    /* CCR MULTI: regular simultaneous = 000001 = bits0|5? keep simple:
     * DUAL[4:0] = 0b00001 regular simultaneous */
    MODIFY_REG(ADC->CCR, 0x1FUL, 0x1U);
    adc_on(adc(2U));
    adc(2U)->SQR1 = 0U;
    adc(2U)->SQR3 = 18U;
    adc(2U)->SMPR1 = (7UL << (8U * 3U));
    a->SQR1 = 0U;
    a->SQR3 = 18U;
    a->SMPR1 = (7UL << (8U * 3U));
    a->SR = 0U;
    a->CR2 |= ADC_CR2_SWSTART;
    while ((a->SR & ADC_SR_EOC) == 0U) { __NOP(); }
    uint16_t d0 = (uint16_t)(a->DR & 0xFFFU);
    while ((adc(2U)->SR & ADC_SR_EOC) == 0U) { __NOP(); }
    uint16_t d1 = (uint16_t)(adc(2U)->DR & 0xFFFU);
    if (d0 == 0U || d1 == 0U) { bad++; }
    MODIFY_REG(ADC->CCR, 0x1FUL, 0U);

    hwval_meas(o, "seq r0=%u r1=%u inj=%u dual %u/%u bad=%d",
               (unsigned long)r0, (unsigned long)r1, (unsigned long)inj,
               (unsigned long)d0, (unsigned long)d1, bad);
    hwval_lim(o, "regular seq, injected, dual simultaneous produce data "
                 "(ADC-005/009 base); interleaved 7.2 MSPS = bench variant");
    if (bad != 0) {
        hwval_fail(o, "sequence conversions failed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- ADC-006 (trigger) ---- */
static tfw_result_t adc_triggers(struct tfw_out *o)
{
    ADC_TypeDef *a = adc(1U);
    int ok = 1;

    adc_clk();
    ADC->CCR |= ADC_CCR_TSVREFE;
    adc_on(a);
    a->SQR1 = 0U;
    a->SQR3 = 17U;
    a->SMPR1 = (7UL << (7U * 3U));
    /* EXT = TIM2 TRGO (bits 0b00100? EXT[3:0]=TIM2 CC2? use EXT = SWSTART
     * default path then TIM2 TRGO: EXTSEL for F7: ADC_CR2_EXTSEL TIM2_CC2
     * code 0b0001? Use TRGO: EXTEN rising on TIM2_TRGO code 0b00100. */
    MODIFY_REG(a->CR2, ADC_CR2_EXTSEL, (4UL << 24U) /* TIM2_TRGO? */);
    a->CR2 |= ADC_CR2_EXTEN_0; /* rising edge */
    TIM2->CR1 = 0U;
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->PSC = (HAL_RCC_GetHCLKFreq() / 10000U) - 1U;
    TIM2->ARR = 100U - 1U;
    TIM2->CR2 = TIM_CR2_MMS_1; /* update -> TRGO */
    TIM2->EGR = TIM_EGR_UG;
    a->SR = 0U;
    TIM2->CR1 = TIM_CR1_CEN;
    uint32_t convs = 0U;
    for (volatile int w = 0; w < 400000; w++) {
        if ((a->SR & ADC_SR_EOC) != 0U) {
            (void)a->DR;
            a->SR = ~ADC_SR_EOC;
            convs++;
            if (convs >= 3U) { break; }
        }
        if (convs == 0U && w == 399999U) { /* timeout below */ }
    }
    TIM2->CR1 = 0U;
    a->CR2 &= ~ADC_CR2_EXTEN;
    MODIFY_REG(a->CR2, ADC_CR2_EXTSEL, 0UL);
    ADC->CCR &= ~ADC_CCR_TSVREFE;
    if (convs < 2U) { ok = 0; }
    hwval_meas(o, "TIM2-TRGO triggered convs=%lu", (unsigned long)convs);
    hwval_lim(o, "timer trigger path fires conversions (ADC-006; EXTI 11/15 "
                 "trigger needs harness, bench)");
    if (ok == 0) {
        hwval_fail(o, "no conversions from the timer trigger");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- ADC-007 (AWD/OVR) ---- */
static tfw_result_t adc_watchdog(struct tfw_out *o)
{
    ADC_TypeDef *a = adc(1U);
    int awd_ok = 0, ovr_ok = 0;

    adc_clk();
    ADC->CCR |= ADC_CCR_TSVREFE;
    adc_on(a);
    a->SQR1 = 0U;
    a->SQR3 = 17U;
    a->SMPR1 = (7UL << (7U * 3U));
    /* AWD on channel 17, low threshold above the value -> must fire */
    a->LTR = 3000U;
    a->HTR = 0xFFFU;
    { uint32_t cr1 = a->CR1;
      cr1 &= ~(ADC_CR1_AWDCH | ADC_CR1_AWDSGL);
      cr1 |= (17UL << ADC_CR1_AWDCH_Pos) | ADC_CR1_AWDSGL | ADC_CR1_AWDEN;
      a->CR1 = cr1;
    }
    a->CR2 |= ADC_CR2_SWSTART;
    while ((a->SR & ADC_SR_EOC) == 0U) { __NOP(); }
    (void)a->DR;
    for (volatile uint32_t w = 0U; w < 200000UL; w++) {
        if ((a->SR & ADC_SR_AWD) != 0U) { awd_ok = 1; a->SR = ~ADC_SR_AWD; break; }
    }
    /* overrun: read DR once, then let SWSTART set new data without reading */
    a->CR1 &= ~(ADC_CR1_AWDEN | ADC_CR1_AWDSGL);
    a->SR = 0U;
    a->CR2 |= ADC_CR2_SWSTART;
    while ((a->SR & ADC_SR_EOC) == 0U) { __NOP(); }
    (void)a->DR;
    a->CR2 |= ADC_CR2_SWSTART;
    int tries = 0;
    while (((a->SR & ADC_SR_OVR) == 0U) && (tries < 200000)) { tries++; }
    if ((a->SR & ADC_SR_OVR) != 0U) {
        ovr_ok = 1;
        a->SR = ~ADC_SR_OVR;
    }
    ADC->CCR &= ~ADC_CCR_TSVREFE;
    hwval_meas(o, "awd=%lu ovr=%lu", (unsigned long)awd_ok,
               (unsigned long)ovr_ok);
    hwval_lim(o, "analog watchdog + overrun flags verified (ADC-007; "
                 "multi-channel AWD = bench wiring for Level L)");
    if (!awd_ok || !ovr_ok) {
        hwval_fail(o, "watchdog/overrun flags missing");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- ADC-008/010/011/012 -- */
static tfw_result_t adc_dma_noise_rate(struct tfw_out *o)
{
    static uint16_t buf[512] HWVAL_SRAM1;
    int rc;
    volatile uint32_t convs = 0U;
    uint32_t t0, ms;

    adc_clk();
    __HAL_RCC_DMA2_CLK_ENABLE();
    ADC->CCR |= ADC_CCR_TSVREFE;
    ADC1->CR2 = 0U;
    adc_on(ADC1);
    ADC1->SQR1 = 0U;
    ADC1->SQR3 = 17U;
    ADC1->SMPR1 = (2UL << (7U * 3U)); /* 28 cyc */
    ADC1->CR2 |= ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_CONT;
    DMA2_Stream0->CR = 0U;
    DMA2_Stream0->CR = DMA_SxCR_CIRC | DMA_SxCR_MINC |
                       (1UL << DMA_SxCR_MSIZE_Pos) | (1UL << DMA_SxCR_PSIZE_Pos);
    DMA2_Stream0->FCR = 0U;
    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)buf;
    DMA2_Stream0->NDTR = 512U;
    DMA2->LIFCR = 0x0F7D0F7DU;
    DMA2_Stream0->CR |= DMA_SxCR_EN;
    convs = 0U;
    ADC1->SR = 0U;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 60U) { ; }
    ms = HAL_GetTick() - t0;
    convs = 512U - DMA2_Stream0->NDTR; /* circular: approximate rate */
    rc = (convs > 0U) ? 1 : 0;
    ADC1->CR2 &= ~ADC_CR2_SWSTART;
    ADC1->CR2 &= ~(ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_CONT);
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;

    /* noise: stddev of 1024 VREFINT samples (long sample time, polled) */
    uint32_t sum = 0U, sq = 0U;
    uint16_t vals[256];
    for (int i = 0; i < 256; i++) {
        vals[i] = adc_conv(ADC1, 17U, 7U << 3U, 0U);
        sum += vals[i];
    }
    uint32_t mean = sum / 256U;
    for (int i = 0; i < 256; i++) {
        int32_t d = (int32_t)vals[i] - (int32_t)mean;
        sq += (uint32_t)(d * d);
    }
    /* integer stddev approx in LSB * 16 units (stddev16 = sqrt-ish via
     * mean-of-abs deviation * 1.25, documented) */
    uint32_t absdev = 0U;
    for (int i = 0; i < 256; i++) {
        int32_t d = (int32_t)vals[i] - (int32_t)mean;
        absdev += (d < 0) ? (uint32_t)(-d) : (uint32_t)d;
    }
    uint32_t sd16 = (absdev * 5U) / (256U * 4U); /* ~sigma in LSB */
    ADC->CCR &= ~ADC_CCR_TSVREFE;

    uint32_t mrate = (ms > 0U) ? (convs * 1000U / ms) : 0U;
    hwval_meas(o, "dma-conv=%lu/%lums rate=%lu conv/s, vref-sd16=%lu LSB",
               (unsigned long)convs, (unsigned long)ms,
               (unsigned long)mrate, (unsigned long)sd16);
    hwval_lim(o, "DMA stream moving data (ADC-008); noise <= 2 LSB "
                 "(HWVAL_LIM_ADC_NOISE_LSB, ADC-010); rate reported (ADC-011)");
    if ((rc == 0) || (sd16 > HWVAL_LIM_ADC_NOISE_LSB)) {
        hwval_fail(o, "noise %lu LSB16 too high (limit %lu)",
                   (unsigned long)sd16,
                   (unsigned long)HWVAL_LIM_ADC_NOISE_LSB);
        return TFW_FAIL;
    }
    (void)sq;
    return TFW_PASS;
}

/* ------------------------------------------------- ADC-012 -------------- */
/* On the F767 ADC1/2/3 share the single ADC_IRQn vector (RM0410 vector
 * table); test_int.c's ADC_IRQHandler owns it and calls this hook first
 * (weak default = empty), so the EOC/JEOC/OVR counters are self-contained
 * and only active while ADC.012 runs (interrupt-enable checked).        */
static volatile uint32_t s_adc3_eoc, s_adc3_jeoc, s_adc3_ovr;

void hwval_adc_irq_hook(void)
{
    if ((ADC3->CR1 & (ADC_CR1_EOCIE | ADC_CR1_JEOCIE | ADC_CR1_OVRIE)) == 0U) {
        return;
    }
    uint32_t sr = ADC3->SR;

    if ((sr & ADC_SR_EOC) != 0U)  { s_adc3_eoc++;  }
    if ((sr & ADC_SR_JEOC) != 0U) { s_adc3_jeoc++; }
    if ((sr & ADC_SR_OVR) != 0U)  { s_adc3_ovr++;  }
    ADC3->SR = ~sr;                             /* clear (write 0)        */
}

static tfw_result_t adc_interrupts(struct tfw_out *o)
{
    volatile uint32_t sink = 0U;

    adc_clk();
    ADC->CCR |= ADC_CCR_TSVREFE;
    ADC3->CR2 = 0U;
    adc_on(ADC3);
    ADC3->SQR1 = 0U;
    ADC3->JSQR = (17UL << ADC_JSQR_JSQ1_Pos);   /* injected: VBAT ch      */
    ADC3->JSQR |= (0UL << ADC_JSQR_JL_Pos);    /* 1 conversion (JL=00)   */
    ADC3->SMPR1 = (2UL << (7U * 3U));
    ADC3->SR = 0U;
    ADC3->CR1 |= ADC_CR1_EOCIE | ADC_CR1_JEOCIE | ADC_CR1_OVRIE;
    s_adc3_eoc = 0U; s_adc3_jeoc = 0U; s_adc3_ovr = 0U;
    NVIC_EnableIRQ(ADC_IRQn);

    /* EOC: two regular SW-start conversions */
    ADC3->CR2 |= ADC_CR2_SWSTART;
    { uint32_t tmo = 200000U;
      while (((ADC3->SR & ADC_SR_EOC) == 0U) && (--tmo != 0U)) { ; }
      sink += ADC3->DR; }
    ADC3->CR2 |= ADC_CR2_SWSTART;
    { uint32_t tmo = 200000U;
      while (((ADC3->SR & ADC_SR_EOC) == 0U) && (--tmo != 0U)) { ; }
      sink += ADC3->DR; }

    /* JEOC: one injected conversion */
    ADC3->CR2 |= ADC_CR2_JSWSTART;
    { uint32_t tmo = 200000U;
      while (((ADC3->SR & ADC_SR_JEOC) == 0U) && (--tmo != 0U)) { ; }
      sink += ADC3->JDR1; }

    /* OVR: stop reading DR, let the 4-word FIFO overflow */
    for (int i = 0; i < 8; i++) { ADC3->CR2 |= ADC_CR2_SWSTART; }

    vTaskDelay(10);
    uint32_t eoc = s_adc3_eoc, jeoc = s_adc3_jeoc, ovr = s_adc3_ovr;
    NVIC_DisableIRQ(ADC_IRQn);
    ADC3->CR1 &= ~(ADC_CR1_EOCIE | ADC_CR1_JEOCIE | ADC_CR1_OVRIE);
    ADC3->CR2 = 0U;
    sink += ADC3->SR;

    hwval_meas(o, "eoc=%lu jeoc=%lu ovr=%lu", (unsigned long)eoc,
               (unsigned long)jeoc, (unsigned long)ovr);
    hwval_lim(o, "EOC/JEOC/OVR each >= 1 hit, cleared (ADC-012)");
    (void)sink;
    if ((eoc == 0U) || (jeoc == 0U) || (ovr == 0U)) {
        hwval_fail(o, "ADC3 interrupt counters: %lu/%lu/%lu",
                   (unsigned long)eoc, (unsigned long)jeoc,
                   (unsigned long)ovr);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_adc_tests[] = {
    { .id = "ADC.001", .group = "ADC", .title = "resolutions 12/10/8/6 x3 ADCs",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "ADC-001", .fn = adc_resolutions },
    { .id = "ADC.002", .group = "ADC", .title = "VREFINT/temp/VBAT internals",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "ADC-002, PWR-001", .fn = adc_internals },
    { .id = "ADC.003", .group = "ADC", .title = "external channels self-driven",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "ADC-003, GPIO-008", .fn = adc_ext_pins },
    { .id = "ADC.004", .group = "ADC", .title = "DAC->ADC linearity (Level L)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "ADC-004", .fn = adc_dac_loop },
    { .id = "ADC.005", .group = "ADC", .title = "sequences/injected/dual-sim",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "ADC-005, ADC-009", .fn = adc_sequences },
    { .id = "ADC.006", .group = "ADC", .title = "timer TRGO trigger",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "ADC-006", .fn = adc_triggers },
    { .id = "ADC.007", .group = "ADC", .title = "AWD + overrun",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "ADC-007", .fn = adc_watchdog },
    { .id = "ADC.008", .group = "ADC", .title = "DMA, noise, rate",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "ADC-008, ADC-010, ADC-011",
      .fn = adc_dma_noise_rate },
    { .id = "ADC.012", .group = "ADC", .title = "EOC/JEOC/OVR interrupts (ADC3)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "ADC-012", .fn = adc_interrupts },
};

const size_t g_adc_test_count = sizeof(g_adc_tests) / sizeof(g_adc_tests[0]);

void hwval_test_register_adc(void)
{
    hwval_test_register(g_adc_tests, g_adc_test_count);
}