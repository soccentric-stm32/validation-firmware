/*
 * iwdg.c - independent watchdog (GEN-009)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  STM32F767ZIT6
 * SRS:     GEN-009 (start early, refresh from the Watchdog task in section
 *          5; a hung test then causes a reset reported on the next boot via
 *          GEN-010), RST-001/WIDG tests in section 8.9.
 */

#include "stm32f7xx_hal.h"
#include "bsp/iwdg.h"

#define IWDG_KEY_ACCESS  0x5555U
#define IWDG_KEY_REFRESH 0xAAAAU
#define IWDG_KEY_START   0xCCCCU

#define IWDG_PR_DIV64    6U         /* LSI 32 kHz -> 512 Hz counter tick  */
#define IWDG_TICKS_PER_S 512U

static uint32_t s_timeout_ms;
static int s_started;

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

void hwval_iwdg_kick(void)
{
    if (s_started) {
        IWDG->KR = IWDG_KEY_REFRESH;
    }
}

uint32_t hwval_iwdg_timeout_ms(void) { return s_timeout_ms; }