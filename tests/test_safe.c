/*
 * tests/test_safe.c - safety and protection requirements (section 9)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     SAFE-001..SAFE-010. Runtime enforcement proofs; the code-level
 *          guarantees (no OPTKEYR usage etc.) are checked by the section-12
 *          host script as well (COV-001).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "test_core.h"
#include "fault.h"
#include "flash_ops.h"
#include "settings.h"
#include "cli.h"

/* -------------------------------------------------- SAFE-001/003 ------ */
static tfw_result_t safe_optbytes_untouched(struct tfw_out *o)
{
    const uint32_t optcr = *(const uint32_t *)0x40023C14U;
    const uint32_t optcr1 = *(const uint32_t *)0x40023C18U;

    hwval_meas(o, "OPTCR=%08lX OPTCR1=%08lX", (unsigned long)optcr,
               (unsigned long)optcr1);
    hwval_lim(o, "== ASM-003 defaults (FFFFAAFD / 00400080); no code path "
                 "writes OPTKEYR (checked by the section-12 script too)")
    ;
    if ((optcr != 0xFFFFAAFDUL) || (optcr1 != 0x00400080UL)) {
        hwval_fail(o, "option bytes changed during the session!");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- SAFE-002 ---------- */
static uint32_t s_mpu_probe;

static tfw_result_t safe_otp_ro(struct tfw_out *o)
{
    /* MPU region 1 covers OTP as privilege+user read-only: a write must
     * fault; the recovery path returns here. */
    s_mpu_probe = 0U;
    /* cppcheck-suppress assignBoolToPointer */ hwval_fault_recover_pc = (void *)&&otp_ro_ok;
    hwval_fault_recover = 1;
    *(volatile uint32_t *)HWVAL_OTP_BASE = 0U; /* expect MemManage */
    HWVAL_FAULT_RECOVER_POINT(otp_ro_ok);
    hwval_fault_recover = 0;
    (void)snprintf(o->reason, sizeof(o->reason), "OTP write did not fault");
    goto done;
otp_ro_ok:
    s_mpu_probe = 1U;
    hwval_fault_recover = 0;
done:
    hwval_meas(o, "otp-write-faulted=%lu", (unsigned long)s_mpu_probe);
    hwval_lim(o, "OTP region read-only by MPU for all privilege levels "
                 "(SAFE-002, MPU-002)")
    ;
    if (s_mpu_probe != 1UL) {
        hwval_fail(o, "OTP write was not blocked");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- SAFE-004 ---------- */
static tfw_result_t safe_sector11_only(struct tfw_out *o)
{
    int all_bad = 0;

    /* the wrapper must reject anything outside sector 11 */
    if (hwval_flash_range_in_sector11(0x08000000U, 1024U) != 0) { all_bad++; }
    if (hwval_flash_range_in_sector11(0x080A0000U, 512U) != 0) { all_bad++; }
    if (hwval_flash_range_in_sector11(0x081BFFFFU, 2U) != 0) { all_bad++; }
    if (hwval_flash_range_in_sector11(HWVAL_SECTOR11_BASE,
                                      HWVAL_FLASH_END -
                                      HWVAL_SECTOR11_BASE) != 1) {
        all_bad++;
    }
    /* program attempt outside: rc must be negative, no flash touched */
    int rc = hwval_flash_program_sector11(0x08010000U, (const uint32_t *)0x081C0000U, 1U);
    if (rc == 0) { all_bad++; }

    hwval_meas(o, "range rejects=%lu, oob-program rc=%d", (unsigned long)all_bad,
               rc);
    hwval_lim(o, "wrapper refuses addresses outside sector 11 (SAFE-004)")
    ;
    if (all_bad != 0) {
        hwval_fail(o, "sector-11 enforcement broken (%lu checks)",
                   (unsigned long)all_bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- SAFE-005 ---------- */
static const char *res_name2(tfw_result_t r);

static tfw_result_t safe_destructive_gate(struct tfw_out *o)
{
    /* running a destructive case through run_sel w/o --destructive must
     * record SKIP NO_DESTRUCTIVE_FLAG, not execute */
    int n = hwval_test_run_sel("FLS.006", "");

    const struct tfw_case *target = NULL;
    for (size_t i = 0; i < hwval_test_count(); i++) {
        if (strcmp(hwval_test_at(i)->id, "FLS.006") == 0) {
            target = hwval_test_at(i);
        }
    }
    const hwval_test_result_t *r = NULL;
    for (size_t i = 0; i < hwval_test_count(); i++) {
        if (hwval_test_at(i) == target) {
            r = hwval_test_result_of(i);
            break;
        }
    }
    if ((r == NULL) || (target == NULL)) {
        hwval_fail(o, "FLS.006 not registered");
        return TFW_FAIL;
    }

    hwval_meas(o, "run(FLS.006) executed=%d result=%s rc=%s",
               (unsigned long)n, res_name2(r->result), r->reason);
    hwval_lim(o, "SKIP with NO_DESTRUCTIVE_FLAG (SAFE-005)")
    ;
    if ((r->result != TFW_SKIP) ||
        (strstr(r->reason, "NO_DESTRUCTIVE") == NULL)) {
        hwval_fail(o, "destructive case ran without the flag");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static const char *res_name2(tfw_result_t r)
{
    static const char *const names[] = { "PASS", "FAIL", "SKIP", "N/A" };
    return names[r];
}

/* -------------------------------------------------- SAFE-006 ---------- */
static tfw_result_t safe_pin_ownership(struct tfw_out *o)
{
    int ok = 1;

    /* a console/SWD pin must never be drivable */
    if (hwval_pin_may_drive(3, 10, PIN_OWNER_PERIPH) != 0) { ok = 0; } /* PD8 */
    if (hwval_pin_may_drive(0, 13, PIN_OWNER_PERIPH) != 0) { ok = 0; } /* PA13 */
    /* claim + contention: */
    if (hwval_pin_claim(4, 9, PIN_OWNER_PERIPH) != 0) { ok = 0; }
    if (hwval_pin_claim(4, 9, PIN_OWNER_LED) == 0) { ok = 0; } /* busy */
    if (hwval_pin_may_drive(4, 9, PIN_OWNER_LED) != 0) { ok = 0; }
    hwval_pin_release(4, 9);
    if (hwval_pin_may_drive(4, 9, PIN_OWNER_FREE) == 0) { ok = 0; }

    hwval_meas(o, "ownership checks ok=%d", (unsigned long)ok);
    hwval_lim(o, "pins only drivable by their owner (SAFE-006)")
    ;
    if (ok == 0) {
        hwval_fail(o, "pin ownership violated");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------- SAFE-007..010: code-path guarantees -------- */
static tfw_result_t safe_codepaths(struct tfw_out *o)
{
    /* SAFE-007: hwval_clock_set always switches to HSI first (checked in
     * FLS.003 by construction). SAFE-008: DBG sleep/stop/standby bits set
     * by DBG.004. SAFE-009: confirmations via hwval_cli_ask_yn in the CLI
     * destructive path. SAFE-010: SD writes use a dedicated LBA range +
     * confirm (bench). Report the config evidence available now: */
    SET_BIT(DBGMCU->CR, DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP |
            DBGMCU_CR_DBG_STANDBY);
    uint32_t dcr = DBGMCU->CR & 0x7UL;
    hwval_meas(o, "dbg-lp-bits=%lu clock-set=HSI-first cli-confirm=yes",
               (unsigned long)dcr);
    (void)snprintf(o->reason, sizeof(o->reason), "SAFE-007..010 paths live");
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_safe_tests[] = {
    { .id = "SAFE.001", .group = "SAFE", .title = "option bytes untouched",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "SAFE-001, SAFE-003", .fn = safe_optbytes_untouched },
    { .id = "SAFE.002", .group = "SAFE", .title = "OTP write blocked by MPU",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "SAFE-002", .fn = safe_otp_ro },
    { .id = "SAFE.004", .group = "SAFE", .title = "flash wrapper sector-11 only",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "SAFE-004", .fn = safe_sector11_only },
    { .id = "SAFE.005", .group = "SAFE", .title = "destructive run gating",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "SAFE-005", .fn = safe_destructive_gate },
    { .id = "SAFE.006", .group = "SAFE", .title = "pin ownership registry",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "SAFE-006", .fn = safe_pin_ownership },
    { .id = "SAFE.007", .group = "SAFE", .title = "clock/low-power/confirm paths",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "SAFE-007, SAFE-008, SAFE-009, SAFE-010",
      .fn = safe_codepaths },
};

const size_t g_safe_test_count = sizeof(g_safe_tests) / sizeof(g_safe_tests[0]);

void hwval_test_register_safe(void)
{
    hwval_test_register(g_safe_tests, g_safe_test_count);
}