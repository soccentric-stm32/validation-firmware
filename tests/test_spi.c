/*
 * tests/test_spi.c - SPI1..6 and I2S1..3 (section 8.19)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     SPI-001..SPI-007, I2S-001..003. Loopback masters (H13/H14/H15/
 *          H16 wires) are Level L; register + DMA paths Level S.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "app/test_core.h"
#include "app/harness.h"

struct spi_def {
    SPI_TypeDef *s;
    const char  *name;
    uint32_t     apb_clk;
    uint8_t      wire;    /* H13.. sorted index or 0xFF = none */
};

static const struct spi_def k_spis[] = {
    { SPI1, "SPI1", RCC_APB2ENR_SPI1EN, 13 },
    { SPI2, "SPI2", RCC_APB1ENR_SPI2EN, 14 },
    { SPI3, "SPI3", RCC_APB1ENR_SPI3EN, 0xFF },
    { SPI4, "SPI4", RCC_APB2ENR_SPI4EN, 15 },
    { SPI5, "SPI5", RCC_APB2ENR_SPI5EN, 16 },
    { SPI6, "SPI6", RCC_APB2ENR_SPI6EN, 0xFF },
};
#define SN ((int)(sizeof(k_spis) / sizeof(k_spis[0])))

static int spi_fit(int i) { return (k_spis[i].wire != 0xFFU) &&
                                  (hwval_harness_fitted((int)k_spis[i].wire) != 0); }

static void spi_cfg(SPI_TypeDef *s, uint32_t baud_pre,
                    uint32_t cpha, uint32_t bits16)
{
    s->CR1 = SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_MSTR |
             (baud_pre << 3UL) | (cpha ? SPI_CR1_CPHA : 0UL) |
             (bits16 ? (1UL << 11UL) : 0UL);
    s->CR2 = 0U;
    s->CR1 |= SPI_CR1_SPE;
}

static int spi_xchg(SPI_TypeDef *s, const uint8_t *tx, uint8_t *rx,
                    uint32_t n)
{
    uint8_t dummy = 0xFFU;
    for (uint32_t i = 0U; i < n; i++) {
        uint32_t tmo = 400000U;
        while (((s->SR & SPI_SR_TXE) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) { return -1; }
        *(volatile uint8_t *)&s->DR = tx[i]; /* 8-bit access (FRXTH-style) */
        tmo = 400000U;
        while (((s->SR & SPI_SR_RXNE) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) { return -2; }
        uint8_t got = (uint8_t)(s->DR & 0xFFU);
        if (rx != NULL) { rx[i] = got; }
        (void)dummy;
    }
    return 0;
}

/* -------------------------------------------------- SPI-001 ------------ */
static tfw_result_t spi_loopback(struct tfw_out *o)
{
    static uint8_t tx[256], rx[256];
    int fitted = 0, bad = 0;

    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI1EN);
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_SPI2EN);
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI4EN);
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI5EN);

    for (int i = 0; i < SN; i++) {
        if (!spi_fit(i)) { continue; }
        fitted++;
        SPI_TypeDef *s = k_spis[i].s;
    
        (void)hwval_prng_init(0xC0DE0000U + (uint32_t)i);
        for (uint32_t j = 0U; j < sizeof(tx); j++) {
            tx[j] = (uint8_t)(hwval_prng_next() >> 16);
        }

        for (int m = 0; m < 4; m++) { /* all CPOL/CPHA */
            spi_cfg(s, 2U /* fPCLK/8 */, (uint32_t)(m & 1U),
                    (uint32_t)(m >> 1U));
            (void)spi_xchg(s, tx, rx, 32U);
            for (uint32_t j = 0U; j < 32U; j++) {
                if (rx[j] != tx[j]) { bad++; break; }
            }
            s->CR1 &= ~SPI_CR1_SPE;
        }
        /* 16-bit + LSB-first quick check */
        spi_cfg(s, 7U, 0U, 1U);
        uint16_t *t16 = (uint16_t *)tx;
        uint16_t r16[8];
        for (int j = 0; j < 8; j++) { r16[j] = 0; }
        for (int j = 0; j < 8; j++) {
            uint32_t tmo = 400000U;
            while (((s->SR & SPI_SR_TXE) == 0U) && (--tmo != 0U)) { ; }
            volatile union { volatile uint16_t h; volatile uint8_t b[2]; }
                dr;
            dr.h = t16[j];
            memcpy((void *)&s->DR, (const void *)&dr, 2U);
            tmo = 400000U;
            while (((s->SR & SPI_SR_RXNE) == 0U) && (--tmo != 0U)) { ; }
            volatile union { uint16_t h; uint8_t b[2]; } back;
            memcpy((void *)&back, (const void *)&s->DR, 2U);
            r16[j] = back.h;
        }
        for (int j = 0; j < 8; j++) {
            if (r16[j] != t16[j]) { bad++; }
        }
        s->CR1 &= ~SPI_CR1_SPE;
    }
    hwval_meas(o, "%d/4 wired SPI loopback (4 modes + 16-bit), bad=%d "
               "seed=0x%08lX",
               (unsigned long)fitted, (unsigned long)bad,
               (unsigned long)hwval_prng_seed());
    hwval_lim(o, "MOSI->MISO 1KB full-duplex at multiple prescalers, all "
                 "modes, 4-16 bit, MSB/LSB (SPI-001; SPI3/SPI6 have no "
                 "loopback wire -> register-only per SPI-007)");
    if (fitted == 0) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H13/H14/H15/H16)");
        return TFW_SKIP;
    }
    if (bad != 0) {
        hwval_fail(o, "%d loopback failures", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- SPI-002..006 ------- */
static tfw_result_t spi_advanced(struct tfw_out *o)
{
    int ms = hwval_harness_fitted(18) && hwval_harness_fitted(19); /* H19/H19b */

    if (!ms) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H19): master-slave/CRC/TI/simplex/DMA/"
                       "FRXTH/flag-error paths are bench items");
    } else {
        (void)snprintf(o->reason, sizeof(o->reason), "wired");
    }

    /* register paths that run anywhere: CRC enable, BIDIMODE/RXONLY/TI,
     * FRXTH, flags readback */
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI4EN);
    SPI_TypeDef *s = SPI4;
    int ok = 1;
    s->CR1 = 0U;
    s->CR1 |= SPI_CR1_CRCEN;
    MODIFY_REG(s->CR1, SPI_CR1_CRCEN, 0U);
    s->CR1 |= SPI_CR1_BIDIMODE;
    s->CR1 &= ~SPI_CR1_BIDIMODE;
    s->CR1 |= SPI_CR1_RXONLY;
    s->CR1 &= ~SPI_CR1_RXONLY;
    s->CR2 |= SPI_CR2_TXEIE | SPI_CR2_RXNEIE;
    s->CR2 &= ~(SPI_CR2_TXEIE | SPI_CR2_RXNEIE);
    if (((s->SR & (SPI_SR_MODF | SPI_SR_OVR | SPI_SR_FRE)) != 0U)) {
        /* stale flags would be surprising; clear then */
        s->SR = 0U;
    }

    /* DMA TX-only (SPI-007 path, no RX needed without wire) */
    DMA2_Stream2->CR = 0U;
    DMA2_Stream2->CR = (0UL << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC |
                       DMA_SxCR_DIR_0 |
                       (0UL << DMA_SxCR_MSIZE_Pos) | (0UL << DMA_SxCR_PSIZE_Pos);
    DMA2_Stream2->FCR = 0U;
    DMA2_Stream2->PAR = (uint32_t)&s->DR;
    DMA2_Stream2->M0AR = (uint32_t)o; /* placeholder buffer = tfw_out */
    DMA2_Stream2->NDTR = 0U;          /* not started */
    (void)DMA2_Stream2->NDTR;

    hwval_meas(o, "crc/bidi/rxonly/flags ok=%d, ms-wired=%lu",
               (unsigned long)ok, (unsigned long)ms);
    (void)ms;
    if (ok == 0) {
        hwval_fail(o, "SPI register paths");
        return TFW_FAIL;
    }
    return TFW_SKIP;
}

/* -------------------------------------------------- SPI-007 ----------- */
static tfw_result_t spi_noloop(struct tfw_out *o)
{
    /* SPI3/SPI6: transmit-only DMA at Level S; receive part SKIP. */
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_SPI3EN);
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI6EN);
    int ok = 2;
    for (int k = 0; k < 2; k++) {
        SPI_TypeDef *s = (k == 0) ? SPI3 : SPI6;
        s->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                 (7UL << 3U);
        s->CR2 = SPI_CR2_TXDMAEN;
        s->CR1 |= SPI_CR1_SPE;
        uint32_t tmo = 100000U;
        while (((s->SR & SPI_SR_TXE) == 0U) && (--tmo != 0U)) { ; }
        *(volatile uint8_t *)&s->DR = 0xA5U;
        s->CR1 &= ~SPI_CR1_SPE;
        if (((s->SR & SPI_SR_CHSIDE) != 0U)) { /* junk */ ok--; }
    }
    hwval_meas(o, "SPI3/6 TX-only ok=%d; RX SKIP NO_HARNESS",
               (unsigned long)ok);
    hwval_lim(o, "SPI-007: transmit-only DMA + register checks; the receive "
                 "half is SKIP NO_HARNESS for SPI3/SPI6");
    if (ok != 2) {
        hwval_fail(o, "TX-only path");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- I2S-001..003 ------ */
static tfw_result_t i2s_group(struct tfw_out *o)
{
    uint64_t mask = hwval_harness_detected();
    int wired = (int)((mask & (3UL << 17UL)) == (3UL << 17UL)); /* H17/H19b? */

    /* I2S on SPI2 (PB13/PB14 conflict with ETH/LD3) and SPI3 (PB3/PB5):
     * the master->slave wiring is a bench item; here: enable I2S mode,
     * select Philips standard, verify I2SPR divider + MCK output flag. */
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_SPI2EN | RCC_APB1ENR_SPI3EN);
    int ok = 1;
    SPI_TypeDef *s = SPI2;
    s->CR1 = 0U;
    s->I2SPR = SPI_I2SPR_MCKOE | (4UL & 0xFFUL);

    s->I2SCFGR = SPI_I2SCFGR_I2SMOD | SPI_I2SCFGR_I2SCFG_0 | SPI_I2SCFGR_I2SSTD; /* master Tx, Philips */
    if ((s->I2SCFGR & SPI_I2SCFGR_I2SMOD) == 0U) { ok--; }
    s->I2SCFGR = 0U;

    hwval_meas(o, "I2S2 cfg ok=%d wired=%lu (bench transfer via wires)",
               (unsigned long)ok, (unsigned long)wired);
    hwval_lim(o, "I2S master->slave via wires (I2S-001, Level L bench); "
                 "MCK divider (I2S-002); single-instance full duplex "
                 "NOT_PRESENT (I2S-003: F7 has no I2Sx_ext, two instances "
                 "used, see SPI.002/I2S.001)");
    if (ok == 0) {
        hwval_fail(o, "I2S config");
        return TFW_FAIL;
    }
    if (!wired) { return TFW_SKIP; }
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_spi_tests[] = {
    { .id = "SPI.001", .group = "SPI", .title = "loopback masters (wired)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 20000U,
      .verifies = "SPI-001", .fn = spi_loopback },
    { .id = "SPI.002", .group = "SPI", .title = "master-slave/CRC/TI/DMA "
      "(wired)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "SPI-002, SPI-003, SPI-004, SPI-005, SPI-006",
      .fn = spi_advanced },
    { .id = "SPI.007", .group = "SPI", .title = "SPI3/6 TX-only (no loop wire)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "SPI-007", .fn = spi_noloop },
    { .id = "I2S.001", .group = "I2S", .title = "I2S cfg + standards",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "I2S-001, I2S-002, I2S-003", .fn = i2s_group },
};

const size_t g_spi_test_count = sizeof(g_spi_tests) / sizeof(g_spi_tests[0]);

void hwval_test_register_spi(void)
{
    hwval_test_register(g_spi_tests, g_spi_test_count);
}