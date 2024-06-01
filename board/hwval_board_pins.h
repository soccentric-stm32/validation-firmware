/**
 * @file    hwval_board_pins.h
 * @brief   Pin table, harness wire list and pin ownership registry.
 * @details Harness pairs (SRS Appendix A) are fixed against the [DS11532] AF
 *          tables and the board pin list of [Hardware.md 12/13]/[UM1974].
 *          Pins a bench check must still confirm carry a "verify" flag
 *          (OPEN-004).
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @target  NUCLEO-F767ZI, LQFP144 (ports A-G fully, PH0/PH1 HSE)
 * @srs     GPIO-001, SAFE-006, Appendix A (CL-005), CON-005, CON-006, NFR-009
 */

#ifndef HWVAL_BOARD_PINS_H
#define HWVAL_BOARD_PINS_H

#include <stdint.h>

/**
 * @defgroup hwval_pins Board pins and harness
 * @brief Pin table, harness wires and the SAFE-006 ownership registry.
 * @{
 */

/**
 * @brief Pin ownership class (SAFE-006).
 * @details A pin may only be driven by its owner or when no other owner is
 *          active.
 */
typedef enum {
    PIN_OWNER_FREE = 0,      /**< Drivable by the GPIO/pin tests. */
    PIN_OWNER_CONSOLE,       /**< USART3 VCP PD8/PD9 (CON-005), USART6 debug
                              *   PG14/PG9 (DUART-004). */
    PIN_OWNER_SWDBG,         /**< PA13/PA14 SWD, PH0/PH1 HSE (CON-005). */
    PIN_OWNER_LED,           /**< PB0/PB7/PB14 board LEDs. */
    PIN_OWNER_BTN,           /**< PC13 B1. */
    PIN_OWNER_LSE,           /**< PC14/PC15 LSE crystal. */
    PIN_OWNER_ETH,           /**< RMII PA1/PA2/PC1/PC4/PC5/PG11/PG13 + PB13. */
    PIN_OWNER_USBFS,         /**< PA9..PA12, PG6/PG7 user USB. */
    PIN_OWNER_PERIPH,        /**< Temporarily owned by the running test. */
} hwval_pin_owner_t;

/** @brief One LQFP144 I/O pin. */
struct hwval_pin {
    uint8_t             port;     /**< 0=A..6=G, 8=H. */
    uint8_t             num;      /**< Pin number, 0..15. */
    const char         *hdr;      /**< Arduino/Zio/morpho designator. */
    const char         *conn;     /**< On-board connection. */
    hwval_pin_owner_t   owner;    /**< Static owner class. */
    uint8_t             drivable; /**< 1 when safe to drive as GPIO. */
};

/** All 114 LQFP144 I/O, in device register order. */
extern const struct hwval_pin hwval_pin_table[114];
#define HWVAL_PIN_TABLE_N 114 /**< Entries in @ref hwval_pin_table. */

/** @brief One harness wire (Appendix A) with both endpoints. */
struct hwval_wire {
    const char *name;             /**< Wire name, e.g. "H01". */
    uint8_t     ap, an, bp, bn;   /**< Port/pin of end A, then end B. */
    const char *used_by;          /**< Tests that use the wire. */
    uint8_t     verify;           /**< 1: AF muxing still to confirm on bench. */
};

/** Harness wire list (Appendix A). */
extern const struct hwval_wire hwval_wire_table[];
#define HWVAL_WIRE_TABLE_N 43 /**< Entries in @ref hwval_wire_table. */

/**
 * @brief Claim a free pin for an owner (SAFE-006).
 * @param[in] port Port index, 0=A..8=H.
 * @param[in] num  Pin number, 0..15.
 * @param[in] who  Claiming owner.
 * @retval 0  Claimed.
 * @retval -1 Unknown pin or already claimed.
 * @note Not thread-safe; call from the test task only.
 */
int  hwval_pin_claim(uint8_t port, uint8_t num, hwval_pin_owner_t who);

/**
 * @brief Return a pin to PIN_OWNER_FREE.
 * @param[in] port Port index, 0=A..8=H.
 * @param[in] num  Pin number, 0..15. Unknown pins are ignored.
 */
void hwval_pin_release(uint8_t port, uint8_t num);

/**
 * @brief Check whether @p who may drive a pin.
 * @details SWD/HSE, console and LSE pins are never drivable (CON-005).
 * @param[in] port Port index, 0=A..8=H.
 * @param[in] num  Pin number, 0..15.
 * @param[in] who  Owner asking to drive.
 * @return Pin's drivable flag when unclaimed or claimed by @p who, else 0.
 */
int  hwval_pin_may_drive(uint8_t port, uint8_t num, hwval_pin_owner_t who);

/**
 * @brief Look up a pin in @ref hwval_pin_table.
 * @param[in] port Port index, 0=A..8=H.
 * @param[in] num  Pin number, 0..15.
 * @return Table index 0..113, or 0xFF when not found.
 */
uint8_t hwval_pin_index(uint8_t port, uint8_t num);

/**
 * @brief Check whether a pin is an endpoint of any harness wire.
 * @details Used to exclude wired pins from the short test (GPIO-003).
 * @param[in] port Port index, 0=A..8=H.
 * @param[in] num  Pin number, 0..15.
 * @return 1 if wired, else 0.
 */
int    hwval_pin_is_wire_endpoint(uint8_t port, uint8_t num);

/**
 * @brief Detect whether a harness wire is fitted.
 * @param[in] index Index into @ref hwval_wire_table.
 * @return Detection result.
 * @warning Declared only; no definition exists in the firmware.
 */
int    hwval_wire_detect(uint8_t index);

/**
 * @brief Get a pin table entry by index.
 * @param[in] index Table index, 0..113.
 * @return Entry, or NULL when out of range.
 */
const struct hwval_pin *hwval_pin_at(uint8_t index);

/** @} */

#endif /* HWVAL_BOARD_PINS_H */
