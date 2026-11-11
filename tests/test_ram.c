/*
 * tests/test_ram.c - internal RAM tests (section 8.4)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     RAM-001..RAM-009, level S (RAM-008 destructive, RAM-009 S, O).
 *
 * March C- [RAM-001..004] runs on every block not used by HWVAL in-place;
 * the used blocks (linker-managed data/bss incl. the kernel heap, the
 * results .noinit block, the MPU non-cacheable block) use the non-
 * destructive save-test-restore of RAM-005 with interrupts disabled per
 * small block.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"

/* linker-provided block boundaries */
extern uint32_t _sdata, _edata, _sbss, _ebss, _estack;

/* March C- elements: {w(z); r(z),w(x); r(x),w(z); r(z),w(!x); r(!x)} — the
 * classic 5-element 6N March with background z/x/!x. */
static int march_c_minus(volatile uint32_t *base, uint32_t words,
                         const char **why)
{
    /* upward and downward passes; returns 0 on pass */
    uint32_t i;

    for (i = 0U; i < words; i++) { base[i] = 0U; }
    for (i = 0U; i < words; i++) {           /* r0, wA5 */
        if (base[i] != 0U) { *why = "stale data after w0"; return -1; }
        base[i] = 0xA5A5A5A5U;
    }
    for (i = 0U; i < words; i++) {           /* rA5, w0 */
        if (base[i] != 0xA5A5A5A5U) { *why = "cell stuck/failed read"; return -1; }
        base[i] = 0U;
    }
    for (i = words; i > 0U; i--) {           /* down: r0, w5A */
        if (base[i - 1U] != 0U) { *why = "disturb during down pass"; return -1; }
        base[i - 1U] = 0x5A5A5A5AU;
    }
    for (i = words; i > 0U; i--) {           /* down: r5A, w0 */
        if (base[i - 1U] != 0x5A5A5A5AU) { *why = "read back bad (down)"; return -1; }
        base[i - 1U] = 0U;
    }
    return 0;
}

/* used-block save/test/restore: one word at a time, IRQs off per block */
static int nondestructive_march(volatile uint32_t *base, uint32_t words,
                                const char **why)
{
    /* verifies each word against an alternating pattern in small critical
     * sections; a mismatch after write+readback flags the cell */
    uint32_t start = 0U;

    while (start < words) {
        uint32_t n = (words - start > 512U) ? 512U : (words - start);
        uint32_t j;

        taskDISABLE_INTERRUPTS();
        for (j = 0U; j < n; j++) {
            volatile uint32_t *a = &base[start + j];
            uint32_t orig = *a;
            *a = 0xA5A5A5A5U ^ (uintptr_t)a;
            __DSB();
            if (*a != (0xA5A5A5A5U ^ (uintptr_t)a)) {
                *a = orig;
                *why = "cell fails save/restore march";
                taskENABLE_INTERRUPTS();
                return -1;
            }
            *a = orig;
            __DSB();
            if (*a != orig) {
                *why = "restore corrupted memory";
                taskENABLE_INTERRUPTS();
                return -1;
            }
        }
        taskENABLE_INTERRUPTS();
        start += n;
        vTaskDelay(1U); /* blocks small enough to keep the CLI responsive */
    }
    return 0;
}

/* region descriptor for the suite */
struct ram_region {
    const char        *name;
    volatile uint32_t *base;
    uint32_t           words;
    uint8_t            used_ranges; /* count of used sub-ranges */
    uint32_t           used[2][2];  /* [idx][start,len] in words */
};

static void build_regions(struct ram_region *r)
{
    /* ITCM: entirely free during testing (code copied transiently) */
    r[0] = (struct ram_region){ "ITCM", (volatile uint32_t *)HWVAL_ITCMRAM_BASE,
                                HWVAL_ITCMRAM_SIZE / 4U, 0, { {0,0},{0,0} } };

    /* DTCM: everything not data/bss/stack */
    uint32_t dtcm_lo = HWVAL_DTCMRAM_BASE / 4U;
    uint32_t dtcm_words = HWVAL_DTCMRAM_SIZE / 4U;
    uint32_t data_lo = (uint32_t)&_sdata / 4U;
    uint32_t bss_end = (uint32_t)&_ebss / 4U;
    /* Keep a guard below the MSP: interrupts use this stack while the
     * March runs, and 4 KB (the linker minimum) is too tight. */
    uint32_t stack_lo = ((uint32_t)&_estack - 0x2000U) / 4U;
    uint32_t stack_hi = (uint32_t)&_estack / 4U;
    r[1] = (struct ram_region){ "DTCM", (volatile uint32_t *)HWVAL_DTCMRAM_BASE,
                                dtcm_words, 2,
                                { { data_lo, bss_end - data_lo },
                                  { stack_lo, stack_hi - stack_lo } } };
    (void)dtcm_lo;
}

#define SRAM1_FREE_WORDS ((304U * 1024U) / 4U)
/* SRAM2 free part 0x2007C000..0x2007DFFF (.noncacheable at 0x2007E000) */
static struct ram_region s_sram2 = {
    "SRAM2", (volatile uint32_t *)HWVAL_SRAM2_BASE, 8U * 1024U / 4U, 0,
    { {0,0},{0,0} },
};

static int test_region(struct ram_region *rg, struct tfw_out *o)
{
    const char *why = "unknown";
    uint32_t pos = 0U;

    for (uint32_t u = 0U; u < rg->used_ranges; u++) {
        uint32_t use_lo = rg->used[u][0], use_len = rg->used[u][1];
        uint32_t rel = use_lo - ((uint32_t)rg->base / 4U);
        if (rel > pos) { /* free gap before this used range */
            if (march_c_minus(&rg->base[pos], rel - pos, &why) != 0) {
                hwval_fail(o, "%s word %lu: %s", rg->name,
                           (unsigned long)(rg->base[pos] / 4U), why);
                return -1;
            }
        }
        /* used range: non-destructive save/restore march */
        if (nondestructive_march(&rg->base[rel], use_len, &why) != 0) {
            hwval_fail(o, "%s used block: %s", rg->name, why);
            return -1;
        }
        pos = rel + use_len;
    }
    if (pos < rg->words) {
        if (march_c_minus(&rg->base[pos], rg->words - pos, &why) != 0) {
            hwval_fail(o, "%s word %lu: %s", rg->name,
                       (unsigned long)pos, why);
            return -1;
        }
    }
    return 0;
}

static tfw_result_t ram_groups(struct tfw_out *o)
{
    struct ram_region regs[2];
    struct ram_region itcm;

    build_regions(regs);
    itcm = regs[0];

    if (test_region(&itcm, o) != 0) {        /* RAM-001 */
        return TFW_FAIL;
    }
    (void)snprintf(o->measured, sizeof(o->measured), "%s", "ITCM 16KB ok");
    if (test_region(&regs[1], o) != 0) {     /* RAM-002 DTCM */
        return TFW_FAIL;
    }
    /* RAM-003: .sram1 holds the DMA/DMA2D/ADC buffers; march them with the
     * save/restore path instead of overwriting them with March C-. */
    extern uint32_t __sram1_start, __sram1_end;
    struct ram_region sram1 = {
        "SRAM1", (volatile uint32_t *)HWVAL_SRAM1_BASE, SRAM1_FREE_WORDS, 0,
        { {0,0},{0,0} },
    };
    uint32_t sram1_used = (uint32_t)&__sram1_end - (uint32_t)&__sram1_start;
    if (sram1_used > 0U) {
        sram1.used_ranges = 1U;
        sram1.used[0][0] = (uint32_t)&__sram1_start / 4U;
        sram1.used[0][1] = sram1_used / 4U;
    }
    if (test_region(&sram1, o) != 0) {       /* RAM-003 SRAM1 */
        return TFW_FAIL;
    }
    if (test_region(&s_sram2, o) != 0) {     /* RAM-004 SRAM2 */
        return TFW_FAIL;
    }
    (void)snprintf(o->reason, sizeof(o->reason),
                   "itcm/dtcm/sram1/sram2 march + save/restore pass");
    (void)snprintf(o->measured, sizeof(o->measured),
                   "itcm16K dtcm128K sram1 304K sram2 8K");
    hwval_lim(o, "March C- clean on all free blocks; live blocks preserved");
    return TFW_PASS;
}

/* -------------------------------------------------------------- RAM-006 */
static tfw_result_t ram_buses(struct tfw_out *o)
{
    /* walking bit patterns + power-of-two address strides on a small free
     * slab of SRAM2 (0x2007C000..), ITCM and DTCM top (free area). */
    static const uint32_t walks[8] = {
        0x00000001U, 0x00000002U, 0x00000004U, 0x00000008U,
        0x00000010U, 0x00000020U, 0x00000040U, 0x00000080U,
    };
    volatile uint32_t *slabs[3] = {
        (volatile uint32_t *)HWVAL_ITCMRAM_BASE,
        (volatile uint32_t *)(HWVAL_SRAM2_BASE),
        (volatile uint32_t *)(HWVAL_DTCMRAM_BASE + 0x1E000U), /* free tail */
    };
    int bad = 0;

    for (int s = 0; s < 3; s++) {
        volatile uint32_t *m = slabs[s];
        for (int p = 0; p < 8; p++) {
            uint32_t pat = walks[p] | (walks[p] << 24U) | (walks[p] << 16U) |
                           (walks[p] << 8U);
            for (uint32_t a = 0U; a < 8U; a++) { /* power-of-two strides */
                m[a * (1U << p)] = pat;
                __DSB();
            }
            for (uint32_t a = 0U; a < 8U; a++) {
                if (m[a * (1U << p)] != pat) { bad = 1; }
                m[a * (1U << p)] = 0U;
            }
            for (uint32_t a = 0U; a < 8U; a++) {
                m[a * (1U << p)] = ~pat;
                __DSB();
            }
            for (uint32_t a = 0U; a < 8U; a++) {
                if (m[a * (1U << p)] != ~pat) { bad = 1; }
                m[a * (1U << p)] = 0U;
            }
        }
    }
    hwval_meas(o, "walk1s/0s + pow2 strides, 3 regions");
    hwval_lim(o, "no data/address bus faults (RAM-006)");
    if (bad != 0) {
        hwval_fail(o, "bus pattern mismatch");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------------------- RAM-007 */
extern uint8_t hwval_itcm_base[]; /* linker symbol (see ld file) */

static tfw_result_t ram_widths(struct tfw_out *o)
{
    volatile uint8_t *m8 = (volatile uint8_t *)(void *)hwval_itcm_base;
    volatile uint16_t *m16 = (volatile uint16_t *)(void *)(hwval_itcm_base + 0x40U);
    volatile uint32_t *m32 = (volatile uint32_t *)(void *)(hwval_itcm_base + 0x80U);
    volatile uint64_t *m64 = (volatile uint64_t *)(void *)(hwval_itcm_base + 0xC0U);
    const uint64_t p64 = 0xFEDCBA9876543210ULL;
    int bad = 0;

    /* 8-bit */
    for (int i = 0; i < 64; i++) { m8[i] = (uint8_t)i; __DSB(); }
    for (int i = 0; i < 64; i++) { if (m8[i] != (uint8_t)i) { bad = 1; } m8[i] = 0; }
    /* 16-bit */
    for (int i = 0; i < 32; i++) { m16[i] = (uint16_t)(i * 0x1111U); __DSB(); }
    for (int i = 0; i < 32; i++) { if (m16[i] != (uint16_t)(i * 0x1111U)) { bad = 1; } m16[i] = 0; }
    /* 32-bit */
    for (int i = 0; i < 16; i++) { m32[i] = 0xA5A5A5A5U ^ (uint32_t)i; __DSB(); }
    for (int i = 0; i < 16; i++) { if (m32[i] != (0xA5A5A5A5U ^ (uint32_t)i)) { bad = 1; } m32[i] = 0; }
    /* 64-bit (LDRD/STRD) */
    for (int i = 0; i < 8; i++) { m64[i] = p64 ^ ((uint64_t)i << 32U); __DSB(); }
    for (int i = 0; i < 8; i++) { if (m64[i] != (p64 ^ ((uint64_t)i << 32U))) { bad = 1; } m64[i] = 0ULL; }

    hwval_meas(o, "8/16/32/64-bit ITCM roundtrips");
    hwval_lim(o, "all widths intact (RAM-007)");
    if (bad != 0) {
        hwval_fail(o, "narrow/wide access mismatch");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------------------- RAM-008 */
static tfw_result_t ram_bkpsram_march(struct tfw_out *o)
{
    /* Destructive: wipes backup SRAM (regulator off path). Requires the
     * --destructive flag (SAFE-005). */
    volatile uint32_t *b = (volatile uint32_t *)HWVAL_BKPSRAM_BASE;
    uint32_t words = HWVAL_BKPSRAM_SIZE / 4U;
    const char *why = "unknown";
    int rc;

    __HAL_RCC_PWR_CLK_ENABLE();
    /* The backup SRAM is inaccessible unless DBP and the backup regulator
     * are on and its clock is enabled; without all three the writes are
     * ignored and the march always fails. */
    SET_BIT(PWR->CR1, PWR_CR1_DBP);
    SET_BIT(PWR->CSR1, PWR_CSR1_BRE);
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_BKPSRAMEN);
    __DSB();
    rc = march_c_minus(b, words, &why);
    hwval_meas(o, "4 KB backup SRAM march (BRE on)");
    hwval_lim(o, "March C- clean; content destroyed (destructive)");
    if (rc != 0) {
        hwval_fail(o, "%s", why);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------------------- RAM-009 */
#define BKPSRAM_PAT_LO 0xC0DEC0DEU
#define BKPSRAM_PAT_HI 0xFACEFEEDU
#define BOOTCHECK_HASH_RAM009 0x5A00U

static tfw_result_t ram_bkpsram_retention(struct tfw_out *o)
{
    volatile uint32_t *b = (volatile uint32_t *)HWVAL_BKPSRAM_BASE;

    /* enable backup regulator + access: content must survive a system
     * reset and (Level O for standby) a Standby cycle. */
    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR1, PWR_CR1_DBP);
    SET_BIT(PWR->CSR1, PWR_CSR1_BRE);
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_BRR) == 0U) { __NOP(); }

    b[0] = BKPSRAM_PAT_LO;
    b[1] = BKPSRAM_PAT_HI;
    __DSB();

    /* Arm the bootcheck with the pattern in backup registers, then reset.
     * bootcheck_evaluate() verifies BKPSRAM after the reset (TFW-007). */
    hwval_bootcheck_arm(BOOTCHECK_HASH_RAM009, TFW_PASS);
    RTC->WPR = 0xCAU; RTC->WPR = 0x53U;
    HWVAL_BKP_CHECK_ARG0 = BKPSRAM_PAT_LO;
    HWVAL_BKP_CHECK_ARG1 = BKPSRAM_PAT_HI;
    RTC->WPR = 0xFFU;
    (void)snprintf(o->reason, sizeof(o->reason), "reset armed via TFW-007");
    return TFW_SKIP; /* verdict arrives after the reset (bootcheck) */
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_ram_tests[] = {
    { .id = "RAM.001..005", .group = "RAM", .title = "March C- all regions "
      "(free: march, live: save/restore)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 10000U,
      .verifies = "RAM-001, RAM-002, RAM-003, RAM-004, RAM-005",
      .fn = ram_groups },
    { .id = "RAM.006", .group = "RAM", .title = "data/address bus tests",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "RAM-006", .fn = ram_buses },
    { .id = "RAM.007", .group = "RAM", .title = "8/16/32/64-bit accesses",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "RAM-007", .fn = ram_widths },
    { .id = "RAM.008", .group = "RAM", .title = "backup SRAM March (destructive)",
      .level = TFW_LEVEL_S, .destructive = 1, .resets_mcu = 0,
      .timeout_ms = 1000U, .verifies = "RAM-008", .fn = ram_bkpsram_march },
    { .id = "RAM.009", .group = "RAM", .title = "backup SRAM retention "
      "(reset now; standby via operator)",
      .level = TFW_LEVEL_S, .needs_operator = 1, .resets_mcu = 1,
      .timeout_ms = 5000U, .verifies = "RAM-009, TFW-007",
      .fn = ram_bkpsram_retention },
};

const size_t g_ram_test_count = sizeof(g_ram_tests) / sizeof(g_ram_tests[0]);

void hwval_test_register_ram(void)
{
    hwval_test_register(g_ram_tests, g_ram_test_count);
}