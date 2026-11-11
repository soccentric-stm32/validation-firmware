/*
 * reset_cause.h - reset cause reporting (GEN-010, RST-001)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 */

#ifndef HWVAL_RESET_CAUSE_H
#define HWVAL_RESET_CAUSE_H

#include <stdint.h>

/* Bitmask of RCC_CSR reset flags read at boot (before clearing). */
typedef enum {
    HWVAL_RST_LPWR   = 1u << 0,
    HWVAL_RST_WWDG   = 1u << 1,
    HWVAL_RST_IWDG   = 1u << 2,
    HWVAL_RST_SFT    = 1u << 3,
    HWVAL_RST_PORPDR = 1u << 4,
    HWVAL_RST_PIN    = 1u << 5,
    HWVAL_RST_BOR    = 1u << 6,
} hwval_reset_cause_t;

uint32_t hwval_reset_cause_flags(void);
uint32_t hwval_reset_flags_snapshot(void);
const char *hwval_reset_cause_name(uint32_t cause);

/* GEN-010: print the causes on boot, then clear the flags. */
void hwval_reset_cause_report(void);

#endif /* HWVAL_RESET_CAUSE_H */