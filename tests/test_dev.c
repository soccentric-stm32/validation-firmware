/*
 * tests/test_dev.c - device identity (section 8.1)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     DEV-001..DEV-006, level S, no operator; registers [RM0410]
 *          DBGMCU/ID blocks and [DS11532] system calibration constants.
 */

#include <stdio.h>
#include <string.h>
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_limits.h"
#include "app/test_core.h"
#include "app/version.h"

#define DBGMCU_IDCODE_ADDR 0xE0042000U
#define UID_ADDR           0x1FF0F420U
#define FLASHSIZE_ADDR     0x1FF0F442U
#define PACKAGE_ADDR       0x1FF0F7E0U
#define VREFINT_CAL_ADDR   0x1FF0F44AU
#define TS_CAL1_ADDR       0x1FF0F44CU
#define TS_CAL2_ADDR       0x1FF0F44EU

static tfw_result_t dev_idcode(struct tfw_out *o)
{
    const uint32_t idcode = *(const uint32_t *)DBGMCU_IDCODE_ADDR;
    const uint32_t dev_id = idcode & 0xFFFU;
    const uint32_t rev_id = (idcode >> 16U) & 0xFFFFU;

    hwval_meas(o, "DEV_ID=%04lX REV_ID=%04lX", (unsigned long)dev_id,
               (unsigned long)rev_id);
    hwval_lim(o, "==HWVAL_MCU_DEV_ID(0x451), rev 0x1001=Z");
    if (dev_id != HWVAL_MCU_DEV_ID) {
        hwval_fail(o, "DEV_ID %04lX != 0x451", (unsigned long)dev_id);
        return TFW_FAIL;
    }
    if (rev_id != HWVAL_MCU_REV_ID) {
        hwval_fail(o, "REV_ID %04lX != 0x1001 (rev Z)",
                   (unsigned long)rev_id);
        return TFW_FAIL;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "rev Z (DEV-001)");
    return TFW_PASS;
}

static tfw_result_t dev_cpuid(struct tfw_out *o)
{
    const uint32_t cpuid = SCB->CPUID;
    const uint32_t impl  = (cpuid >> 24U) & 0xFFU;
    const uint32_t part  = (cpuid >> 4U) & 0xFFFU;
    const uint32_t var   = (cpuid >> 20U) & 0xFU;
    const uint32_t rev   = cpuid & 0xFU;

    hwval_meas(o, "impl=%02lX part=%03lX r%lup%lu", (unsigned long)impl,
               (unsigned long)part, (unsigned long)var, (unsigned long)rev);
    hwval_lim(o, "impl=0x41 Arm, part=0xC27 CM7");
    if ((impl != 0x41U) || (part != 0xC27U)) {
        hwval_fail(o, "CPUID %08lX not Arm Cortex-M7",
                   (unsigned long)cpuid);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static tfw_result_t dev_flashsize(struct tfw_out *o)
{
    const uint32_t kb = *(const uint16_t *)FLASHSIZE_ADDR;

    hwval_meas(o, "%lu KB", (unsigned long)kb);
    hwval_lim(o, "2048 KB (HWVAL_FLASH_SIZE_KB)");
    if (kb != HWVAL_FLASH_SIZE_KB) {
        hwval_fail(o, "flash size register %lu != 2048 KB",
                   (unsigned long)kb);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static tfw_result_t dev_package(struct tfw_out *o)
{
    const uint32_t pkg = *(const uint32_t *)PACKAGE_ADDR & 0x7U;

    /* CL-014: DS11532 package field for LQFP144; confirmed on the bench in
     * the test phase (Hardware.md section 2 does not record it). */
    hwval_meas(o, "PKG=%lu", (unsigned long)pkg);
    hwval_lim(o, "LQFP144 code(s) in hwval_limits.h");
    if ((pkg != HWVAL_LIM_PKG_LQFP144) && (pkg != HWVAL_LIM_PKG_LQFP144_ALT)) {
        hwval_fail(o, "package code %lu not LQFP144", (unsigned long)pkg);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static tfw_result_t dev_uid(struct tfw_out *o)
{
    const uint32_t w0 = *(const uint32_t *)UID_ADDR;
    const uint32_t w1 = *(const uint32_t *)(UID_ADDR + 4U);
    const uint32_t w2 = *(const uint32_t *)(UID_ADDR + 8U);
    const uint32_t all0 = 0U, all1 = 0xFFFFFFFFU;
    char dec[64];

    hwval_meas(o, "%08lX%08lX%08lX", (unsigned long)w0, (unsigned long)w1,
               (unsigned long)w2);
    /* wafer X/Y from word 0, wafer number from word 1 (DS11532) */
    (void)snprintf(dec, sizeof(dec), "X=%lu Y=%lu wafer=%lu",
                   (unsigned long)(w0 >> 16U), (unsigned long)(w0 & 0xFFFFU),
                   (unsigned long)(w1 & 0xFFU));
    (void)snprintf(o->reason, sizeof(o->reason), "%s", dec);
    hwval_lim(o, "not all 0, not all 1");

    if (((w0 == all0) && (w1 == all0) && (w2 == all0)) ||
        ((w0 == all1) && (w1 == all1) && (w2 == all1))) {
        hwval_fail(o, "UID degenerate");
        return TFW_FAIL;
    }
    if ((w0 == 0x00290028U) && (w1 == 0x3031510DU)) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "matches Hardware.md board uid");
    }
    return TFW_PASS;
}

static tfw_result_t dev_calib(struct tfw_out *o)
{
    const uint16_t vref = *(const uint16_t *)VREFINT_CAL_ADDR;
    const uint16_t ts1  = *(const uint16_t *)TS_CAL1_ADDR;
    const uint16_t ts2  = *(const uint16_t *)TS_CAL2_ADDR;
    int bad = 0;

    hwval_meas(o, "VREFINT=%u TS1=%u TS2=%u", vref, ts1, ts2);
    hwval_lim(o, "VREFINT 1300-1750 | TS1 800-1100 | TS2 1000-1350");
    if ((vref < HWVAL_LIM_VREFINT_CAL_MIN) ||
        (vref > HWVAL_LIM_VREFINT_CAL_MAX)) { bad = 1; }
    if ((ts1 < HWVAL_LIM_TS_CAL1_MIN) || (ts1 > HWVAL_LIM_TS_CAL1_MAX)) {
        bad = 1;
    }
    if ((ts2 < HWVAL_LIM_TS_CAL2_MIN) || (ts2 > HWVAL_LIM_TS_CAL2_MAX)) {
        bad = 1;
    }
    if (bad != 0) {
        hwval_fail(o, "calibration value outside [DS11532] range");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_dev_tests[] = {
    { .id = "DEV.001", .group = "DEV", .title = "DBGMCU_IDCODE DEV_ID/REV_ID",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "DEV-001", .fn = dev_idcode },
    { .id = "DEV.002", .group = "DEV", .title = "Arm Cortex-M7 CPUID",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "DEV-002", .fn = dev_cpuid },
    { .id = "DEV.003", .group = "DEV", .title = "2 MB flash size register",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "DEV-003", .fn = dev_flashsize },
    { .id = "DEV.004", .group = "DEV", .title = "LQFP144 package register",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "DEV-004", .fn = dev_package },
    { .id = "DEV.005", .group = "DEV", .title = "96-bit unique ID decode",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "DEV-005", .fn = dev_uid },
    { .id = "DEV.006", .group = "DEV", .title = "factory calibration values",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "DEV-006", .fn = dev_calib },
};

const size_t g_dev_test_count = sizeof(g_dev_tests) / sizeof(g_dev_tests[0]);

void hwval_test_register_dev(void)
{
    hwval_test_register(g_dev_tests, g_dev_test_count);
}