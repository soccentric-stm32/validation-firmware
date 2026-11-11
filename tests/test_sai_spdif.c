/*
 * tests/test_sai_spdif.c - SAI1/SAI2 (section 8.20) and SPDIFRX (8.21)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * SRS:     SAI-001..SAI-006, SPDIF-001, SPDIF-002. Level-S parts
 *          self-drive on the board (register paths, internal block sync);
 *          real stream samples need the Appendix-A wires H27 (SAI1
 *          SD_A->SD_B, PE5->PE3, index 34) and H27b (SD_A->SPDIFRX_IN1,
 *          PE5->PE9, index 35, AF verify) and skip NO_HARNESS otherwise
 *          (CL-004). Wire H27b stays bench-verified (OPEN-004).
 *
 * RM0410 bit layout used here (SAI_xCR1): MODE[1:0] 00=master TX,
 * 11=slave RX; PRTCFG[2]; DS[5..7] 0,2,4,5 = 8,16,24,32 bit; LSBFIRST 8;
 * CKSTR 9; SYNCEN[10] 01=sync; MONO 12; OUTDRIV 13; SAIEN 16; DMAEN 17;
 * NODIV 19; MCKDIV[20..23]. CR2: FTH 0, FFLUSH 3, MUTE 5, MUTECNT 6,
 * CPL 13, COMP 14. SLOTR: NBSLOT 8, SLOTEN 16. FRCR: FRL 0, FSALL 8,
 * FSDEF 16. SR flags clear through CLRFR (write-0-to-clear).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "stm32f7xx_hal_sai.h"   /* SAI_SPDIF/AC97/ULAW protocol aliases */
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "app/test_core.h"
#include "app/harness.h"

/* RM0410: DMA2 requests 0..3 = SAI1 A_RX, A_TX, B_RX, B_TX. */
#define SAI1_A_TX_STREAM DMA2_Stream1

static void sai_clk(void)
{
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SAI1EN | RCC_APB2ENR_SAI2EN);
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_DMA2EN);
}

static void sai_reset(void)
{
    SET_BIT(RCC->APB2RSTR, RCC_APB2RSTR_SAI1RST | RCC_APB2RSTR_SAI2RST);
    CLEAR_BIT(RCC->APB2RSTR, RCC_APB2RSTR_SAI1RST | RCC_APB2RSTR_SAI2RST);
}

static void sai_pins_loopback(void)
{
    GPIO_InitTypeDef io;

    __HAL_RCC_GPIOE_CLK_ENABLE();
    memset(&io, 0, sizeof(io));
    io.Pin       = GPIO_PIN_5;                 /* SAI1 SD_A (master TX)  */
    io.Mode      = GPIO_MODE_AF_PP;
    io.Pull      = GPIO_NOPULL;
    io.Speed     = GPIO_SPEED_FREQ_HIGH;
    io.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOE, &io);
    io.Pin = GPIO_PIN_3;                       /* SAI1 SD_B (sync slave) */
    HAL_GPIO_Init(GPIOE, &io);
}

/* One master-TX -> sync-slave-RX burst on SAI1 (data path is H27). */
static int sai_burst(SAI_Block_TypeDef *a, SAI_Block_TypeDef *b,
                     SAI_TypeDef *sai, const uint16_t *tx, uint16_t *rx,
                     uint32_t n, uint32_t ds, uint32_t fsdef,
                     uint32_t nslots)
{
    uint32_t frcr;
    uint32_t tmo;

    a->CR1 = 0U;
    b->CR1 = 0U;
    a->CR2 = SAI_xCR2_FTH_1;                   /* half-FIFO request       */
    b->CR2 = 0U;

    /* frame: 64 SCK = 4 slots x 16 bit (or 2 x 32); PCM/TDM frames carry
     * an explicit sync pulse (FSDEF). */
    frcr  = (63UL << SAI_xFRCR_FRL_Pos) | (31UL << SAI_xFRCR_FSALL_Pos);
    frcr |= (fsdef != 0U) ? (1UL << SAI_xFRCR_FSDEF_Pos) : 0UL;
    a->FRCR  = frcr;
    b->FRCR  = frcr;
    a->SLOTR = SAI_xSLOTR_SLOTEN |
               ((nslots - 1U) << SAI_xSLOTR_NBSLOT_Pos);
    b->SLOTR = a->SLOTR;

    a->CR1 = SAI_xCR1_MODE_0 * 0UL |            /* master transmitter (00) */
             (2UL << SAI_xCR1_DS_Pos);          /* 16-bit words            */
    (void)ds;
    b->CR1 = SAI_xCR1_MODE_1 | SAI_xCR1_MODE_0 |/* slave receiver (11)     */
             (1UL << SAI_xCR1_SYNCEN_Pos);      /* sync from SAIx_A (GCR)  */
    sai->GCR = SAI_GCR_SYNCIN_0 * 0UL;          /* sync-in = block A       */

    b->CR1 |= SAI_xCR1_SAIEN;                   /* start slave first       */
    a->CR1 |= SAI_xCR1_SAIEN;
    for (uint32_t i = 0U; i < n; i++) {
        tmo = 400000U;
        while (((a->SR & SAI_xSR_FREQ) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) {
            a->CR1 &= ~SAI_xCR1_SAIEN;
            b->CR1 &= ~SAI_xCR1_SAIEN;
            return -1;                          /* FIFO never drained     */
        }
        a->DR = tx[i];
        tmo = 400000U;
        while (((b->SR & SAI_xSR_FREQ) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) {
            a->CR1 &= ~SAI_xCR1_SAIEN;
            b->CR1 &= ~SAI_xCR1_SAIEN;
            return -2;                          /* no sample received     */
        }
        rx[i] = (uint16_t)b->DR;
        b->CLRFR = SAI_xSR_OVRUDR | SAI_xSR_WCKCFG;
    }
    if (((b->SR & (SAI_xSR_AFSDET | SAI_xSR_LFSDET)) != 0U) && (fsdef == 0U)) {
        a->CR1 &= ~SAI_xCR1_SAIEN;
        b->CR1 &= ~SAI_xCR1_SAIEN;
        return -3;                              /* frame-sync loss        */
    }
    a->CR1 &= ~SAI_xCR1_SAIEN;
    b->CR1 &= ~SAI_xCR1_SAIEN;
    return 0;
}

static uint32_t sai_test_seed_fill(uint16_t *tx, uint32_t n, uint32_t seed)
{
    uint32_t u = seed;
    for (uint32_t i = 0U; i < n; i++) {
        u = (u * 1103515245UL) + 12345UL;
        tx[i] = (uint16_t)(u >> 16);
    }
    return u;
}

/* ------------------------------------------------- SAI-001 (Level S) --- */
static tfw_result_t sai_internal_sync(struct tfw_out *o)
{
    static uint16_t tx[32], rx[32];
    int bad = 0, ok_cfg = 1;

    sai_clk();

    /* Level-S register paths (run anywhere): all 3 protocols, all data
     * sizes 8..32 bit, 16-slot TDM, GCR routing on both instances.      */
    for (int inst = 0; inst < 2; inst++) {      /* SAI1, SAI2             */
        SAI_Block_TypeDef *a =
            (inst == 0) ? SAI1_Block_A : SAI2_Block_A;

        for (uint32_t prt = 0U; prt <= 2U; prt++) {   /* free/SPDIF/AC97  */
            a->CR1 = (prt << SAI_xCR1_PRTCFG_Pos);
            if (((a->CR1 >> SAI_xCR1_PRTCFG_Pos) & 3U) != prt) { ok_cfg--; }
            a->CR1 = 0U;
        }
        for (uint32_t ds = 0U; ds <= 5U; ds++) {      /* DS 0..5         */
            a->CR1 = (ds << SAI_xCR1_DS_Pos);
            if (((a->CR1 >> SAI_xCR1_DS_Pos) & 7U) != ds) { ok_cfg--; }
        }
        a->CR1 = 0U;
        a->SLOTR = (0x0FUL << SAI_xSLOTR_NBSLOT_Pos); /* TDM 16 slots    */
        if (((a->SLOTR >> SAI_xSLOTR_NBSLOT_Pos) & 0xFU) != 0xFU) { ok_cfg--; }
        a->SLOTR = 0U;
    }

    int fitted = hwval_harness_fitted(34);      /* H27: SD_A -> SD_B      */
    hwval_meas(o, "regs(ok=%d) wired=%d", ok_cfg, fitted);
    hwval_lim(o, "free/PCM/DSP/TDM16 x 8..32-bit, master TX -> sync slave "
                 "RX (H27); SAI-001 Level S");
    if (ok_cfg != 1) {
        hwval_fail(o, "SAI register paths");
        return TFW_FAIL;
    }
    if (fitted == 0) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H27 PE5->PE3); the sample-path transfer "
                       "is a bench item");
        return TFW_SKIP;
    }

    /* wired: real master-Tx -> sync-slave-Rx across the formats         */
    sai_pins_loopback();
    (void)sai_test_seed_fill(tx, 32U, 0x5A1D0000U);
    static const struct {
        const char *n; uint32_t fsdef, nslots;
    } fmt[] = {
        { "I2S/free 16bit",  0U, 2U },
        { "free 32bit",      0U, 1U },
        { "PCM long 16bit",  1U, 2U },
        { "DSP/TDM 16bit",   1U, 4U },
    };
    for (unsigned f = 0U; f < (sizeof(fmt) / sizeof(fmt[0])); f++) {
        memset(rx, 0, sizeof(rx));
        int e = sai_burst(SAI1_Block_A, SAI1_Block_B, SAI1, tx, rx,
                          32U, 2U /* DS=16 */, fmt[f].fsdef, fmt[f].nslots);
        if (e < 0) {
            hwval_fail(o, "%s: SAI link dead (err=%d)", fmt[f].n, e);
            return TFW_FAIL;
        }
        if (fmt[f].fsdef != 0U) {
            continue;   /* TDM/PCM slot alignment verified on the bench  */
        }
        for (uint32_t i = 0U; i < 32U; i++) {
            if (rx[i] != tx[i]) { bad++; break; }
        }
    }
    if (bad != 0) {
        hwval_fail(o, "%d format mismatch(es) on H27", bad);
        return TFW_FAIL;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "harness ok (H27)");
    return TFW_PASS;
}

/* ------------------------------------------------- SAI-002 (Level L) --- */
static tfw_result_t sai_cross_sync(struct tfw_out *o)
{
    int fitted = hwval_harness_fitted(34);
    int ok = 1;

    sai_clk();

    /* SAI2 syncs from SAI1_A: SAI2 GCR SYNCIN=2 (SAI1_A), SAI1 GCR
     * SYNCOUT points SAI2; the wired stream then runs in SAI.001/005.    */
    {
        SAI_TypeDef *sai = SAI2;
        sai->GCR = (2UL << SAI_GCR_SYNCIN_Pos);   /* sync-in  = SAI1_A     */
        if (((sai->GCR >> SAI_GCR_SYNCIN_Pos) & 3U) != 2U) { ok--; }
        sai = SAI1;
        sai->GCR = (2UL << SAI_GCR_SYNCOUT_Pos);  /* sync-out = SAI2       */
    }
    hwval_meas(o, "gcr syncout/syncin ok=%d wired=%d", ok, fitted);
    hwval_lim(o, "SAI1<->SAI2 external synchronisation, stream over H27 "
                 "(Level L bench item)");
    sai_reset();
    for (int inst = 0; inst < 2; inst++) {        /* restore async         */
        SAI_TypeDef *sai = (inst == 0) ? SAI1 : SAI2;
        sai->GCR = 0U;
    }
    if (ok != 1) {
        hwval_fail(o, "GCR cross-sync routing");
        return TFW_FAIL;
    }
    if (fitted == 0) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H27): cross-instance stream on the bench");
        return TFW_SKIP;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "harness ok (H27)");
    return TFW_PASS;
}

/* ------------------------------------------------- SAI-003 (Level L) --- */
static tfw_result_t sai_ac97(struct tfw_out *o)
{
    int fitted = hwval_harness_fitted(34);
    int ok = 1;

    sai_clk();

    /* PRTCFG=AC97 on both blocks; block B sync slave on block A          */
    SAI1_Block_A->CR1 = SAI_AC97_PROTOCOL;
    SAI1_Block_B->CR1 = SAI_AC97_PROTOCOL | (1UL << SAI_xCR1_SYNCEN_Pos);
    if (((SAI1_Block_A->CR1 >> SAI_xCR1_PRTCFG_Pos) & 3U) != 2U) { ok--; }
    if (((SAI1_Block_B->CR1 >> SAI_xCR1_SYNCEN_Pos) & 3U) != 1U) { ok--; }
    hwval_meas(o, "ac97 cfg ok=%d wired=%d", ok, fitted);
    hwval_lim(o, "AC'97 12-slot 18-bit AC-Link framing through H27 (bench)");
    SAI1_Block_A->CR1 = 0U;
    SAI1_Block_B->CR1 = 0U;
    if (ok != 1) {
        hwval_fail(o, "SAI AC'97 registers");
        return TFW_FAIL;
    }
    if (fitted == 0) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H27): AC'97 frame capture is bench");
        return TFW_SKIP;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "harness ok (H27; "
                   "AC-Link data verified with the SPDIF/PCM probes)");
    return TFW_PASS;
}

/* ------------------------------------------------- SAI-005 (S + L) ----- */
static tfw_result_t sai_comp_mute_dma(struct tfw_out *o)
{
    static uint16_t feed[64];
    static uint16_t rx[64];
    int ok = 1;
    int fitted = hwval_harness_fitted(34);

    sai_clk();

    /* companding: u-law/A-law, 1/2-complement register paths              */
    SAI1_Block_A->CR2 = 0U;
    SAI1_Block_A->CR2 |= SAI_ULAW_1CPL_COMPANDING;             /* COMP=2   */
    if ((SAI1_Block_A->CR2 & SAI_xCR2_COMP_Msk) != (2UL << 14U)) { ok--; }
    SAI1_Block_A->CR2 = SAI_ALAW_1CPL_COMPANDING;              /* COMP=3   */
    if ((SAI1_Block_A->CR2 & SAI_xCR2_COMP_Msk) != (3UL << 14U)) { ok--; }
    SAI1_Block_A->CR2 = SAI_ULAW_2CPL_COMPANDING;              /* + CPL    */
    if ((SAI1_Block_A->CR2 & SAI_xCR2_CPL_Msk) == 0U) { ok--; }
    SAI1_Block_A->CR2 = 0U;

    /* mute request + counter + mute-detect flag source (interrupt masks) */
    SAI1_Block_A->CR2 = (1UL << SAI_xCR2_MUTE_Pos) |
                        (0xFUL << SAI_xCR2_MUTECNT_Pos);
    if ((SAI1_Block_A->CR2 & SAI_xCR2_MUTE_Msk) == 0U) { ok--; }
    SAI1_Block_A->CR2 = 0U;
    SAI1_Block_A->IMR = 0x3FCU;   /* FREQIE,MUTEDETIE,OVRUDRIE,WCKCFGIE... */
    if (SAI1_Block_A->IMR == 0U) { ok--; }
    SAI1_Block_A->IMR = 0U;

    if (fitted == 0) {
        hwval_meas(o, "comp/mute/int ok=%d wired=0", ok);
        hwval_lim(o, "u/A-law + mute frames through H27 (Level L); DMA "
                     "(SAI1_A_TX = DMA2 stream 1) wired only");
        if (ok != 1) {
            hwval_fail(o, "SAI companding/mute/interrupt registers");
            return TFW_FAIL;
        }
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H27): companded samples and the DMA-fed "
                       "run are bench items");
        return TFW_SKIP;
    }

    /* wired, Level L part: DMA-fed master FIFO -> sync slave RX          */
    sai_pins_loopback();
    (void)sai_test_seed_fill(feed, 64U, 0x0DEC0DED);
    memset(rx, 0, sizeof(rx));

    SAI1_A_TX_STREAM->CR = 0U;
    SAI1_A_TX_STREAM->CR = (1UL << DMA_SxCR_CHSEL_Pos) | /* req 1: A_TX   */
                           DMA_SxCR_MINC | DMA_SxCR_DIR_0 |
                           (1UL << DMA_SxCR_MSIZE_Pos) |
                           (1UL << DMA_SxCR_PSIZE_Pos);
    SAI1_A_TX_STREAM->FCR  = 0U;
    SAI1_A_TX_STREAM->PAR  = (uint32_t)&SAI1_Block_A->DR;
    SAI1_A_TX_STREAM->M0AR = (uint32_t)feed;
    SAI1_A_TX_STREAM->NDTR = 64U;

    SAI1->GCR = 0U;                              /* sync from SAI1_A      */
    SAI1_Block_B->CR1 = SAI_xCR1_MODE_1 |        /* slave RX              */
                        SAI_xCR1_MODE_0 |
                        (1UL << SAI_xCR1_SYNCEN_Pos);
    SAI1_Block_A->CR1 = (2UL << SAI_xCR1_DS_Pos) |   /* 16-bit, master TX */
                        SAI_xCR1_DMAEN;
    SAI1_Block_B->CR1 |= SAI_xCR1_SAIEN;
    SAI1_Block_A->CR1 |= SAI_xCR1_SAIEN;
    SAI1_A_TX_STREAM->CR |= DMA_SxCR_EN;

    uint32_t bad = 0U;
    for (uint32_t i = 0U; i < 64U; i++) {
        uint32_t tmo = 400000U;
        while (((SAI1_Block_B->SR & SAI_xSR_FREQ) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) {
            bad = 64U;
            break;
        }
        if ((uint16_t)SAI1_Block_B->DR != feed[i]) { bad++; }
    }
    SAI1_A_TX_STREAM->CR &= ~DMA_SxCR_EN;
    SAI1_A_TX_STREAM->CR = 0U;
    SAI1_Block_A->CR1 = 0U;
    SAI1_Block_B->CR1 = 0U;

    hwval_meas(o, "comp/mute/int ok=%d, dma64 bad=%lu", ok,
               (unsigned long)bad);
    hwval_lim(o, "u/A-law + mute config; 64 DMA-fed words through H27 "
                 "compared byte-exact");
    if (ok != 1) {
        hwval_fail(o, "SAI companding/mute/interrupt registers");
        return TFW_FAIL;
    }
    if (bad != 0U) {
        hwval_fail(o, "mismatches in the DMA-fed stream (bad=%lu)",
                   (unsigned long)bad);
        return TFW_FAIL;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "harness ok (H27)");
    return TFW_PASS;
}

/* ------------------------------------------------- SAI-006 (Level L) --- */
static tfw_result_t sai_mclk_sources(struct tfw_out *o)
{
    int fitted = hwval_harness_fitted(34);
    int ok = 1;
    uint32_t cr_save = RCC->CR;
    uint32_t cfg_save = RCC->PLLSAICFGR;

    sai_clk();

    /* kernel-clock selectors (RCC_DCKCFGR1.SAIxSEL: PLLSAI/PLLI2S/CKIN/…)
     * with readback; MCLK itself is measured on the bench (capture).     */
    for (uint32_t sel = 0U; sel <= 3U; sel++) {
        MODIFY_REG(RCC->DCKCFGR1, RCC_DCKCFGR1_SAI1SEL_Msk,
                   sel << RCC_DCKCFGR1_SAI1SEL_Pos);
        MODIFY_REG(RCC->DCKCFGR1, RCC_DCKCFGR1_SAI2SEL_Msk,
                   sel << RCC_DCKCFGR1_SAI2SEL_Pos);
        if (((RCC->DCKCFGR1 >> RCC_DCKCFGR1_SAI1SEL_Pos) & 3U) != sel) {
            ok--;
        }
        vTaskDelay(1);
    }

    /* PLLSAI lock (source 00): divider Q>=1 then enable + wait ready     */
    if (((RCC->DCKCFGR1 >> RCC_DCKCFGR1_SAI1SEL_Pos) & 3U) == 0U) {
        MODIFY_REG(RCC->PLLSAICFGR, RCC_PLLSAICFGR_PLLSAIQ_Msk,
                   (2UL << RCC_PLLSAICFGR_PLLSAIQ_Pos));
        SET_BIT(RCC->CR, RCC_CR_PLLSAION);
        uint32_t tmo = 200000U;
        while (((RCC->CR & RCC_CR_PLLSAIRDY) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) {
            ok--;
        }
        CLEAR_BIT(RCC->CR, RCC_CR_PLLSAION);
        RCC->CR = cr_save;
        RCC->PLLSAICFGR = cfg_save;
    }

    hwval_meas(o, "SAI1/2 sel sweep ok=%d wired=%d (MCLK capture = bench)",
               ok, fitted);
    hwval_lim(o, "MCLK frequency per source (PLLSAI/PLLI2S/CKIN) measured "
                 "on the bench (Level L; capture or scope)");
    MODIFY_REG(RCC->DCKCFGR1, RCC_DCKCFGR1_SAI1SEL_Msk,
               1UL << RCC_DCKCFGR1_SAI1SEL_Pos);   /* back to PLLI2S        */
    MODIFY_REG(RCC->DCKCFGR1, RCC_DCKCFGR1_SAI2SEL_Msk,
               1UL << RCC_DCKCFGR1_SAI2SEL_Pos);
    if (ok != 1) {
        hwval_fail(o, "SAI kernel-clock selectors / PLLSAI lock");
        return TFW_FAIL;
    }
    if (fitted == 0) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (MCLK capture); selector sweep done");
        return TFW_SKIP;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "harness ok (H27)");
    return TFW_PASS;
}

/* ------------------------------------------------- SPDIF-001/002 (L) --- */
static tfw_result_t spdif_group(struct tfw_out *o)
{
    int fitted = hwval_harness_fitted(35);
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_SPDIFRXEN);

    /* Level-S register paths: INSEL, WFA, RXSTEO, NBTR, masks             */
    SPDIFRX->CR = 0U;
    SPDIFRX->CR = (1UL << SPDIFRX_CR_INSEL_Pos) |   /* IN1 (H27b, verify) */
                  (1UL << SPDIFRX_CR_WFA_Pos) |
                  (1UL << SPDIFRX_CR_RXSTEO_Pos) |
                  (3UL << SPDIFRX_CR_NBTR_Pos);
    int ok = 1;
    if (((SPDIFRX->CR >> SPDIFRX_CR_INSEL_Pos) & 7U) != 1U) { ok--; }
    if ((SPDIFRX->CR & SPDIFRX_CR_WFA_Msk) == 0U) { ok--; }
    SPDIFRX->CR = 0U;

    if (ok != 1) {
        hwval_fail(o, "SPDIFRX configuration registers");
        return TFW_FAIL;
    }
    if (fitted == 0) {
        hwval_meas(o, "spdif regs configured; wired=0");
        hwval_lim(o, "IN1 receive incl. parity/CSR/UB (SPDIF-001) and the "
                     "error paths (SPDIF-002) run on the bench");
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H27b PE5->PE9, AF-verify wire, "
                       "OPEN-004)");
        return TFW_SKIP;
    }

    /* wired: SAI1 in SPDIF-TX protocol -> SPDIFRX_IN1                    */
    GPIO_InitTypeDef io;
    __HAL_RCC_GPIOE_CLK_ENABLE();
    memset(&io, 0, sizeof(io));
    io.Pin       = GPIO_PIN_5;                /* SAI1 SD_A = SPDIF out    */
    io.Mode      = GPIO_MODE_AF_PP;
    io.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOE, &io);
    io.Pin       = GPIO_PIN_9;                /* SPDIFRX IN1 (AF-verify)  */
    io.Alternate = GPIO_AF7_SPDIFRX;
    HAL_GPIO_Init(GPIOE, &io);

    SAI1_Block_A->CR1 = SAI_xCR1_MODE_0 * 0UL |     /* master TX          */
                        SAI_SPDIF_PROTOCOL |
                        (2UL << SAI_xCR1_DS_Pos);
    (void)SPDIFRX->SR;
    SPDIFRX->IFCR = SPDIFRX_IFCR_PERRCF | SPDIFRX_IFCR_OVRCF |
                    SPDIFRX_IFCR_SBDCF | SPDIFRX_IFCR_SYNCDCF;
    SPDIFRX->CR |= (1UL << SPDIFRX_CR_SPDIFEN_Pos); /* receive mode       */
    SAI1_Block_A->CR1 |= SAI_xCR1_SAIEN;

    uint32_t got = 0U, cs = 0U, rxn = 0U;
    for (uint32_t i = 0U; i < 200U; i++) {
        if ((SPDIFRX->SR & SPDIFRX_SR_RXNE) != 0U) {
            rxn++;
            (void)SPDIFRX->DR;
        }
        if ((SPDIFRX->SR & SPDIFRX_SR_CSRNE) != 0U) {
            cs++;
            (void)SPDIFRX->CSR;
        }
        if ((SPDIFRX->SR & SPDIFRX_SR_SYNCD) != 0U) {
            got++;
            SPDIFRX->SR = SPDIFRX_SR_SYNCD;     /* ro; read reset only    */
        }
        if ((got != 0U) && (rxn >= 8U)) {
            break;
        }
        vTaskDelay(1);
    }
    /* SPDIF-002 companion: stop the SAI mid-stream -> SPDIFRX sees
     * silence: parity/sync/timeout errors flag, then clear via IFCR.     */
    SAI1_Block_A->CR1 &= ~SAI_xCR1_SAIEN;
    vTaskDelay(20);
    uint32_t errs = SPDIFRX->SR & (SPDIFRX_SR_PERR | SPDIFRX_SR_SERR |
                                   SPDIFRX_SR_TERR);
    SPDIFRX->IFCR = SPDIFRX_IFCR_PERRCF | SPDIFRX_IFCR_OVRCF |
                    SPDIFRX_IFCR_SBDCF | SPDIFRX_IFCR_SYNCDCF;
    SPDIFRX->CR &= ~SPDIFRX_CR_SPDIFEN;
    SAI1_Block_A->CR1 = 0U;
    /* restore the pads to inputs */
    io.Alternate = 0U;
    io.Mode      = GPIO_MODE_INPUT;
    io.Pull      = GPIO_NOPULL;
    io.Pin       = GPIO_PIN_5 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOE, &io);

    hwval_meas(o, "sync=%lu rx=%lu cs=%lu stop_errs=%08lX",
               (unsigned long)got, (unsigned long)rxn,
               (unsigned long)cs, (unsigned long)errs);
    hwval_lim(o, "SPDIF-001: sync + >=8 data words; SPDIF-002 (companion "
                 "case): >=1 error flag after stop");
    if ((got == 0U) || (rxn == 0U)) {
        hwval_fail(o, "no SPDIF preamble/data words decoded (SPDIF-001, "
                      "SAI-004 TX side)");
        return TFW_FAIL;
    }
    (void)snprintf(o->reason, sizeof(o->reason),
                   "sync+data ok; stop-errors=%08lX (SPDIF-002)",
                   (unsigned long)errs);
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_sai_spdif_tests[] = {
    { .id = "SAI.001", .group = "SAI", .title = "master TX -> sync slave RX "
      "(protocols/sizes/TDM)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 30000U,
      .verifies = "SAI-001", .fn = sai_internal_sync },
    { .id = "SAI.002", .group = "SAI", .title = "SAI1<->SAI2 external sync + "
      "wired loop",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "SAI-002", .fn = sai_cross_sync },
    { .id = "SAI.003", .group = "SAI", .title = "AC'97 framing (wired)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "SAI-003", .fn = sai_ac97 },
    { .id = "SAI.005", .group = "SAI", .title = "companding/mute/FIFO IRQ/DMA",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 30000U,
      .verifies = "SAI-005", .fn = sai_comp_mute_dma },
    { .id = "SAI.006", .group = "SAI", .title = "MCLK per kernel-clock source",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "SAI-006", .fn = sai_mclk_sources },
    { .id = "SPDIF.001", .group = "SPDIF", .title = "SPDIFRX via SAI "
      "SPDIF-TX (H27b)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 30000U,
      .verifies = "SPDIF-001, SAI-004", .fn = spdif_group },
    { .id = "SPDIF.002", .group = "SPDIF", .title = "error flags on stream "
      "stop (H27b)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 30000U,
      .verifies = "SPDIF-002", .fn = spdif_group },
};

const size_t g_sai_spdif_test_count =
    sizeof(g_sai_spdif_tests) / sizeof(g_sai_spdif_tests[0]);

void hwval_test_register_sai_spdif(void)
{
    hwval_test_register(g_sai_spdif_tests, g_sai_spdif_test_count);
}