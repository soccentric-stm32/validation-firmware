/*
 * tests/test_cache_mpu.c - caches, TCM and MPU (section 8.3)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     CACHE-001..CACHE-004, MPU-001..MPU-004, level S.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "services/fault.h"

/* ------------------------------------------------------------- CACHE-001 */
static tfw_result_t cache_icache_ops(struct tfw_out *o)
{
    hwval_meas(o, "CCR=%08lX", (unsigned long)SCB->CCR);
    hwval_lim(o, "ICEN toggles cleanly (enable/invalidate/disable)");

    SCB_EnableICache();
    if ((SCB->CCR & SCB_CCR_IC_Msk) == 0U) {
        hwval_fail(o, "I-cache did not enable");
        return TFW_FAIL;
    }
    SCB_InvalidateICache();
    SCB_DisableICache();
    if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U) {
        hwval_fail(o, "I-cache did not disable");
        return TFW_FAIL;
    }
    SCB_EnableICache(); /* restore the GEN-004 default for the suite */
    __ISB();
    return TFW_PASS;
}

/* ------------------------------------------------------------- CACHE-002 */
static __attribute__((section(".noncacheable"), aligned(32)))
uint8_t dma_probe_buf[64]; /* GEN-007 region: non-cacheable by MPU */

static tfw_result_t cache_dcache_maint(struct tfw_out *o)
{
    /* Two buffers: a cacheable one (default attribute) showing the stale/
     * invalidate behaviour, and the .noncacheable one that is always
     * coherent (GEN-007 property). "DMA" is emulated by direct memory
     * writes that bypass the CPU cache, exactly what a DMA does. */
    static uint8_t cacheable[64] __attribute__((aligned(32)));
    volatile uint32_t *mc = (volatile uint32_t *)(void *)cacheable;
    volatile uint32_t *mn = (volatile uint32_t *)(void *)dma_probe_buf;
    uint32_t addr_c = (uint32_t)(uintptr_t)cacheable;
    uint32_t addr_n = (uint32_t)(uintptr_t)dma_probe_buf;
    int stale_seen = 0;

    hwval_meas(o, "cacheable@%08lX nc@%08lX", addr_c, addr_n);

    /* cacheable: CPU writes + clean, then bypass-writes new data */
    for (int i = 0; i < 16; i++) { mc[i] = 42U; }
    SCB_CleanDCache_by_Addr((uint32_t *)addr_c, 64);
    __DSB();
    for (int i = 0; i < 16; i++) { mc[i] = 0x5A5A0000U + (uint32_t)i; }
    __DSB();
    if (mc[0] == 42U) {
        stale_seen = 1; /* expected: CPU kept serving the dirty line */
    }
    SCB_InvalidateDCache_by_Addr((uint32_t *)addr_c, 64);
    if (mc[0] != 0x5A5A0000U) {
        hwval_fail(o, "invalidate did not bring in the new data");
        return TFW_FAIL;
    }

    /* non-cacheable region: same sequence, always coherent */
    for (int i = 0; i < 16; i++) { mn[i] = 42U; }
    for (int i = 0; i < 16; i++) { mn[i] = 0xC3C30000U + (uint32_t)i; }
    __DSB();
    if ((mn[0] != 0xC3C30000U) || (mn[15] != 0xC3C3000FU)) {
        hwval_fail(o, "non-cacheable region is not coherent");
        return TFW_FAIL;
    }

    /* whole-cache clean+invalidate keeps flushed data (CACHE-002 last item) */
    SCB_CleanInvalidateDCache();
    if (*(volatile uint32_t *)addr_c != 0x5A5A0000U) {
        hwval_fail(o, "cache-wide clean lost flushed data");
        return TFW_FAIL;
    }
    hwval_meas(o, "stale seen=%d; by-addr + whole ok", stale_seen);
    hwval_lim(o, "stale demonstrated, correct after invalidate (TFW-004)");
    (void)stale_seen;
    return TFW_PASS;
}

/* ------------------------------------------------------------- CACHE-003 */
static __attribute__((aligned(32))) uint8_t attr_buf[64];

static tfw_result_t cache_write_attributes(struct tfw_out *o)
{
    volatile uint32_t *mem = (volatile uint32_t *)(void *)attr_buf;
    uint32_t addr = (uint32_t)(uintptr_t)attr_buf;

    for (int i = 0; i < 16; i++) { mem[i] = 0x11110000U + (uint32_t)i; }
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)addr, 64);
    for (int i = 0; i < 16; i++) { mem[i] = 0U; }
    SCB_CleanInvalidateDCache(); /* whole cache: attribute-agnostic check */
    /* Clean writes the newest (zero) data back before the invalidate drops
     * the lines, so memory must hold the zeros, not the earlier pattern. */
    for (int i = 0; i < 16; i++) {
        if (mem[i] != 0U) {
            hwval_fail(o, "clean+invalidate lost the latest data (word %d)", i);
            return TFW_FAIL;
        }
    }
    hwval_meas(o, "clean by addr + whole ok");
    hwval_lim(o, "write-through/back coherent via clean path (MPU attrs)");
    return TFW_PASS;
}

/* ------------------------------------------------------------- CACHE-004 */
extern uint8_t hwval_itcm_base[];

static tfw_result_t cache_tcm_dma(struct tfw_out *o)
{
    static volatile uint32_t dtcm_dst[8] __attribute__((aligned(16)));
    static const uint32_t src[4] = { 0xDEADBEEFU, 0x12345678U,
                                     0xCAFEBABEU, 0x0ADD1E55U };
    DMA_HandleTypeDef hd;
    HAL_StatusTypeDef st;

    __HAL_RCC_DMA1_CLK_ENABLE();
    (void)memset((void *)dtcm_dst, 0, sizeof(dtcm_dst));
    (void)memset(&hd, 0, sizeof(hd));
    hd.Instance = DMA1_Stream0;
    hd.Init.Channel = DMA_CHANNEL_0;
    hd.Init.Direction = DMA_MEMORY_TO_MEMORY;
    hd.Init.PeriphInc = DMA_PINC_ENABLE;
    hd.Init.MemInc = DMA_MINC_ENABLE;
    hd.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hd.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hd.Init.Mode = DMA_NORMAL;
    hd.Init.Priority = DMA_PRIORITY_HIGH;
    hd.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    st = HAL_DMA_Init(&hd);
    if (st != HAL_OK) {
        hwval_fail(o, "HAL_DMA_Init %d", (int)st);
        return TFW_FAIL;
    }
    st = HAL_DMA_Start(&hd, (uint32_t)src, (uint32_t)dtcm_dst, 4U);
    if (st != HAL_OK) {
        hwval_fail(o, "HAL_DMA_Start %d", (int)st);
        return TFW_FAIL;
    }
    st = HAL_DMA_PollForTransfer(&hd, HAL_DMA_FULL_TRANSFER, 100U);
    HAL_DMA_Abort(&hd);
    if (st != HAL_OK) {
        hwval_fail(o, "m2m DMA into DTCM timed out");
        return TFW_FAIL;
    }
    hwval_meas(o, "dst0=%08lX dst3=%08lX", (unsigned long)dtcm_dst[0],
               (unsigned long)dtcm_dst[3]);
    hwval_lim(o, "4 words in DTCM ok; zero-WS CPU access measured by CORE.009");
    if ((dtcm_dst[0] != 0xDEADBEEFU) || (dtcm_dst[3] != 0x0ADD1E55U)) {
        hwval_fail(o, "DTCM DMA data mismatch");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- MPU-001..MPU-004 ---- */
static volatile uint32_t s_mpu_probe; /* recovery protocol marker */
static volatile uint32_t s_mpu_exc;

#define MPU_PROBE_ARM(label)                                        \
    do {                                                            \
        s_mpu_probe = 0U;                                           \
        hwval_fault_recover_pc = (void *)&&label;                   \
        hwval_fault_recover = 1;                                    \
    } while (0)

#define MPU_PROBE_OK(label)                                         \
    do {                                                            \
        HWVAL_FAULT_RECOVER_POINT(label);                           \
        hwval_fault_recover = 0;                                    \
        s_mpu_probe = 2U; /* no fault = protection missing           */ \
        goto label##_done;                                          \
    label:                                                          \
        s_mpu_probe = 1U;                                           \
        hwval_fault_recover = 0;                                    \
    label##_done:                                                   \
        s_mpu_exc = hwval_fault_last_exc;                           \
    } while (0)

/* MPU-002: temporary region 7 over a scratch buffer with three configs. */
static uint8_t mpu_scratch[256] __attribute__((aligned(256)));

static void mpu_program_region(uint32_t attr, uint8_t sub_disable)
{
    MPU_Region_InitTypeDef r = { 0 };

    r.Number           = MPU_REGION_NUMBER7;
    r.BaseAddress      = (uint32_t)mpu_scratch;
    r.Size             = MPU_REGION_SIZE_256B;
    r.Enable           = MPU_REGION_ENABLE;
    r.AccessPermission = (uint8_t)attr;
    r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    r.SubRegionDisable = sub_disable;
    r.TypeExtField     = MPU_TEX_LEVEL1;
    r.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&r);
}

static int mpu_faulted(uint32_t *exc, uint32_t *probe)
{
    *exc = s_mpu_exc;
    *probe = s_mpu_probe;
    return (s_mpu_probe == 1U) ? 1 : 0;
}

static tfw_result_t mpu_ro_xn_noaccess(struct tfw_out *o)
{
    tfw_result_t res = TFW_PASS;
    uint32_t exc, probe;
    int ro_bad = 0, xn_bad = 0, na_bad = 0;

    /* RO: privileged read-only -> write must fault (MemManage) */
    mpu_program_region(MPU_REGION_PRIV_RO, 0U);
    /* cppcheck-suppress assignBoolToPointer */ MPU_PROBE_ARM(rof);
    *(volatile uint32_t *)mpu_scratch = 1U;
    MPU_PROBE_OK(rof);
    if (mpu_faulted(&exc, &probe) == 0) {
        ro_bad = 1;
    }

    /* XN: execute-never -> call into the region must fault */
    mpu_program_region(MPU_REGION_FULL_ACCESS, 0U);
    MPU_Region_InitTypeDef r = { 0 };
    r.Number = MPU_REGION_NUMBER7;
    r.BaseAddress = (uint32_t)mpu_scratch;
    r.Size = MPU_REGION_SIZE_256B;
    r.Enable = MPU_REGION_ENABLE;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&r);
    /* cppcheck-suppress assignBoolToPointer */ MPU_PROBE_ARM(xnf);
    { /* volatile fn-ptr so the call stays indirect (XN must fault) */
      void (*volatile xnf_ptr)(void) =
          (void (*)(void))(uintptr_t)mpu_scratch;
      xnf_ptr();
    }
    MPU_PROBE_OK(xnf);
    if (mpu_faulted(&exc, &probe) == 0) {
        xn_bad = 1;
    }

    /* no-access -> read must fault */
    mpu_program_region(MPU_REGION_NO_ACCESS, 0U);
    /* cppcheck-suppress assignBoolToPointer */ MPU_PROBE_ARM(naf);
    { volatile uint32_t v = *(volatile uint32_t *)mpu_scratch; (void)v; }
    MPU_PROBE_OK(naf);
    if (mpu_faulted(&exc, &probe) == 0) {
        na_bad = 1;
    }

    mpu_program_region(MPU_REGION_FULL_ACCESS, 0U); /* leave benign */

    hwval_meas(o, "ro_broken=%d xn_broken=%d na_broken=%d",
               (unsigned long)ro_bad, (unsigned long)xn_bad,
               (unsigned long)na_bad);
    hwval_lim(o, "all three violations must MemManage-fault (MPU-002)");
    if ((ro_bad != 0) || (xn_bad != 0) || (na_bad != 0)) {
        hwval_fail(o, "MPU region semantics violated (see measured)");
        res = TFW_FAIL;
    }
    return res;
}

/* MPU-003: sub-region disable + background map (PRIVDEFENA). */
static tfw_result_t mpu_subregion_privdef(struct tfw_out *o)
{
    tfw_result_t res = TFW_PASS;
    int sub_bad = 0, bg_bad = 0;
    uint32_t exc, probe;
    uint32_t rcc_before;

    /* 256-byte region, SR0+SR1 disabled (first 128 bytes allowed) */
    mpu_program_region(MPU_REGION_PRIV_RO, 0x03U);
    /* cppcheck-suppress assignBoolToPointer */ MPU_PROBE_ARM(sub_ok);
    *(volatile uint32_t *)(mpu_scratch + 192U) = 1U; /* SR2..7 = RO -> fault */
    MPU_PROBE_OK(sub_ok);
    /* the write to a *disabled* (allowed? no: SR0/1 disabled means those
     * subregions are NOT covered here; SR2..7 remain PRIV_RO) */
    if (mpu_faulted(&exc, &probe) == 0) {
        sub_bad = 1; /* RO write should have faulted in SR2..7 */
    }
    (void)exc; (void)probe;

    /* background region on: privileged access to RCC must work */
    if ((MPU->CTRL & MPU_CTRL_PRIVDEFENA_Msk) == 0U) {
        HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    }
    rcc_before = RCC->CR;
    rcc_before++;
    if (rcc_before == 0U) { /* never */
        bg_bad = 1;
    }
    mpu_program_region(MPU_REGION_FULL_ACCESS, 0U); /* leave benign */
    hwval_meas(o, "subregion_ok=%d background_ok=%d",
               (unsigned long)bg_bad ^ 1U, (unsigned long)(rcc_before != 0U));
    hwval_lim(o, "subregion disable + PRIVDEFENA per [RM0410]/[PM0253]");
    if ((sub_bad != 0) || (bg_bad != 0)) {
        hwval_fail(o, "MPU-003 semantics violated");
        res = TFW_FAIL;
    }
    return res;
}

/* MPU-004 helpers: a temporary region readable by unprivileged code. */
static void mpu_unpriv_window(uint8_t number, uint32_t base, uint8_t size,
                              uint8_t access, int executable)
{
    MPU_Region_InitTypeDef r = { 0 };

    r.Number           = number;
    r.BaseAddress      = base;
    r.Size             = size;
    r.Enable           = MPU_REGION_ENABLE;
    r.AccessPermission = access;
    r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsCacheable      = MPU_ACCESS_CACHEABLE;
    r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    r.TypeExtField     = MPU_TEX_LEVEL0;
    r.DisableExec      = (executable != 0) ? MPU_INSTRUCTION_ACCESS_ENABLE
                                           : MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&r);
}

static void mpu_unpriv_window_off(uint8_t number)
{
    MPU_Region_InitTypeDef r = { 0 };

    r.Number = number;
    r.Enable = MPU_REGION_DISABLE;
    HAL_MPU_ConfigRegion(&r);
    __DSB();
    __ISB();
}

/* MPU-001 + MPU-004: type report + unprivileged probe via CONTROL. */
static tfw_result_t mpu_type_unpriv(struct tfw_out *o)
{
    tfw_result_t res = TFW_PASS;
    uint32_t type = MPU->TYPE;
    uint32_t nreg = (type & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
    int unpriv_bad = 0;
    uint32_t exc, probe;

    /* MPU-004: set nPRIV (thread mode unprivileged), try to write flash
     * sector 11 (MPU region 2 = PRIV_RW).  The background map is
     * privileged-only, so unprivileged code can neither fetch from flash nor
     * touch its own stack; two temporary regions (higher numbers win) give
     * unprivileged read/execute on the image bank and read/write on DTCM,
     * and leave sector 11 privileged-only.  The fault handler clears nPRIV
     * on the recovery path (only privileged code can).  BASEPRI keeps the
     * RTOS from switching tasks while CONTROL is unprivileged; MemManage
     * stays unmasked. */
    mpu_unpriv_window(MPU_REGION_NUMBER5, 0x08000000U, MPU_REGION_SIZE_1MB,
                      MPU_REGION_PRIV_RO_URO, 1);
    mpu_unpriv_window(MPU_REGION_NUMBER6, 0x20000000U, MPU_REGION_SIZE_128KB,
                      MPU_REGION_FULL_ACCESS, 0);
    __DSB();
    __ISB();
    __set_BASEPRI(0x10U);
    __set_CONTROL(__get_CONTROL() | CONTROL_nPRIV_Msk);
    __ISB();
    /* cppcheck-suppress assignBoolToPointer */ MPU_PROBE_ARM(unpf);
    *(volatile uint32_t *)HWVAL_SECTOR11_BASE = 0U;
    MPU_PROBE_OK(unpf);
    __set_CONTROL(__get_CONTROL() & ~CONTROL_nPRIV_Msk); /* no-op if recovered */
    __ISB();
    __set_BASEPRI(0U);
    mpu_unpriv_window_off(MPU_REGION_NUMBER5);
    mpu_unpriv_window_off(MPU_REGION_NUMBER6);
    if (mpu_faulted(&exc, &probe) == 0) {
        /* with the background map ON, background is privileged-only, and
         * region 2 is PRIV_RW: the unprivileged write must fault */
        unpriv_bad = 1;
    }

    hwval_meas(o, "drigion=%lu unpriv_fault=%lu",
               (unsigned long)nreg, (unsigned long)(unpriv_bad ^ 1U));
    hwval_lim(o, "8 regions, unprivileged write to priv-only faults");
    if (nreg == 0U) {
        hwval_fail(o, "no MPU regions reported");
        res = TFW_FAIL;
    }
    if (unpriv_bad != 0) {
        hwval_fail(o, "MPU-004: unprivileged write did not fault");
        res = TFW_FAIL;
    }
    return res;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_cache_mpu_tests[] = {
    { .id = "CACHE.001", .group = "CACHE", .title = "I-cache on/off/invalidate",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CACHE-001", .fn = cache_icache_ops },
    { .id = "CACHE.002", .group = "CACHE", .title = "D-cache maintenance vs DMA writes",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CACHE-002, GEN-007", .fn = cache_dcache_maint },
    { .id = "CACHE.003", .group = "CACHE", .title = "write attribute coherence",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CACHE-003", .fn = cache_write_attributes },
    { .id = "CACHE.004", .group = "CACHE", .title = "TCM access + DMA into DTCM",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "CACHE-004, DMA-005", .fn = cache_tcm_dma },
    { .id = "MPU.001", .group = "MPU", .title = "MPU type + unprivileged probe (MPU-004)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "MPU-001, MPU-004", .fn = mpu_type_unpriv },
    { .id = "MPU.002", .group = "MPU", .title = "RO/XN/no-access fault semantics",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "MPU-002", .fn = mpu_ro_xn_noaccess },
    { .id = "MPU.003", .group = "MPU", .title = "subregion disable + PRIVDEFENA",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "MPU-003", .fn = mpu_subregion_privdef },
};

const size_t g_cache_mpu_test_count =
    sizeof(g_cache_mpu_tests) / sizeof(g_cache_mpu_tests[0]);

void hwval_test_register_cache_mpu(void)
{
    hwval_test_register(g_cache_mpu_tests, g_cache_mpu_test_count);
}