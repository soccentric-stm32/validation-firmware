/*
 * iwdg.h - independent watchdog control (GEN-009)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     GEN-009 (IWDG runs during normal operation, refreshed by a
 *          dedicated low-priority task once the scheduler exists);
 *          section 8.9 (WDG-001..004) builds on this.
 */

#ifndef HWVAL_IWDG_H
#define HWVAL_IWDG_H

#include <stdint.h>

/* Configure and start IWDG in standalone mode (cannot be stopped again).
 * timeout_ms is quantised to the LSI/2^prescaler grid (LSI ~32 kHz). */
void hwval_iwdg_start(uint32_t timeout_ms);

/* Refresh. Callers must be the designated watchdog owner(s). */
void hwval_iwdg_kick(void);

/* Programmed timeout in ms (from the register values, ~32 kHz nominal). */
uint32_t hwval_iwdg_timeout_ms(void);

#endif /* HWVAL_IWDG_H */