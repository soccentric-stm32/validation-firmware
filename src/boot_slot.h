/*
 * boot_slot.h - run as a slot image under the A/B bootloader
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-05
 * Target:  STM32F767ZIT6, dual-bank flash (nDBANK = 0)
 * SRS:     SRS-BOOT-F767 LAY-002, HND-003, ENT-002, Appendix B
 *
 * BOOT starts HWVAL from slot A or B with a hand-over record in RTC_BKP2R.
 * A slot on trial must write its CONFIRMED record once the health check
 * passes, or BOOT rolls back after the trial limit.  The values mirror the
 * bootloader contract used by the demo applications.
 */

#ifndef HWVAL_BOOT_SLOT_H
#define HWVAL_BOOT_SLOT_H

#include <stdint.h>

/* Slot identifiers (hwlink Slot). */
#define HWVAL_SLOT_UNKNOWN      0U
#define HWVAL_SLOT_A            1U
#define HWVAL_SLOT_B            2U

/* Trial states (hwlink TrialState). */
#define HWVAL_TRIAL_UNKNOWN     0U
#define HWVAL_TRIAL_CONFIRMED   1U
#define HWVAL_TRIAL_PENDING     2U

/* Health states (hwlink HealthState). */
#define HWVAL_HEALTH_NOT_REQUIRED 0U
#define HWVAL_HEALTH_RUNNING      1U
#define HWVAL_HEALTH_PASSED       2U
#define HWVAL_HEALTH_FAILED       3U

/* Decode the BOOT hand-over record.  Call once in main before the RTOS. */
void hwval_boot_init(void);

uint8_t hwval_boot_slot(void);
uint8_t hwval_boot_trial(void);
uint8_t hwval_boot_trial_count(void);
uint8_t hwval_boot_health(void);

/* 1 while the slot is on trial and the health check has not passed yet.
 * The Runner holds every test (they may reset the MCU) until it clears. */
int hwval_boot_trial_pending(void);

/* Health check, called periodically by the watchdog task: 5 s after start a
 * pending slot is confirmed when the system is healthy. */
void hwval_boot_health_poll(void);

/* 1 once a failed health check must let the IWDG expire so BOOT counts a
 * failed trial. */
int hwval_boot_stop_kick(void);

/* Write the "stay in BOOT" request to RTC_BKP1R before a reset. */
void hwval_boot_request_bootloader(void);

#endif /* HWVAL_BOOT_SLOT_H */
