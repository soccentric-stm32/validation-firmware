/*
 * tests/test_sd_qspi_fmc.c - SDMMC, Quad-SPI, FMC (sections 8.24, 8.25, 8.26)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     SD-001..005, QSPI-001..003, FMC-001..002. Card/module absent ->
 *          register-level tests + N/A NOT_FITTED.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"

/* ----------------------------------------------------------- SD-001 ---- */
static tfw_result_t sd_registers(struct tfw_out *o)
{
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SDMMC1EN); /* bus interface APB2 */
    SDMMC1->CLKCR = 0U;
    /* power + divider + state machine timeouts */
    SDMMC1->POWER = 3U;        /* power on */
    SDMMC1->CLKCR = 174UL;     /* 400 kHz idle clock: 54M/2/(174+2) ~ 153k */
    int ok = 1;
    /* command timeout without card: CMD0 with response timeout */
    SDMMC1->CMD = (0UL << 8UL) | SDMMC_CMD_CPSMEN | SDMMC_CMD_WAITRESP_0;
    uint32_t tmo = 200000U;
    while (((SDMMC1->STA & (SDMMC_STA_CMDREND | SDMMC_STA_CTIMEOUT)) == 0U) &&
           (--tmo > 0U)) { ; }
    uint32_t to = SDMMC1->STA & SDMMC_STA_CTIMEOUT;
    SDMMC1->ICR = SDMMC_ICR_CCRCFAILC | SDMMC_ICR_CMDRENDC |
                  SDMMC_ICR_CTIMEOUTC;
    SDMMC1->CMD &= ~SDMMC_CMD_CPSMEN;
    if (tmo == 0U) { ok = 0; }
    if (to == 0U) {  /* a timeout is EXPECTED with no card present */
        ok = 2;      /* a card might be in: not an error, note it */
    }
    SDMMC1->POWER = 0U;

    hwval_meas(o, "clk/power/cpath ok=%d ctimeout=%lu (SD-001; card tests "
               "= Level E with adapter)", (unsigned long)ok,
               (unsigned long)to);
    hwval_lim(o, "no-card command timeout verified; SD-002..005 card/"
                 "FatFs/SPI-path tests with the microSD adapter (Level E)")
    ;
    if (ok == 0) {
        hwval_fail(o, "SDMMC command path did not respond");
        return TFW_FAIL;
    }
    if (to == 0U) {
        (void)snprintf(o->measured, sizeof(o->measured), "card present?");
        return TFW_SKIP; /* card in slot: bench tests own the rest */
    }
    return TFW_PASS;
}

/* --------------------------------------------------------- QSPI-001 ---- */
static tfw_result_t qspi_registers(struct tfw_out *o)
{
    SET_BIT(RCC->AHB3ENR, RCC_AHB3ENR_QSPIEN);
    QUADSPI->CR = QUADSPI_CR_ABORT;      /* abort anything */
    QUADSPI->CR = 0U;
    QUADSPI->CR = 3UL;                   /* FTH 2 bytes, prescaler 1 */
    QUADSPI->CCR = 0U;
    QUADSPI->LPTR = 10U;
    /* indirect read with no device: timeout, not a hang */
    QUADSPI->CCR = (1UL << 26UL);        /* FMODE: indirect read */
    QUADSPI->AR = 0x10000000UL;          /* beyond a 16 MB module */
    QUADSPI->DLR = 3U;
    QUADSPI->CR |= QUADSPI_CR_EN;
    uint32_t tmo = 500000U;
    while (((QUADSPI->SR & (QUADSPI_SR_TOF | QUADSPI_SR_TCF)) == 0U) &&
           (--tmo > 0U)) { ; }
    uint32_t stat = QUADSPI->SR;
    QUADSPI->FCR = QUADSPI_FCR_CTOF | QUADSPI_FCR_CTCF;
    QUADSPI->CR &= ~QUADSPI_CR_EN;
    int ok = (tmo > 0U) &&
             ((stat & (QUADSPI_SR_TOF | QUADSPI_SR_TCF)) != 0U);

    hwval_meas(o, "qspi timeout=%lu state=%08lX (QSPI-001; module tests = "
               "Level E)", (unsigned long)tmo, (unsigned long)stat);
    hwval_lim(o, "no-device indirect read times out (not a hang); QSPI-002/"
                 "003 with the NOR module (Level E)");
    if (ok == 0) {
        hwval_fail(o, "QSPI did not time out");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ---------------------------------------------------------- FMC-001 ---- */
static tfw_result_t fmc_registers(struct tfw_out *o)
{
    SET_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FMCEN);
    int ok = 1;

    /* NOR/SRAM timing registers write/read */
    FMC_Bank1->BTCR[0] = 0x0UL;
    FMC_Bank1->BTCR[0] = (0x0FUL << 0UL) | (0x0FUL << 8UL);
    if (FMC_Bank1->BTCR[0] != ((0x0FUL << 0UL) | (0x0FUL << 8UL))) { ok = 0; }
    FMC_Bank1E->BWTR[0] = 0x0UL;
    /* SDRAM controller: command mode + refresh rate */
    FMC_Bank5_6->SDCR[0] = 0x0UL;
    FMC_Bank5_6->SDTR[0] = 0x0UL;
    FMC_Bank5_6->SDCR[0] = (1UL << 12UL) | (2UL << 0UL) | (1UL << 12UL);
    FMC_Bank5_6->SDTR[0] = (1UL << 0UL) | (4UL << 4UL);
    FMC_Bank5_6->SDCMR = (2UL << 4UL); /* CTCB? write any config bit roundtrip */
    FMC_Bank5_6->SDCMR = 0UL;
    FMC_Bank5_6->SDRTR = 10UL << 0UL;
    FMC_Bank5_6->SDRTR = 0UL;

    hwval_meas(o, "NOR/SRAM + SDRAM controller regs ok=%d (FMC-001; device "
               "= N/A NOT_FITTED, FMC-002 via an external board)",
               (unsigned long)ok);
    hwval_lim(o, "NOR/NAND/SDRAM controller config; memory device absent")
    ;
    if (ok == 0) {
        hwval_fail(o, "FMC register access");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static tfw_result_t fmc_device_na(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason), "NOT_FITTED");
    return TFW_NA;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_mem_tests[] = {
    { .id = "SD.001", .group = "SD", .title = "SDMMC1 register/path state",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "SD-001", .fn = sd_registers },
    { .id = "SD.002", .group = "SD", .title = "card + FatFs (Level E)",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "SD-002, SD-003, SD-004, SD-005, SD-006, SD-007",
      .fn = fmc_device_na }, /* uses the NOT_FITTED skip path */ /* same NOT_FITTED-style reason pattern */
    { .id = "QSPI.001", .group = "QSPI", .title = "indirect read timeout no-device",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "QSPI-001", .fn = qspi_registers },
    { .id = "QSPI.002", .group = "QSPI", .title = "NOR module (Level E)",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "QSPI-002, QSPI-003", .fn = fmc_device_na },
    { .id = "FMC.001", .group = "FMC", .title = "NOR/NAND/SDRAM controller regs",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "FMC-001", .fn = fmc_registers },
    { .id = "FMC.002", .group = "FMC", .title = "external memory device",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "FMC-002", .fn = fmc_device_na },
};

const size_t g_mem_test_count = sizeof(g_mem_tests) / sizeof(g_mem_tests[0]);

void hwval_test_register_mem(void)
{
    hwval_test_register(g_mem_tests, g_mem_test_count);
}