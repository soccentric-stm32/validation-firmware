/**
 * @file    iwdg.h
 * @brief   Independent watchdog control.
 * @details The IWDG runs during normal operation and is refreshed by a
 *          dedicated low-priority task once the scheduler exists.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     GEN-009; WDG-001..004 (section 8.9) build on this
 */

#ifndef HWVAL_IWDG_H
#define HWVAL_IWDG_H

#include <stdint.h>

/**
 * @defgroup bsp_iwdg Independent watchdog
 * @brief IWDG start, refresh and timeout query (GEN-009).
 * @{
 */

/**
 * @brief Start the IWDG in standalone mode; it cannot be stopped again.
 * @param[in] timeout_ms Timeout, ms; quantised to the reload grid.
 */
void hwval_iwdg_start(uint32_t timeout_ms);

/**
 * @brief Refresh the IWDG.
 * @note Call only from the designated watchdog owner(s).
 */
void hwval_iwdg_kick(void);

/**
 * @brief Get the programmed timeout, computed from the reload value.
 * @return Timeout, ms, assuming a 32 kHz LSI; 0 before start.
 */
uint32_t hwval_iwdg_timeout_ms(void);

/** @} */

#endif /* HWVAL_IWDG_H */
