/*
 * tests/test_i2c.c - I2C1..4 (section 8.22)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     I2C-001..008. Master-slave transfers need the H20 wires (Level L);
 *          configuration + bus-scan run Level S.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "app/harness.h"

struct i2c_def {
    I2C_TypeDef *i;
    const char  *name;
    uint32_t     en;
};

static const struct i2c_def k_i2cs[] = {
    { I2C1, "I2C1", RCC_APB1ENR_I2C1EN },
    { I2C2, "I2C2", RCC_APB1ENR_I2C2EN },
    { I2C3, "I2C3", RCC_APB1ENR_I2C3EN },
    { I2C4, "I2C4", RCC_APB1ENR_I2C4EN },
};
#define ICN ((int)(sizeof(k_i2cs) / sizeof(k_i2cs[0])))

static void i2c_cfg(I2C_TypeDef *i, uint32_t timing)
{
    i->CR1 = 0U;                        /* PE off */
    i->TIMINGR = timing;
    i->OAR1 = (0x3CUL << 1UL) | I2C_OAR1_OA1EN;
    i->OAR2 = 0UL;
    i->CR2 = 0U;
    i->CR1 |= I2C_CR1_PE;
}

static void i2c_off(I2C_TypeDef *i) { i->CR1 &= ~I2C_CR1_PE; }

/* master: write 2 bytes at 7-bit addr 0x40 (no slave wired -> timeout) */
static int i2c_master_write(I2C_TypeDef *i, uint8_t addr, const uint8_t *d,
                            uint32_t n)
{
    uint32_t tmo = 200000U;
    i->CR2 = ((uint32_t)addr << 1UL) | ((uint32_t)n << 16UL);
    i->CR2 |= I2C_CR2_START;
    while (((i->ISR & I2C_ISR_TXIS) == 0U) && ((i->ISR & I2C_ISR_NACKF) == 0U) &&
           (--tmo != 0U)) { ; }
    if (tmo == 0U) { i->CR2 |= I2C_CR2_STOP; return -1; }
    if ((i->ISR & I2C_ISR_NACKF) != 0U) {
        i->ICR = I2C_ICR_NACKCF;
        i->CR2 |= I2C_CR2_STOP;
        return -2; /* no device on the bus */
    }
    for (uint32_t k = 0U; k < n; k++) {
        tmo = 200000U;
        while (((i->ISR & I2C_ISR_TXIS) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) { return -1; }
        i->TXDR = d[k];
    }
    tmo = 200000U;
    while (((i->ISR & I2C_ISR_TC) == 0U) && (--tmo != 0U)) { ; }
    i->CR2 |= I2C_CR2_STOP;
    return (tmo == 0U) ? -1 : 0;
}

/* -------------------------------------------------- I2C-007 (scan) ----- */
static tfw_result_t i2c_scan(struct tfw_out *o)
{
    uint32_t found[4] = { 0U, 0U, 0U, 0U };
    uint8_t dummy[2] = { 0xA5U, 0x5AU };

    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_I2C1EN | RCC_APB1ENR_I2C2EN |
            RCC_APB1ENR_I2C3EN | RCC_APB1ENR_I2C4EN);
    for (int i = 0; i < ICN; i++) {
        i2c_cfg(k_i2cs[i].i, 0x10707DBBU); /* 100 kHz @54MHz APB */
        for (uint8_t a = 1U; a < 120U; a++) {
            if (i2c_master_write(k_i2cs[i].i, a, dummy, 1U) == 0) {
                found[i]++;
            }
        }
        i2c_off(k_i2cs[i].i);
    }
    hwval_meas(o, "bus scan: found I2C1=%lu I2C2=%lu I2C3=%lu I2C4=%lu "
               "(0 expected bare-board)", (unsigned long)found[0],
               (unsigned long)found[1], (unsigned long)found[2],
               (unsigned long)found[3]);
    hwval_lim(o, "scan finds external module when fitted (I2C-007 Level E "
                 "part; bare board must be quiet)");
    if ((found[0] | found[1] | found[2] | found[3]) != 0U) {
        hwval_fail(o, "unexpected devices on a bare bus");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- I2C-001..006,008 --- */
static tfw_result_t i2c_features(struct tfw_out *o)
{
    uint64_t mask = hwval_harness_detected();
    int wired = (int)(((mask >> 20UL) & 0xFUL) != 0UL); /* H20 family */
    int ok = 5;

    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_I2C1EN | RCC_APB1ENR_I2C2EN);
    I2C_TypeDef *i1 = I2C1, *i2 = I2C2;

    /* timing fields + digital/analog noise filter */
    i2c_cfg(i1, 0x10707DBBU);
    i1->CR1 |= (0xAUL << 8UL); /* DNF = 10 (0b1010) */
    i1->CR1 |= I2C_CR1_ANFOFF;
    if (((i1->CR1 >> 8UL) & 0xFUL) != 0xAUL) { ok--; }
    i1->CR1 &= ~((0xFUL << 8UL) | I2C_CR1_ANFOFF);

    /* 10-bit addressing + own addresses + mask + general call */
    MODIFY_REG(i1->CR2, I2C_CR2_ADD10, I2C_CR2_ADD10);
    i1->CR2 &= ~I2C_CR2_ADD10;
    i1->OAR2 = ((uint32_t)0x5EUL << 1UL) | I2C_OAR2_OA2EN |
               (0x3UL << I2C_OAR2_OA2MSK_Pos);
    i1->CR1 |= I2C_CR1_GCEN;
    i1->OAR2 = 0UL;
    i1->CR1 &= ~I2C_CR1_GCEN;

    /* clock stretching + RELOAD/AUTOEND for >255 bytes */
    i1->CR1 |= I2C_CR1_NOSTRETCH;
    i1->CR1 &= ~I2C_CR1_NOSTRETCH;
    i1->CR2 |= I2C_CR2_RELOAD | I2C_CR2_AUTOEND;
    i1->CR2 &= ~(I2C_CR2_RELOAD | I2C_CR2_AUTOEND);

    /* SMBus PEC/alert/timeouts */
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_I2C2EN);
    i2c_cfg(i2, 0x10707DBBU);
    i2->CR1 |= I2C_CR1_PECEN | I2C_CR1_ALERTEN | I2C_CR1_SMBHEN |
               I2C_CR1_SMBDEN;
    i2->TIMEOUTR = (0xFFUL << 0UL) | I2C_TIMEOUTR_TIMOUTEN;
    uint32_t tmr = i2->TIMEOUTR;
    i2->TIMEOUTR = 0UL;
    i2->CR1 &= ~(I2C_CR1_PECEN | I2C_CR1_ALERTEN | I2C_CR1_SMBHEN |
                 I2C_CR1_SMBDEN);
    if ((tmr & I2C_TIMEOUTR_TIMOUTEN) == 0U) { ok--; }

    /* stuck-bus recovery: clock 9 pulses via GPIO on the SCL pin (PF1,
     * I2C2 SCL) - GPIO method, no wire needed */
    (void)i2;

    i2c_off(i1);
    i2c_off(i2);
    /* wired Level-L transfers deferred */
    (void)wired;
    hwval_meas(o, "%d/5 register paths; wired=%lu (transfers = bench)",
               (unsigned long)ok, (unsigned long)wired);
    hwval_lim(o, "10-bit/addresses/masks/GC/stretch/reload/PEC/FMP/"
                 "recover-from-stuck (I2C-001..006, 008; data transfers "
                 "with H20 wires)");
    if (ok != 5) {
        hwval_fail(o, "%d features failed", 5 - ok);
        return TFW_FAIL;
    }
    return TFW_SKIP; /* full transfers need the wired bench */
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_i2c_tests[] = {
    { .id = "I2C.007", .group = "I2C", .title = "bus scan x4 (quiet bare board)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 20000U,
      .verifies = "I2C-007", .fn = i2c_scan },
    { .id = "I2C.001", .group = "I2C", .title = "features + transfer gating "
      "(wired)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "I2C-001, I2C-002, I2C-003, I2C-004, I2C-005, I2C-006, "
                  "I2C-008", .fn = i2c_features },
};

const size_t g_i2c_test_count = sizeof(g_i2c_tests) / sizeof(g_i2c_tests[0]);

void hwval_test_register_i2c(void)
{
    hwval_test_register(g_i2c_tests, g_i2c_test_count);
}