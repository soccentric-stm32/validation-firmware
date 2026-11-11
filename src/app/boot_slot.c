/*
 * boot_slot.c - run as a slot image under the A/B bootloader
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-05
 * Target:  STM32F767ZIT6, dual-bank flash (nDBANK = 0)
 * SRS:     SRS-BOOT-F767 LAY-002, HND-003, ENT-002, Appendix B
 *
 * The CONFIRMED record is the only write HWVAL makes to a slot's metadata
 * sector.  Its address is fixed: while an application runs, BOOT's bank swap
 * (SYSCFG_MEMRMP.SWP_FB) maps the running bank at 0x08000000, so the running
 * slot's metadata sector is always at 0x08008000.  The record is 28 bytes
 * (magic, zeros) followed by CRC-32/ISO-HDLC over them; it is programmed
 * once, never erased, and a torn record is never overwritten.
 */

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "app/boot_slot.h"
#include "services/fault.h"
#include "bsp/uart3.h"
#include "bsp/debug_uart.h"

/* RTC_BKP2R hand-over record (HND-003): bits 0..23 magic "HND", 24..25
 * slot, 26 trial pending, 27..28 trial counter. */
#define HANDOVER_MAGIC          0x00444E48UL
#define HANDOVER_MAGIC_MASK     0x00FFFFFFUL
#define HANDOVER_SLOT_SHIFT     24U
#define HANDOVER_SLOT_MASK      0x3UL
#define HANDOVER_PENDING_BIT    26U
#define HANDOVER_TRIAL_SHIFT    27U
#define HANDOVER_TRIAL_MASK     0x3UL

/* RTC_BKP1R "stay in BOOT" request (ENT-002). */
#define STAY_IN_BOOT_MAGIC      0x59415453UL   /* "STAY" */

/* Appendix B: CONFIRMED record of the running slot. */
#define CONFIRMED_ADDR          (0x08008000UL + 0x140UL)
#define CONFIRMED_MAGIC         0x434F4E46UL   /* "CONF" */
#define CONFIRMED_WORDS         8U

#define HEALTH_CHECK_AT_MS      5000U
#define HEALTH_STOP_KICK_MS     10000U
#define HEALTH_MIN_FREE_HEAP    4096U

static uint8_t s_slot = HWVAL_SLOT_UNKNOWN;
static volatile uint8_t s_trial = HWVAL_TRIAL_UNKNOWN;
static uint8_t s_trial_count;
static volatile uint8_t s_health = HWVAL_HEALTH_NOT_REQUIRED;
static uint32_t s_fault_count_at_boot;

static uint32_t crc32_iso_hdlc(const uint32_t *words, uint32_t count)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < count; i++) {
        for (uint32_t b = 0U; b < 4U; b++) {
            crc ^= (words[i] >> (8U * b)) & 0xFFU;
            for (uint32_t k = 0U; k < 8U; k++) {
                crc = ((crc & 1U) != 0U) ? ((crc >> 1U) ^ 0xEDB88320UL)
                                         : (crc >> 1U);
            }
        }
    }
    return ~crc;
}

static void confirmed_invalidate(void)
{
    SCB_InvalidateDCache_by_Addr((uint32_t *)CONFIRMED_ADDR,
                                 (int32_t)(CONFIRMED_WORDS * 4U));
}

static int confirmed_valid(const volatile uint32_t *record)
{
    uint32_t copy[CONFIRMED_WORDS];

    for (uint32_t i = 0U; i < CONFIRMED_WORDS; i++) {
        copy[i] = record[i];
    }
    return (copy[0] == CONFIRMED_MAGIC) &&
           (crc32_iso_hdlc(copy, CONFIRMED_WORDS - 1U) ==
            copy[CONFIRMED_WORDS - 1U]);
}

static int confirmed_erased(const volatile uint32_t *record)
{
    for (uint32_t i = 0U; i < CONFIRMED_WORDS; i++) {
        if (record[i] != 0xFFFFFFFFUL) {
            return 0;
        }
    }
    return 1;
}

/* Register-level word program, PSIZE x32 like flash_ops.c. */
static int program_word(uint32_t addr, uint32_t value)
{
    FLASH->SR = FLASH_SR_PGPERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR |
                FLASH_SR_OPERR | FLASH_SR_ERSERR | FLASH_SR_EOP;
    MODIFY_REG(FLASH->CR, FLASH_CR_PSIZE | FLASH_CR_SER | FLASH_CR_SNB,
               FLASH_PSIZE_WORD);
    SET_BIT(FLASH->CR, FLASH_CR_PG);
    *(volatile uint32_t *)addr = value;
    __DSB();
    for (uint32_t loops = 1000000U; (FLASH->SR & FLASH_SR_BSY) != 0U;) {
        if (--loops == 0U) {
            CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
            return -1;
        }
    }
    CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
    if ((FLASH->SR & (FLASH_SR_PGPERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR |
                      FLASH_SR_OPERR | FLASH_SR_ERSERR)) != 0U) {
        return -1;
    }
    confirmed_invalidate();
    return (*(volatile uint32_t *)addr == value) ? 0 : -1;
}

static int confirm_running_slot(void)
{
    volatile uint32_t *record = (volatile uint32_t *)CONFIRMED_ADDR;
    uint32_t words[CONFIRMED_WORDS] = { 0U };
    int ok = 1;

    confirmed_invalidate();
    if (confirmed_valid(record)) {
        return 1;
    }
    if (!confirmed_erased(record)) {
        return 0;   /* append-only: a torn record is never overwritten */
    }

    words[0] = CONFIRMED_MAGIC;
    words[CONFIRMED_WORDS - 1U] = crc32_iso_hdlc(words, CONFIRMED_WORDS - 1U);

    if (READ_BIT(FLASH->CR, FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
    if (READ_BIT(FLASH->CR, FLASH_CR_LOCK) != 0U) {
        return 0;
    }
    for (uint32_t i = 0U; i < CONFIRMED_WORDS; i++) {
        if (program_word(CONFIRMED_ADDR + (i * 4U), words[i]) != 0) {
            ok = 0;
            break;
        }
    }
    SET_BIT(FLASH->CR, FLASH_CR_LOCK);
    confirmed_invalidate();
    return ok && confirmed_valid(record);
}

static int health_conditions_met(void)
{
    const hwval_fault_record_t *r = hwval_fault_record();

    if ((r->magic == 0x4856464CU /* "HVFL" */) &&
        (r->count != s_fault_count_at_boot)) {
        return 0;
    }
    if (xPortGetFreeHeapSize() < HEALTH_MIN_FREE_HEAP) {
        return 0;
    }
    return hwval_uart3_ready() ? 1 : 0;
}

void hwval_boot_init(void)
{
    /* Reading the backup registers needs no DBP; only BKP1R writes do. */
    uint32_t raw = RTC->BKP2R;
    const hwval_fault_record_t *r = hwval_fault_record();

    s_fault_count_at_boot = (r->magic == 0x4856464CU) ? r->count : 0U;

    if ((raw & HANDOVER_MAGIC_MASK) != HANDOVER_MAGIC) {
        return;   /* no BOOT hand-over: slot UNKNOWN, nothing to confirm */
    }
    s_slot = (uint8_t)((raw >> HANDOVER_SLOT_SHIFT) & HANDOVER_SLOT_MASK);
    if ((s_slot != HWVAL_SLOT_A) && (s_slot != HWVAL_SLOT_B)) {
        s_slot = HWVAL_SLOT_UNKNOWN;
        return;
    }
    s_trial = (((raw >> HANDOVER_PENDING_BIT) & 1U) != 0U)
              ? HWVAL_TRIAL_PENDING : HWVAL_TRIAL_CONFIRMED;
    s_trial_count = (uint8_t)((raw >> HANDOVER_TRIAL_SHIFT) &
                              HANDOVER_TRIAL_MASK);
    s_health = (s_trial == HWVAL_TRIAL_PENDING) ? HWVAL_HEALTH_RUNNING
                                                : HWVAL_HEALTH_NOT_REQUIRED;
}

uint8_t hwval_boot_slot(void) { return s_slot; }
uint8_t hwval_boot_trial(void) { return s_trial; }
uint8_t hwval_boot_trial_count(void) { return s_trial_count; }
uint8_t hwval_boot_health(void) { return s_health; }

int hwval_boot_trial_pending(void)
{
    return (s_trial == HWVAL_TRIAL_PENDING) ? 1 : 0;
}

void hwval_boot_health_poll(void)
{
    if (s_health != HWVAL_HEALTH_RUNNING) {
        return;
    }
    uint32_t now = HAL_GetTick();

    if (now < HEALTH_CHECK_AT_MS) {
        return;
    }
    if (health_conditions_met() && confirm_running_slot()) {
        /* The CONFIRMED record is in flash: report CONFIRMED in Status and
         * release the Runner.  Single byte stores, never torn. */
        s_trial = HWVAL_TRIAL_CONFIRMED;
        s_health = HWVAL_HEALTH_PASSED;
        hwval_dbg('I', "health: passed, CONFIRMED written");
    } else if (now >= HEALTH_STOP_KICK_MS) {
        const hwval_fault_record_t *r = hwval_fault_record();
        unsigned new_faults = (r->magic == 0x4856464CU)
                                  ? (unsigned)(r->count - s_fault_count_at_boot) : 0U;

        s_health = HWVAL_HEALTH_FAILED;
        hwval_dbg('E', "health: not healthy by %u ms, trial fails "
                  "(new faults %u, free heap %u, uart3 ready %u)",
                  (unsigned)HEALTH_STOP_KICK_MS, new_faults,
                  (unsigned)xPortGetFreeHeapSize(),
                  (unsigned)hwval_uart3_ready());
    }
}

int hwval_boot_stop_kick(void)
{
    return (s_health == HWVAL_HEALTH_FAILED) ? 1 : 0;
}

void hwval_boot_request_bootloader(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR1, PWR_CR1_DBP);
    RTC->BKP1R = STAY_IN_BOOT_MAGIC;
    __DSB();
}
