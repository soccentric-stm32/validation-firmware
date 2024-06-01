/**
 * @file    reset_cause.h
 * @brief   Reset cause capture and reporting.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     GEN-010, RST-001
 */

#ifndef HWVAL_RESET_CAUSE_H
#define HWVAL_RESET_CAUSE_H

#include <stdint.h>

/**
 * @defgroup bsp_reset_cause Reset cause
 * @brief RCC_CSR reset flags: snapshot, names and boot report (GEN-010).
 * @{
 */

/**
 * @brief Reset cause bits.
 * @note Not the RCC_CSR bit positions; the API passes raw RCC_CSR values.
 */
typedef enum {
    HWVAL_RST_LPWR   = 1u << 0, /**< Low-power reset. */
    HWVAL_RST_WWDG   = 1u << 1, /**< Window watchdog. */
    HWVAL_RST_IWDG   = 1u << 2, /**< Independent watchdog. */
    HWVAL_RST_SFT    = 1u << 3, /**< Software reset. */
    HWVAL_RST_PORPDR = 1u << 4, /**< Power-on/power-down reset. */
    HWVAL_RST_PIN    = 1u << 5, /**< NRST pin. */
    HWVAL_RST_BOR    = 1u << 6, /**< Brown-out reset. */
} hwval_reset_cause_t;

/**
 * @brief Read RCC_CSR now.
 * @return Raw RCC_CSR; flags are cleared by @ref hwval_reset_cause_report.
 */
uint32_t hwval_reset_cause_flags(void);

/**
 * @brief Get the RCC_CSR value captured at boot (TFW-007).
 * @return Raw RCC_CSR snapshot; 0 before the boot report.
 */
uint32_t hwval_reset_flags_snapshot(void);

/**
 * @brief Format RCC_CSR reset flags as "power-on+pin" style text.
 * @param[in] cause Raw RCC_CSR value.
 * @return Static string, or "unknown" when no flag is set.
 * @warning Returns a shared static buffer; not reentrant.
 */
const char *hwval_reset_cause_name(uint32_t cause);

/**
 * @brief Print the reset causes, mirror them to RTC BKP13R, then clear them.
 * @pre USART3 initialised. Call once at boot (GEN-010).
 */
void hwval_reset_cause_report(void);

/** @} */

#endif /* HWVAL_RESET_CAUSE_H */
