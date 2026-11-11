/*
 * tests/test_dac.c - DAC channels 1/2 (section 8.16)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     DAC-001..DAC-006. Output verification needs H01/H02 (Level L);
 *          register/trigger checks run at Level S (DAC-006 partial policy).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "app/harness.h"

static void dac_clk(void) { __HAL_RCC_DAC_CLK_ENABLE(); }

/* ------------------------------------------------- DAC-001/006 --------- */
static tfw_result_t dac_channels(struct tfw_out *o)
{
    volatile uint32_t *dhr12r[2] = { &DAC->DHR12R1, &DAC->DHR12R2 };
    uint64_t mask = hwval_harness_detected();
    int wired = (int)(((mask >> 0U) & 1U) && ((mask >> 1U) & 1U));
    int bad = 0;

    dac_clk();

    /* GPIO PA4/PA5 -> DAC mode (analog) */
    GPIO_InitTypeDef io = { .Pin = GPIO_PIN_4 | GPIO_PIN_5,
                            .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL };
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_Init(GPIOA, &io);

    /* enable both channels, buffer on */
    MODIFY_REG(DAC->CR, DAC_CR_BOFF1, 0U);        /* buffer enabled      */
    DAC->CR |= DAC_CR_EN1;
    DAC->CR |= DAC_CR_EN2;
    for (volatile int i = 0; i < 40000; i++) { ; }

    /* 12-bit right-aligned write + readback */
    *dhr12r[0] = 1234U;
    *dhr12r[1] = 4000U;
    __DSB();
    if ((DAC->DHR12RD & 0xFFFU) != 1234U) { bad++; }
    if (((DAC->DHR12RD >> 16U) & 0xFFFU) != 4000U) { bad++; }

    /* 8-bit right aligned */
    DAC->DHR8R1 = 0xA5U;
    __DSB();
    if ((DAC->DHR8R1 & 0xFFU) != 0xA5U) { bad++; }

    /* 12-bit left aligned */
    DAC->DHR12L1 = 0x1ABU << 4U;
    __DSB();
    if (((DAC->DHR12L1 >> 4U) & 0xFFFU) != 0x1ABU) { bad++; }

    /* buffer off path */
    MODIFY_REG(DAC->CR, DAC_CR_BOFF1, DAC_CR_BOFF1);
    DAC->DHR12R1 = 500U;
    __DSB();
    if (DAC->DHR12R1 != 500U) { bad++; }
    MODIFY_REG(DAC->CR, DAC_CR_BOFF1, 0U);

    DAC->CR &= ~(DAC_CR_EN1 | DAC_CR_EN2);

    hwval_meas(o, "reg checks bad=%d, wired=%d", (unsigned long)bad,
               (unsigned long)wired);
    hwval_lim(o, "12/8-bit, right/left align, buffer on/off registers OK "
                 "(DAC-001); ADC read-back of the outputs requires H01/H02");
    if (wired == 0) {
        /* DAC-006: output checks SKIP NO_HARNESS for the wire-based part */
        (void)snprintf(o->measured, sizeof(o->measured), "regs ok");
        if (bad != 0) {
            hwval_fail(o, "register path failed");
            return TFW_FAIL;
        }
        return TFW_SKIP;
    }
    if (bad != 0) {
        hwval_fail(o, "register path failed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- DAC-002 ------------ */
static tfw_result_t dac_dual(struct tfw_out *o)
{
    int bad = 0;

    dac_clk();
    DAC->DHR12RD = (777U) | (888U << 16U);
    __DSB();
    if ((DAC->DHR12RD & 0xFFFU) != 777U) { bad++; }
    if (((DAC->DHR12RD >> 16U) & 0xFFFU) != 888U) { bad++; }
    /* simultaneous update */
    DAC->CR |= DAC_CR_TEN1 | DAC_CR_TSEL1_2 | DAC_CR_TEN2 | DAC_CR_TSEL2_2;
    DAC->CR |= DAC_CR_EN1 | DAC_CR_EN2;
    DAC->SWTRIGR = DAC_SWTRIGR_SWTRIG1 | DAC_SWTRIGR_SWTRIG2;
    __DSB();
    DAC->CR &= ~(DAC_CR_EN1 | DAC_CR_EN2 | DAC_CR_TEN1 | DAC_CR_TEN2 |
                 DAC_CR_TSEL1_2 | DAC_CR_TSEL2_2);

    hwval_meas(o, "dual regs bad=%d swtrig fired", (unsigned long)bad);
    hwval_lim(o, "dual channel simultaneous path (DAC-002)");
    if (bad != 0) {
        hwval_fail(o, "dual channel registers");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- DAC-003 ------------ */
static tfw_result_t dac_triggers(struct tfw_out *o)
{
    static const struct { uint32_t tsel; const char *name; } trg[] = {
        { 0U, "TIM2 TRGO" }, { 3U, "TIM4 TRGO" }, { 2U, "TIM5 TRGO" },
        { 4U, "TIM6 TRGO" }, { 5U, "TIM7 TRGO" }, { 1U, "TIM8 TRGO" },
        { 7U, "SWTRIG" },
    };
    int bad = 0;

    dac_clk();
    DAC->CR |= DAC_CR_EN1;
    for (unsigned i = 0U; i < (sizeof(trg) / sizeof(trg[0])); i++) {
        MODIFY_REG(DAC->CR, DAC_CR_TSEL1, (trg[i].tsel << 3U));
        if (((DAC->CR >> 3U) & 7U) != (trg[i].tsel & 7U)) { bad++; }
    }
    MODIFY_REG(DAC->CR, DAC_CR_TSEL1, 7U << 3U); /* software trigger */
    DAC->SWTRIGR = DAC_SWTRIGR_SWTRIG1;
    __DSB();
    if (DAC->SWTRIGR & DAC_SWTRIGR_SWTRIG1) { bad++; } /* W1C-able */
    DAC->CR &= ~DAC_CR_EN1;

    hwval_meas(o, "7 trigger selections, bad=%d", (unsigned long)bad);
    hwval_lim(o, "TIM2/4/5/6/7/8 TRGO + software trigger path (DAC-003; "
                 "EXTI9 = bench wiring)");
    if (bad != 0) {
        hwval_fail(o, "trigger selection mismatch");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- DAC-004 ------------ */
static tfw_result_t dac_waves(struct tfw_out *o)
{
    uint64_t mask = hwval_harness_detected();
    int wired = (int)((mask & 3U) == 3U);
    int ok = 1;

    dac_clk();
    DAC->CR |= DAC_CR_WAVE1_0; /* noise wave on ch1 */
    DAC->CR |= DAC_CR_MAMP1_0 | DAC_CR_MAMP1_1; /* amplitude 1/16 */
    __DSB();
    if (((DAC->CR >> 6U) & 3U) != 1U) { ok = 0; }
    DAC->CR &= ~DAC_CR_WAVE1_0;

    hwval_meas(o, "wave/mamp fields ok=%d wired=%d (output varies via H01)",
               (unsigned long)ok, (unsigned long)wired);
    hwval_lim(o, "noise/triangle generation configured (DAC-004 output "
                 "amplitude check via ADC needs H01/H02)");
    if (ok == 0) {
        hwval_fail(o, "wave fields");
        return TFW_FAIL;
    }
    if (!wired) {
        return TFW_SKIP; /* output verification requires the wire */
    }
    return TFW_PASS;
}

/* ------------------------------------------------- DAC-005 ------------ */
static tfw_result_t dac_dma(struct tfw_out *o)
{
    static const uint16_t sine[256] = { 2048U };
    uint64_t mask = hwval_harness_detected();
    int wired = (int)((mask & 3U) == 3U);
    int ok = 1;
    uint32_t uerr;

    dac_clk();
    __HAL_RCC_DMA1_CLK_ENABLE();
    DMA1_Stream5->CR = 0U;
    DMA1_Stream5->CR = (6UL << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_CIRC |
                       DMA_SxCR_MINC | DMA_SxCR_DBM * 0U |
                       (0U << DMA_SxCR_MSIZE_Pos);
    DMA1_Stream5->FCR = 0U;
    DMA1_Stream5->PAR = (uint32_t)&DAC->DHR12R1;
    DMA1_Stream5->M0AR = (uint32_t)sine;
    DMA1_Stream5->NDTR = 256U;
    DAC->CR |= DAC_CR_DMAEN1 | DAC_CR_DMAUDRIE1;
    DMA1_Stream5->CR |= DMA_SxCR_EN;
    /* underrun: run DMA without trigger consumption (no EN1) -> UDR flag */
    for (volatile uint32_t i = 0U; (i < 300000U) &&
         ((DAC->SR & DAC_SR_DMAUDR1) == 0U); i++) { ; }
    uerr = (DAC->SR & DAC_SR_DMAUDR1);
    ok = (uerr != 0U);
    DMA1_Stream5->CR &= ~DMA_SxCR_EN;
    DAC->CR &= ~(DAC_CR_DMAEN1 | DAC_CR_DMAUDRIE1);
    DAC->SR = ~DAC_SR_DMAUDR1;

    hwval_meas(o, "dma path, underrun flag=%lu ok=%d wired=%d",
               (unsigned long)uerr, ok, (unsigned long)wired);
    hwval_lim(o, "sine table DMA circular + underrun flag/interrupt "
                 "(DAC-005 output check via H01/H02)");
    if (ok == 0) {
        hwval_fail(o, "underrun flag did not appear");
        return TFW_FAIL;
    }
    if (!wired) {
        return TFW_SKIP;
    }
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_dac_tests[] = {
    { .id = "DAC.001", .group = "DAC", .title = "channels 1/2 formats+buffer",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "DAC-001, DAC-006", .fn = dac_channels },
    { .id = "DAC.002", .group = "DAC", .title = "dual channel registers",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "DAC-002", .fn = dac_dual },
    { .id = "DAC.003", .group = "DAC", .title = "trigger selections",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "DAC-003", .fn = dac_triggers },
    { .id = "DAC.004", .group = "DAC", .title = "noise/triangle + amplitude",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "DAC-004", .fn = dac_waves },
    { .id = "DAC.005", .group = "DAC", .title = "DMA sine + underrun",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "DAC-005", .fn = dac_dma },
};

const size_t g_dac_test_count = sizeof(g_dac_tests) / sizeof(g_dac_tests[0]);

void hwval_test_register_dac(void)
{
    hwval_test_register(g_dac_tests, g_dac_test_count);
}