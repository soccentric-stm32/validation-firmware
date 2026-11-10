/*
 * board/hwval_board_pins.h - pin table + harness pairs + ownership (GPIO-001)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  NUCLEO-F767ZI, LQFP144 (ports A-G fully, PH0/PH1 HSE)
 * SRS:     GPIO-001, SAFE-006, Appendix A (CL-005), CON-005/CON-006, NFR-009.
 *
 * The harness pairs of Appendix A are fixed here against [DS11532] AF
 * tables and the board pin list of [Hardware.md 12/13]/[UM1974]. Pins that
 * a bench check must still confirm appear with a "verify" note (OPEN-004).
 */

#ifndef HWVAL_BOARD_PINS_H
#define HWVAL_BOARD_PINS_H

#include <stdint.h>

/* Ownership classes (SAFE-006): a pin may only be driven by its owner or
 * when no other owner is active. */
typedef enum {
    PIN_OWNER_FREE = 0,      /* drivable by the GPIO/pin tests            */
    PIN_OWNER_CONSOLE,       /* USART3 VCP (CON-005: PD8/PD9), USART6 debug
                              * (DUART-004: PG14/PG9)                    */
    PIN_OWNER_SWDBG,         /* PA13/PA14 SWD, PH0/PH1 HSE (CON-005)      */
    PIN_OWNER_LED,           /* PB0/PB7/PB14 board LEDs                   */
    PIN_OWNER_BTN,           /* PC13 B1                                   */
    PIN_OWNER_LSE,           /* PC14/PC15 crystal                         */
    PIN_OWNER_ETH,           /* RMII pins PA1/PA2/PC1/PC4/PC5/PG11/PG13 + PB13 */
    PIN_OWNER_USBFS,         /* PA9..PA12, PG6/PG7 user USB               */
    PIN_OWNER_PERIPH,        /* temporarily owned by the running test     */
} hwval_pin_owner_t;

struct hwval_pin {
    uint8_t             port;     /* 0=A..7=G, 8=H                     */
    uint8_t             num;      /* 0..15                             */
    const char         *hdr;      /* Arduino/Zio/morpho designator     */
    const char         *conn;     /* on-board connection               */
    hwval_pin_owner_t   owner;
    uint8_t             drivable; /* safe to drive as GPIO             */
};

/* All 114 LQFP144 I/O. Entries keep the register order of the device. */
extern const struct hwval_pin hwval_pin_table[114];
#define HWVAL_PIN_TABLE_N 114

/* Harness wire list (Appendix A): each wire with its endpoints. */
struct hwval_wire {
    const char *name;
    uint8_t     ap, an, bp, bn;   /* port/index of both ends           */
    const char *used_by;
    uint8_t     verify;           /* 1: AF muxing to confirm on bench  */
};

extern const struct hwval_wire hwval_wire_table[];
#define HWVAL_WIRE_TABLE_N 43

/* ownership registry (SAFE-006): set/clear while a test owns a device */
int  hwval_pin_claim(uint8_t port, uint8_t num, hwval_pin_owner_t who);
void hwval_pin_release(uint8_t port, uint8_t num);
int  hwval_pin_may_drive(uint8_t port, uint8_t num, hwval_pin_owner_t who);

uint8_t hwval_pin_index(uint8_t port, uint8_t num); /* 0..113 or 0xFF */
int    hwval_pin_is_wire_endpoint(uint8_t port, uint8_t num);
int    hwval_wire_detect(uint8_t index);
const struct hwval_pin *hwval_pin_at(uint8_t index);

#endif /* HWVAL_BOARD_PINS_H */