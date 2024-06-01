/**
 * @file    clock.h
 * @brief   System clock tree configuration and query.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     GEN-004, GEN-005
 */

#ifndef HWVAL_CLOCK_H
#define HWVAL_CLOCK_H

#include <stdint.h>

/**
 * @defgroup bsp_clock Clock tree
 * @brief GEN-004 default clock tree and test-time frequency switching.
 * @{
 */

/**
 * @brief Configure the GEN-004 default clock tree.
 * @details SYSCLK 216 MHz from PLL (VCO 432 MHz) on HSE 8 MHz bypass
 *          (ST-LINK MCO); voltage scale 1 + over-drive, 7 flash wait states,
 *          ART + prefetch, I- and D-cache on. Blocks until ready.
 * @warning Hangs in a NOP loop if the PLL or over-drive fails.
 */
void hwval_clock_init(void);

/** @brief Snapshot of the current clock tree, for `clock` (CLI-039) and tests. */
typedef struct {
    uint32_t sysclk_hz, hclk_hz, pclk1_hz, pclk2_hz, apb1_tim_hz, apb2_tim_hz;
    /**< SYSCLK, HCLK, PCLK1, PCLK2 and APB1/APB2 timer clocks, Hz. */
    uint32_t hse_hz, hsi_hz, lsi_hz, lse_hz;
    /**< Nominal oscillator frequencies, Hz. */
    uint32_t pllvco_hz, pllq_hz;
    /**< PLL VCO (not filled in) and 48 MHz domain clock, Hz. */
} hwval_clock_tree_t;

/**
 * @brief Read the current clock tree.
 * @param[out] out Snapshot; @c pllvco_hz is left untouched.
 */
void hwval_clock_get_tree(hwval_clock_tree_t *out);

/**
 * @brief Switch HCLK via HSI to a PLL frequency with a given flash latency.
 * @details Goes through HSI first (SAFE-007) and enables over-drive at
 *          >= 180 MHz. Sets link quiet mode, cleared only by
 *          @ref hwval_clock_restore_default.
 * @param[in] hclk_hz Target HCLK, Hz; whole MHz, PLLN = 2 * MHz.
 * @param[in] ws      Flash latency (FLASH_LATENCY_x).
 * @retval 0  Switched.
 * @retval -1 Switch to HSI failed.
 * @retval -2 PLL configuration failed.
 * @retval -3 Over-drive failed.
 * @retval -4 Final clock configuration failed.
 * @srs FLS-003, CLK-013, SAFE-007
 */
int hwval_clock_set(uint32_t hclk_hz, uint32_t ws);

/**
 * @brief Restore the GEN-004 default tree (216 MHz, 7 WS) and leave quiet mode.
 * @return Result of @ref hwval_clock_set.
 */
int hwval_clock_restore_default(void);

/** @} */

#endif /* HWVAL_CLOCK_H */
