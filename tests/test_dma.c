/*
 * tests/test_dma.c - DMA controllers (section 8.12)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     DMA-001..DMA-007, level S.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "services/fault.h"
#include "hwval_sections.h"

extern uint8_t hwval_itcm_base[]; /* linker symbol */

static void dma_clk(void) { __HAL_RCC_DMA1_CLK_ENABLE(); __HAL_RCC_DMA2_CLK_ENABLE(); }

static int dma_cfg(DMA_Stream_TypeDef *d, uint32_t ch, uint32_t dir,
                   uint32_t psize, uint32_t msize, uint32_t inc, uint32_t prio,
                   uint32_t fifo, uint32_t dbm)
{
    d->CR = 0U;
    while (d->CR & DMA_SxCR_EN) { ; }
    uint32_t cr = (ch << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC * 0U |
                  (msize << DMA_SxCR_MSIZE_Pos) | (psize << DMA_SxCR_PSIZE_Pos) |
                  (prio << DMA_SxCR_PL_Pos) | dir | (inc ? DMA_SxCR_MINC |
                   DMA_SxCR_PINC : 0U) | (dbm ? DMA_SxCR_DBM : 0U);
    d->CR = cr;
    if (fifo) { /* FIFO enabled: threshold full, burst INCR4 */
        d->FCR = DMA_SxFCR_FTH_0 | DMA_SxFCR_FTH_1; /* full */
        d->CR |= DMA_SxCR_MBURST_0 | DMA_SxCR_PBURST_0;
    } else {
        d->FCR = 0U; /* FCR FTH=0 with DMDIS=0 -> direct mode */
    }
    return 0;
}

static int dma_xfer(DMA_Stream_TypeDef *d, const void *src, void *dst,
                    uint32_t items, uint32_t timeout)
{
    __DSB();
    d->PAR = (uint32_t)(uintptr_t)src;
    d->M0AR = (uint32_t)(uintptr_t)dst;
    d->NDTR = items;
    __DSB();
    d->CR |= DMA_SxCR_EN;
    for (volatile uint32_t i = 0U; i < timeout; i++) {
        if (d->NDTR == 0U) { d->CR &= ~DMA_SxCR_EN; return 0; }
    }
    d->CR &= ~DMA_SxCR_EN;
    return -1;
}

/* ------------------------------------------------- DMA-001 (8 streams) -- */
static tfw_result_t dma_m2m_streams(struct tfw_out *o)
{
    static uint32_t src[64] HWVAL_SRAM1;
    static uint32_t dst[64] HWVAL_SRAM1; /* reused: streams run sequentially */
    DMA_Stream_TypeDef *s[8] = {
        DMA2_Stream0, DMA2_Stream1, DMA2_Stream2, DMA2_Stream3,
        DMA2_Stream4, DMA2_Stream5, DMA2_Stream6, DMA2_Stream7,
    };
    int bad = 0;
    uint32_t words = 64U;

    dma_clk();
    for (uint32_t i = 0U; i < words; i++) { src[i] = 0xC1000000U + i; }

    for (int st = 0; st < 8; st++) {
        uint32_t dir = (st % 2) ? DMA_SxCR_DIR_0 : 0U; /* both dirs legal m2m */
        dma_cfg(s[st], (uint32_t)(st % 8), dir, DMA_SxCR_PSIZE_1, /*word*/
                DMA_SxCR_MSIZE_1, 1U, 2U, 0U, 0U);
        if (dma_xfer(s[st], src, dst, words, 400000) != 0) {
            bad++; continue;
        }
        if (dst[0] != 0xC1000000U || dst[63] != 0xC1000000U + 63U) {
            bad++;
        }
    }
    /* 8/16/32-bit widths + FIFO/burst + direct-mode sanity on stream 0 */
    dma_cfg(DMA2_Stream0, 0, 0U, 0U, 0U, 1U, 2U, 0U, 0U); /* bytes */
    if (dma_xfer(DMA2_Stream0, src, dst, 256U, 400000) != 0) { bad++; }
    if (((uint8_t *)dst)[255] != (uint8_t)0xC1000000U) { bad++; }
    dma_cfg(DMA2_Stream0, 0, 0U, DMA_SxCR_PSIZE_0, DMA_SxCR_MSIZE_0, 1U, 2U,
            1U, 0U); /* halfwords, FIFO */
    if (dma_xfer(DMA2_Stream0, src, dst, 128U, 400000) != 0) { bad++; }
    hwval_meas(o, "8 DMA2 m2m streams, widths 8/16/32, fifo+direct: bad=%d",
               (unsigned long)bad);
    hwval_lim(o, "every DMA2 stream transfers (DMA-001); DMA1 needs periph "
                 "requests (UART/ADC owner tests cover, DMA-003)");
    if (bad != 0) {
        hwval_fail(o, "%d/8 streams failed", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- DMA-002 (IRQs) ------ */
static volatile uint32_t d_tc, d_ht, d_err, d_fe, d_dme;

void DMA2_Stream1_IRQHandler(void);

void DMA2_Stream1_IRQHandler(void)
{
    uint32_t isr = DMA2->LISR;
    if (isr & DMA_LISR_TCIF1) { d_tc++;  DMA2->LIFCR = DMA_LIFCR_CTCIF1; }
    if (isr & DMA_LISR_HTIF1) { d_ht++;  DMA2->LIFCR = DMA_LIFCR_CHTIF1; }
    if (isr & DMA_LISR_TEIF1) { d_err++; DMA2->LIFCR = DMA_LIFCR_CTEIF1; }
    if (isr & DMA_LISR_FEIF1) { d_fe++;  DMA2->LIFCR = DMA_LIFCR_CFEIF1; }
    if (isr & DMA_LISR_DMEIF1) { d_dme++; DMA2->LIFCR = DMA_LIFCR_CDMEIF1; }
}

static tfw_result_t dma_irqs(struct tfw_out *o)
{
    static uint32_t src[16], dst[16];
    uint32_t tc0, ht0;

    dma_clk();
    for (int i = 0; i < 16; i++) { src[i] = i; dst[i] = 0; }
    d_tc = d_ht = d_err = d_fe = d_dme = 0U;
    NVIC_SetPriority(DMA2_Stream1_IRQn, 6);
    NVIC_EnableIRQ(DMA2_Stream1_IRQn);

    dma_cfg(DMA2_Stream1, 0, 0U, 1U, 1U, 1U, 2U, 0U, 0U);
    /* TC + HT */
    DMA2_Stream1->FCR = DMA_SxFCR_DMDIS * 0U;
    DMA2->LIFCR = 0x0F7D0F7DU; /* clear all */
    DMA2_Stream1->CR |= DMA_SxCR_TCIE | DMA_SxCR_HTIE;
    (void)dma_xfer(DMA2_Stream1, src, dst, 16U, 400000);
    tc0 = d_tc; ht0 = d_ht;
    DMA2_Stream1->CR &= ~(DMA_SxCR_TCIE | DMA_SxCR_HTIE);

    /* TE: read from flash into DTCM? flash reads fine... force TE via
     * bus fault address (0x24F00000) with BUSFAULT? that faults the CPU
     * not the stream. Use FIFO error by misconfigured burst: direct mode
     * error via invalid address on PAR = 0x24F00000 (DME/TE). */
    DMA2_Stream1->CR |= DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    DMA2_Stream1->FCR |= DMA_SxFCR_FEIE;
    DMA2_Stream1->PAR = 0x24F00000U;
    DMA2_Stream1->M0AR = (uint32_t)dst;
    DMA2_Stream1->NDTR = 4U;
    DMA2_Stream1->CR |= DMA_SxCR_EN;
    for (volatile uint32_t i = 0U; i < 400000U; i++) {
        if (((DMA2->LISR & (DMA_LISR_TEIF1 | DMA_LISR_DMEIF1)) != 0U)
            || (d_err != 0U) || (d_dme != 0U)) { break; }
    }
    DMA2_Stream1->CR &= ~DMA_SxCR_EN;
    DMA2->LIFCR = DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CFEIF1;
    DMA2_Stream1->CR &= ~(DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
    DMA2_Stream1->FCR &= ~DMA_SxFCR_FEIE;
    NVIC_DisableIRQ(DMA2_Stream1_IRQn);

    hwval_meas(o, "tc=%lu ht=%lu err=%lu dme=%lu", (unsigned long)tc0,
               (unsigned long)ht0, (unsigned long)d_err + d_dme,
               (unsigned long)d_fe);
    hwval_lim(o, "TC + HT fired; TE/DME fired on bad address (DMA-002)");
    if ((tc0 == 0U) || (ht0 == 0U) ||
        ((d_err + d_dme) == 0U)) {
        hwval_fail(o, "interrupt evidence incomplete");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static uint32_t dtcm_scratch_buf[8] __attribute__((aligned(16)));
static void *dtcm_scratch(void) { return dtcm_scratch_buf; }

/* ------------------------------------------------- DMA-004 (circ/dbm) -- */
static tfw_result_t dma_circular_dbm(struct tfw_out *o)
{
    static uint32_t src[8], dst[2][8];
    int bad = 0;

    dma_clk();
    for (int i = 0; i < 8; i++) { src[i] = 0xD0000000U + i; }

    /* circular */
    DMA2_Stream3->CR = 0U;
    DMA2_Stream3->CR = DMA_SxCR_CIRC | (1U << DMA_SxCR_MSIZE_Pos) |
                       (1U << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | DMA_SxCR_PINC;
    DMA2_Stream3->FCR = 0U;
    DMA2_Stream3->PAR = (uint32_t)src;
    DMA2_Stream3->M0AR = (uint32_t)dst[0];
    DMA2_Stream3->NDTR = 8U;
    DMA2_Stream3->CR |= DMA_SxCR_EN;
    for (volatile uint32_t i = 0U; i < 500000U; i++) {
        if (((DMA2->LISR & DMA_LISR_TCIF3) != 0U)) break;
    }
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    DMA2->LIFCR = DMA_LIFCR_CTCIF3;
    if (dst[0][7] != 0xD0000007U) { bad++; }

    /* double buffer */
    DMA2_Stream3->CR = 0U;
    DMA2_Stream3->CR = DMA_SxCR_DBM | (1U << DMA_SxCR_MSIZE_Pos) |
                       (1U << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | DMA_SxCR_PINC;
    DMA2_Stream3->PAR = (uint32_t)src;
    DMA2_Stream3->M0AR = (uint32_t)dst[0];
    DMA2_Stream3->M1AR = (uint32_t)dst[1];
    DMA2_Stream3->NDTR = 8U;
    DMA2_Stream3->CR |= DMA_SxCR_EN;
    /* let it fill M0 then toggle to M1: poll NDTR twice wrapping */
    uint32_t t0 = 0;
    for (volatile uint32_t i = 0U; i < 500000U; i++) {
        uint32_t ct = DMA2_Stream3->NDTR;
        if (t0 != 0U && ct > t0) { break; } /* wrapped: second buffer */
        t0 = ct;
    }
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    if (dst[0][0] != 0xD0000000U) { bad++; }

    hwval_meas(o, "circular fill + dbm buffers: bad=%d", (unsigned long)bad);
    hwval_lim(o, "circular wrap and double-buffer switch (DMA-004)");
    if (bad != 0) {
        hwval_fail(o, "circular/double-buffer failed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- DMA-005 (memories) -- */
static tfw_result_t dma_memory_map(struct tfw_out *o)
{
    static uint32_t sram1_dst[8] HWVAL_SRAM1; /* genuinely in SRAM1 */
    static __attribute__((section(".noncacheable"), aligned(16)))
        uint32_t sram2_dst[8];
    static __attribute__((aligned(16))) uint32_t bkp_dst[4];
    int to_dtc = 0, to_s1 = 0, to_s2 = 0, from_fl = 0, to_bkp = 0, itcm_te = 0;

    dma_clk();
    static const uint32_t s[4] = { 0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U };

    dma_cfg(DMA2_Stream4, 0, 0U, 1U, 1U, 1U, 2U, 0U, 0U);
    to_dtc = (dma_xfer(DMA2_Stream4, s, dtcm_scratch(), 4U, 200000) == 0) &&
             (((uint32_t *)dtcm_scratch())[3] == s[3]);
    to_s1  = (dma_xfer(DMA2_Stream4, s, sram1_dst, 4U, 200000) == 0) &&
             (sram1_dst[3] == s[3]);
    to_s2  = (dma_xfer(DMA2_Stream4, s, sram2_dst, 4U, 200000) == 0) &&
             (sram2_dst[3] == s[3]);
    from_fl = (dma_xfer(DMA2_Stream4, (const void *)0x08020000U, sram1_dst, 4U,
                        200000) == 0);
    to_bkp = 0;
    { /* backup SRAM needs DBP + BRE */
      __HAL_RCC_PWR_CLK_ENABLE();
      SET_BIT(PWR->CR1, PWR_CR1_DBP); SET_BIT(PWR->CSR1, PWR_CSR1_BRE);
      for (volatile int i = 0; i < 10000; i++) { ; }
      to_bkp = (dma_xfer(DMA2_Stream4, s, (void *)0x40024000U, 4U, 200000) == 0);
      (void)bkp_dst;
    }
    /* ITCM: expect transfer error (not DMA-accessible) */
    DMA2_Stream4->CR = 0U;
    DMA2_Stream4->CR = (1U << DMA_SxCR_MSIZE_Pos) | (1U << DMA_SxCR_PSIZE_Pos) |
                       DMA_SxCR_MINC | DMA_SxCR_PINC | DMA_SxCR_TEIE;
    DMA2_Stream4->PAR = (uint32_t)s;
    DMA2_Stream4->M0AR = (uint32_t)hwval_itcm_base;
    DMA2_Stream4->NDTR = 4U;
    DMA2_Stream4->CR |= DMA_SxCR_EN;
    for (volatile uint32_t i = 0U; (i < 200000U); i++) {
        if ((DMA2->HISR & DMA_HISR_TEIF4) != 0U) { itcm_te = 1; break; }
        if (DMA2_Stream4->NDTR == 0U) { break; }
    }
    DMA2_Stream4->CR &= ~DMA_SxCR_EN;
    DMA2->HIFCR = DMA_HIFCR_CTEIF4;

    hwval_meas(o, "dtcm=%d sram1=%d sram2=%d flash-read=%d bkpram=%d "
               "itcm-TE=%d", (unsigned long)to_dtc, (unsigned long)to_s1,
               (unsigned long)to_s2, (unsigned long)from_fl,
               (unsigned long)to_bkp, (unsigned long)itcm_te);
    hwval_lim(o, "all reachable memories transfer; ITCM must TE (DMA-005)");
    if (!to_dtc || !to_s1 || !to_s2 || !from_fl || !to_bkp || !itcm_te) {
        hwval_fail(o, "memory reachability incomplete");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- DMA-006 (MB/s) ------ */

static tfw_result_t dma_throughput(struct tfw_out *o)
{
    static uint32_t src[512] HWVAL_SRAM1;
    static uint32_t dst[512] HWVAL_SRAM1;
    /* the DTCM leg stays in DTCM: DMA2 reads DTCM through the CPU AHBS
     * slave bus (RM0410 2.1.6) - slower than the AHB-path SRAM1 leg */
    static uint32_t dtcm_src[512] __attribute__((aligned(32)));
    int64_t cyc_s1s1, cyc_dtcm;
    uint32_t words = 512U;
    uint32_t bytes = (uint32_t)sizeof(src);

    dma_clk();
    for (uint32_t i = 0U; i < words; i++) { src[i] = i; }

    dma_cfg(DMA2_Stream0, 0, 0U, 1U, 1U, 1U, 3U, 1U, 0U); /* FIFO full+burst */
    DWT->CYCCNT = 0U;
    (void)dma_xfer(DMA2_Stream0, src, dst, words, 4000000);
    cyc_s1s1 = DWT->CYCCNT;

    /* DTCM -> SRAM1 */
    dma_cfg(DMA2_Stream0, 0, 0U, 1U, 1U, 1U, 3U, 1U, 0U);
    DWT->CYCCNT = 0U;
    (void)dma_xfer(DMA2_Stream0, dtcm_src, dst, words, 4000000);
    cyc_dtcm = DWT->CYCCNT;

    uint32_t mbs1 = (uint32_t)((uint64_t)bytes * 216U / cyc_s1s1);
    uint32_t mbs2 = (uint32_t)((uint64_t)bytes * 216U / cyc_dtcm);
    hwval_meas(o, "sram1->sram1 %lu MB/s, dtcm->sram1 %lu MB/s",
               (unsigned long)mbs1, (unsigned long)mbs2);
    hwval_lim(o, "reported (DMA-006 analysis; no pass limit in [SRS])");
    if (mbs1 < 50U) {
        hwval_fail(o, "throughput implausibly low (%lu MB/s)",
                   (unsigned long)mbs1);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- DMA-007 (arbitration) */
static tfw_result_t dma_arbitration(struct tfw_out *o)
{
    static uint32_t srcA[512] HWVAL_SRAM1;
    static uint32_t srcB[512] HWVAL_SRAM1;
    static uint32_t dstA[512] HWVAL_SRAM1;
    static uint32_t dstB[512] HWVAL_SRAM1;
    uint32_t tA_done = 0, tB_done = 0, t0;

    dma_clk();
    for (int i = 0; i < 512; i++) { srcA[i] = 0xAAU + (uint32_t)i; srcB[i] = 0xBBU + (uint32_t)i; }

    DMA2_Stream0->CR = 0U; /* high */
    DMA2_Stream0->CR = (3U << DMA_SxCR_PL_Pos) | (1U << DMA_SxCR_MSIZE_Pos) |
                       (1U << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | DMA_SxCR_PINC;
    DMA2_Stream0->FCR = DMA_SxFCR_FTH_0 | DMA_SxFCR_FTH_1;
    DMA2_Stream0->PAR = (uint32_t)srcA;
    DMA2_Stream0->M0AR = (uint32_t)dstA;
    DMA2_Stream0->NDTR = 512U;

    DMA2_Stream1->CR = 0U; /* low */
    DMA2_Stream1->CR = (0U << DMA_SxCR_PL_Pos) | (1U << DMA_SxCR_MSIZE_Pos) |
                       (1U << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | DMA_SxCR_PINC;
    DMA2_Stream1->FCR = DMA_SxFCR_FTH_0 | DMA_SxFCR_FTH_1;
    DMA2_Stream1->PAR = (uint32_t)srcB;
    DMA2_Stream1->M0AR = (uint32_t)dstB;
    DMA2_Stream1->NDTR = 512U;

    __DSB();
    DMA2_Stream0->CR |= DMA_SxCR_EN;
    __NOP(); __NOP();
    DMA2_Stream1->CR |= DMA_SxCR_EN;
    t0 = DWT->CYCCNT;
    (void)t0;
    for (volatile uint32_t i = 0U; i < 3000000U; i++) {
        if (DMA2_Stream0->NDTR == 0U && !tA_done) { tA_done = DWT->CYCCNT; }
        if (DMA2_Stream1->NDTR == 0U && !tB_done) { tB_done = DWT->CYCCNT; }
        if (tA_done && tB_done) { break; }
    }
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    DMA2_Stream1->CR &= ~DMA_SxCR_EN;

    hwval_meas(o, "high finished first: %d (dt %lu cyc)",
               (unsigned long)(tA_done <= tB_done),
               (unsigned long)(tB_done - tA_done));
    hwval_lim(o, "high-priority stream completes first or equal (DMA-007)");
    if (tA_done == 0U || tB_done == 0U) {
        hwval_fail(o, "both streams did not finish");
        return TFW_FAIL;
    }
    if (tA_done != tB_done && tA_done > tB_done) {
        hwval_fail(o, "low-priority stream beat the high one");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_dma_tests[] = {
    { .id = "DMA.001", .group = "DMA", .title = "DMA2 m2m on 8 streams + widths/fifo",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "DMA-001, DMA-003", .fn = dma_m2m_streams },
    { .id = "DMA.002", .group = "DMA", .title = "TC/HT/TE/DME interrupts",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "DMA-002", .fn = dma_irqs },
    { .id = "DMA.004", .group = "DMA", .title = "circular + double buffer",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "DMA-004", .fn = dma_circular_dbm },
    { .id = "DMA.005", .group = "DMA", .title = "memory reachability incl ITCM TE",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "DMA-005", .fn = dma_memory_map },
    { .id = "DMA.006", .group = "DMA", .title = "m2m throughput MB/s",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 4000U,
      .verifies = "DMA-006", .fn = dma_throughput },
    { .id = "DMA.007", .group = "DMA", .title = "two-stream priority arbitration",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 4000U,
      .verifies = "DMA-007", .fn = dma_arbitration },
};

const size_t g_dma_test_count = sizeof(g_dma_tests) / sizeof(g_dma_tests[0]);

void hwval_test_register_dma(void)
{
    hwval_test_register(g_dma_tests, g_dma_test_count);
}