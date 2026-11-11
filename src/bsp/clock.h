/*
 * clock.h - system clock configuration
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     GEN-004, GEN-005
 */

#ifndef HWVAL_CLOCK_H
#define HWVAL_CLOCK_H

#include <stdint.h>

/* Configure the default clock tree of GEN-004:
 * SYSCLK 216 MHz <- PLL(VCO 432 MHz) <- HSE 8 MHz bypass (ST-LINK MCO),
 * voltage scale 1 + over-drive, 7 flash wait states, ART + prefetch on,
 * I-cache and D-cache on.
 * Blocks until ready; faults blink LD3 if the PLL does not lock. */
void hwval_clock_init(void);

/* Snapshot of the current tree, for `clock` (CLI-039) and tests. */
typedef struct {
    uint32_t sysclk_hz, hclk_hz, pclk1_hz, pclk2_hz, apb1_tim_hz, apb2_tim_hz;
    uint32_t hse_hz, hsi_hz, lsi_hz, lse_hz;
    uint32_t pllvco_hz, pllq_hz;
} hwval_clock_tree_t;

void hwval_clock_get_tree(hwval_clock_tree_t *out);

/* Switch to a specific HCLK with a given flash latency (FLS-003, CLK-013;
 * SAFE-007: always goes through HSI first). 0 on success. */
int hwval_clock_set(uint32_t hclk_hz, uint32_t ws);
/* Restore the GEN-004 default tree. */
int hwval_clock_restore_default(void);

#endif /* HWVAL_CLOCK_H */