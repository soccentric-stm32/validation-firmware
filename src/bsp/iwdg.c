/**
 * @file    iwdg.c
 * @brief   Independent watchdog driver.
 * @details Started early; refreshed by the Watchdog task so a hung test causes
 *          a reset that the next boot reports (GEN-010).
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @target  STM32F767ZIT6
 * @srs     GEN-009, GEN-010, RST-001; WDG tests in section 8.9
 */

#include "stm32f7xx_hal.h"
#include "bsp/iwdg.h"

#define IWDG_KEY_ACCESS  0x5555U /**< KR key: unlock PR/RLR. */
#define IWDG_KEY_REFRESH 0xAAAAU /**< KR key: reload the counter. */
#define IWDG_KEY_START   0xCCCCU /**< KR key: start the watchdog. */

/**
 * PR value written at start.
 * @warning RM0410 decodes PR = 6 as /256, not /64; the timeout math below
 *          assumes /64.
 */
#define IWDG_PR_DIV64    6U
#define IWDG_TICKS_PER_S 512U /**< Assumed counter rate, Hz (32 kHz / 64). */

static uint32_t s_timeout_ms; /**< Programmed timeout, ms. */
static int s_started;         /**< Non-zero once the IWDG runs. */

/**
 * @brief Start the IWDG; it cannot be stopped again.
 * @param[in] timeout_ms Timeout, ms; reload clamped to 1..4095.
 */
void hwval_iwdg_start(uint32_t timeout_ms)
{
    uint32_t reload = (timeout_ms * IWDG_TICKS_PER_S) / 1000U;

    if (reload > 4095U) { reload = 4095U; }
    if (reload == 0U)   { reload = 1U;    }

    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->PR = IWDG_PR_DIV64;
    IWDG->RLR = reload;
    IWDG->KR = IWDG_KEY_REFRESH;
    IWDG->KR = IWDG_KEY_START; /* standalone: runs after any reset */

    s_timeout_ms = (reload * 1000U) / IWDG_TICKS_PER_S;
    s_started = 1;
}

/** @brief Refresh the IWDG; no-op before @ref hwval_iwdg_start. */
void hwval_iwdg_kick(void)
{
    if (s_started) {
        IWDG->KR = IWDG_KEY_REFRESH;
    }
}

/**
 * @brief Get the programmed timeout.
 * @return Timeout, ms; 0 before start.
 */
uint32_t hwval_iwdg_timeout_ms(void) { return s_timeout_ms; }
