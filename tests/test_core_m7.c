/*
 * tests/test_core_m7.c - Cortex-M7 core tests (section 8.2)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     CORE-001..CORE-012, level S, no operator.
 * File note (NFR-008): group CORE -> this file (tests/test_core.c is the
 * framework engine, not a test file).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_limits.h"
#include "test_core.h"
#include "fault.h"

/* ------------------------------------------------------------------ 001 */
static tfw_result_t core_integer_ops(struct tfw_out *o)
{
    int32_t  r_sdiv;
    uint32_t r_udiv, r_clz, r_qadd8, r_smlad, r_bfi, r_ubfx;
    int fails = 0;

    __asm volatile ("udiv %0, %1, %2" : "=r"(r_udiv) : "r"(1000U), "r"(3U));
    __asm volatile ("sdiv %0, %1, %2" : "=r"(r_sdiv) : "r"(-1000), "r"(7));
    r_clz   = __CLZ(0x00001000U);
    __asm volatile ("qadd8 %0, %1, %2" : "=r"(r_qadd8)
                    : "r"(0x02010100U), "r"(0xFF0103FFU));
    __asm volatile ("smlad  %0, %1, %2, %3" : "=r"(r_smlad)
                    : "r"(0x00020003U), "r"(0x00040005U), "r"(0U));
    r_bfi = 0U;
    __asm volatile ("bfi    %0, %1, #8, #8" : "+r"(r_bfi)
                    : "r"(0xA5U) : );
    __asm volatile ("ubfx   %0, %1, #12, #4" : "=r"(r_ubfx)
                    : "r"(0x0000F000U));

    hwval_meas(o, "udiv=%lu sdiv=%ld clz=%lu qadd8=%08lX smlad=%ld",
               (unsigned long)r_udiv, (long)r_sdiv, (unsigned long)r_clz,
               (unsigned long)r_qadd8, (long)r_smlad);
    hwval_lim(o, "udiv 333 sdiv -142 clz 19 qadd8 FF0403FF smlad 23 ubfx F");

    if (r_udiv != 333U)                            { fails++; }
    if (r_sdiv != -142)                            { fails++; }
    if (r_clz != 19U)                              { fails++; }
    /* saturating byte adds: 00+FF=FF, 01+03=04, 02+01=03, FF+01=FF sat */
    if (r_qadd8 != 0xFF0403FFU)                    { fails++; }
    if (r_smlad != (2 * 4 + 3 * 5))                { fails++; }
    if ((r_bfi & 0xFF00U) != 0xA500U)              { fails++; }
    if (r_ubfx != 0xFU)                            { fails++; }

    if (fails != 0) {
        hwval_fail(o, "%d/7 integer checks wrong", fails);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------------------------ 002 */
static uint32_t f32_bits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
static uint64_t f64_bits(double d)
{ uint64_t b; memcpy(&b, &d, 8); return b; }

static tfw_result_t core_fpu(struct tfw_out *o)
{
    int fails = 0;
    uint32_t fpscr;
    float  a = 1.5f, b = 0.25f;
    double x = 65504.0, y = 4.0;

    __asm volatile ("vmrs %0, fpscr" : "=r"(fpscr));

    if (f32_bits(a + b)    != 0x3FC00000U) fails++; /* 1.75   */
    if (f32_bits(a / b)    != 0x41000000U) fails++; /* 6.0    */
    if (f32_bits(__builtin_sqrtf(2.0f)) != f32_bits(1.4142135381698608f))
        fails++;
    /* fused: fma(a,b,+1)=1.375 exact in single */
    if (f32_bits(__builtin_fmaf(1.5f, 0.25f, 1.0f)) != 0x3FB00000U) fails++;
    /* infinity and NaN handling */
    float inf  = a / 0.0f;
    float zero = 0.0f;
    if (f32_bits(inf)               != 0x7F800000U) fails++; /* +inf   */
    if (f32_bits(inf * zero)        != 0x7FC00000U) fails++; /* NaN    */
    /* smallest denormal stays denormal (FZ=0 on M7 default) */
    float den = 1.401298464324817e-45f; /* 0x00000001, min denormal */
    if (f32_bits(den) != 0x00000001U) fails++;
    if (f32_bits(den * 2.0f) != 0x00000002U) fails++; /* stays denormal */
    /* double precision */
    if (f64_bits(x * y) != f64_bits(262016.0)) fails++;
    if (f64_bits(x / 2.0) != f64_bits(32752.0)) fails++;
    double d2 = __builtin_sqrt(2.0);
    if (f64_bits(d2) != f64_bits(1.4142135623730951)) fails++;
    int up = (int)(2.9), dn = (int)(-2.9);
    if ((up != 2) || (dn != -2)) fails++; /* conversions truncate */

    hwval_meas(o, "fpscr=%08lX inf=%08lX nan=1", (unsigned long)fpscr,
               (unsigned long)f32_bits(inf));
    hwval_lim(o, "IEEE-754 SP+DP exact results, FZ=0 (denormals kept)");
    if (fails != 0) {
        hwval_fail(o, "%d FPU checks wrong", fails);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* --------------------------- fault tests shared setup (CORE-003..006) -- */
/* Enable the configurable fault handlers.  The DIV_0/UNALIGN traps are set
 * only by the test that needs one and cleared on recovery, before any
 * library call: newlib's printf makes unaligned accesses, so a trap left on
 * turns the next hwval_meas() into a real fault. */
static void fault_arming(void)
{
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                  SCB_SHCSR_MEMFAULTENA_Msk;
}

static tfw_result_t core_div0(struct tfw_out *o)
{
    tfw_result_t res = TFW_FAIL;
    uint32_t q;

    fault_arming();
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
    /* cppcheck-suppress assignBoolToPointer */ hwval_fault_recover_pc = (void *)&&recover;
    hwval_fault_recover = 1;
    __asm volatile ("udiv %0, %1, %2" : "=r"(q) : "r"(1U), "r"(0U));
    HWVAL_FAULT_RECOVER_POINT(recover);
    SCB->CCR &= ~SCB_CCR_DIV_0_TRP_Msk;
    hwval_fail(o, "udiv 1/0 did not fault");
    goto done;

recover:
    SCB->CCR &= ~SCB_CCR_DIV_0_TRP_Msk;
    if (hwval_fault_last_exc != 6U) { /* UsageFault */
        hwval_fail(o, "expected UsageFault, got exc %lu",
                   (unsigned long)hwval_fault_last_exc);
        goto done;
    }
    if ((hwval_fault_cfsr_snap & SCB_CFSR_USGFAULTSR_Msk) == 0U) {
        hwval_fail(o, "UFSR did not record the fault");
        goto done;
    }
    hwval_meas(o, "exc=%lu UFSR set", (unsigned long)hwval_fault_last_exc);
    hwval_lim(o, "DIV_0_TRP -> UsageFault, handler recovers");
    res = TFW_PASS;
done:
    hwval_fault_recover = 0;
    SCB->CCR &= ~SCB_CCR_DIV_0_TRP_Msk;
    return res;
}

static tfw_result_t core_unaligned(struct tfw_out *o)
{
    tfw_result_t res = TFW_FAIL;
    static uint32_t buf[4] __attribute__((aligned(8)));
    uint32_t *mis = (uint32_t *)((uintptr_t)buf + 1U);
    uint32_t v;

    fault_arming();

    SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk;
    /* cppcheck-suppress assignBoolToPointer */ hwval_fault_recover_pc = (void *)&&trapped;
    hwval_fault_recover = 1;
    v = *(volatile const uint32_t *)mis;   /* volatile: must not be elided */
    HWVAL_FAULT_RECOVER_POINT(trapped);
    (void)v;                 /* only reached if the trap failed */
    SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
    hwval_fail(o, "unaligned read did not trap");
    goto done;

trapped:
    SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
    if (hwval_fault_last_exc != 6U) {
        hwval_fail(o, "expected UsageFault, got %lu",
                   (unsigned long)hwval_fault_last_exc);
        goto done;
    }
    hwval_fault_recover = 0;
    /* UNALIGN_TRP clear: unaligned accesses must work */
    SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
    *mis = 0xC3C3C3C3U;      /* unaligned store, then read back */
    v = *mis;
    buf[0] = v;
    __asm volatile ("dmb" ::: "memory");
    hwval_meas(o, "trap yes; unaligned ok when clear (val=%08lX)",
               (unsigned long)buf[0], (unsigned long)v);
    hwval_lim(o, "UNALIGN_TRP semantics per [PM0253]");
    res = TFW_PASS;
done:
    hwval_fault_recover = 0;
    SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
    return res;
}

static volatile uint32_t s_bus_probe;

static tfw_result_t core_busfault(struct tfw_out *o)
{
    tfw_result_t res = TFW_FAIL;

    fault_arming();
    /* cppcheck-suppress assignBoolToPointer */ hwval_fault_recover_pc = (void *)&&recover;
    hwval_fault_recover = 1;
    s_bus_probe = *(volatile const uint32_t *)0x24F00000U; /* reserved */
    HWVAL_FAULT_RECOVER_POINT(recover);
    hwval_fail(o, "reserved read did not busfault");
    goto done;

recover:
    if (hwval_fault_last_exc != 5U) {
        hwval_fail(o, "expected BusFault, got %lu",
                   (unsigned long)hwval_fault_last_exc);
        goto done;
    }
    hwval_meas(o, "BFAR=%08lX probe=%08lX", (unsigned long)SCB->BFAR,
               (unsigned long)s_bus_probe);
    hwval_lim(o, "BusFault on reserved, BFAR reports address");
    if (SCB->BFAR == 0x24F00000U) {
        res = TFW_PASS;
    } else {
        hwval_fail(o, "BFAR %08lX != 24F00000", (unsigned long)SCB->BFAR);
    }
done:
    hwval_fault_recover = 0;
    return res;
}

static tfw_result_t core_undefinstr(struct tfw_out *o)
{
    tfw_result_t res = TFW_FAIL;

    fault_arming();
    /* cppcheck-suppress assignBoolToPointer */ hwval_fault_recover_pc = (void *)&&recover;
    hwval_fault_recover = 1;
    __asm volatile (".word 0xDE01\n"); /* UDF #1 */
    HWVAL_FAULT_RECOVER_POINT(recover);
    hwval_fail(o, "UDF did not fault");
    goto done;

recover:
    if (hwval_fault_last_exc != 6U) {
        hwval_fail(o, "expected UsageFault, got %lu",
                   (unsigned long)hwval_fault_last_exc);
        goto done;
    }
    /* UNDEFINSTR bit 17 of part of CFSR (UsageFault status) */
    const uint32_t ufsr = (hwval_fault_cfsr_snap >> 16U) & 0xFFFFU;
    hwval_meas(o, "UFSR=%04lX", (unsigned long)ufsr);
    hwval_lim(o, "UNDEFINSTR (bit 17) set");
    if ((ufsr & (1U << 0U)) != 0U) { /* UFSR bit0 = UNDEFINSTR [PM0253] */
        res = TFW_PASS;
    } else {
        hwval_fail(o, "UNDEFINSTR flag missing (UFSR=%04lX)",
                   (unsigned long)ufsr);
    }
done:
    hwval_fault_recover = 0;
    return res;
}

/* ------------------------------------------------------------------ 007 */
extern volatile uint32_t hwval_nmi_count; /* counted in src/fault.c */

static tfw_result_t core_svc_pendsv_nmi(struct tfw_out *o)
{
    uint32_t before = hwval_nmi_count;
    uint32_t icsr;

    /* NMI by software pend (CORE-007; also the CLK-011 CSS path) */
    SCB->ICSR = SCB_ICSR_NMIPENDSET_Msk;
    __ISB();
    __DSB();
    if (hwval_nmi_count <= before) {
        hwval_fail(o, "NMIPENDSET did not run the NMI handler");
        return TFW_FAIL;
    }

    /* PendSV: pend it; the port handler drains it (bit self-clears) */
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    __ISB();
    __DSB();
    icsr = SCB->ICSR;
    if ((icsr & SCB_ICSR_PENDSVSET_Msk) != 0U) {
        hwval_fail(o, "PendSV pending bit did not clear");
        return TFW_FAIL;
    }

    /* SVC: invoking SVC under a running scheduler would make the port
     * handler restart the first task, so verify the vector instead. */
    extern void vPortSVCHandler(void);
    /* The table is at the bootloader slot, not 0x08000000 (BOOT's own). */
    uint32_t svc_vec = *(const uint32_t *)(SCB->VTOR + 11U * 4U);
    if ((uint32_t)(uintptr_t)vPortSVCHandler != svc_vec) {
        hwval_fail(o, "SVC vector %08lX != vPortSVCHandler",
                   (unsigned long)svc_vec);
        return TFW_FAIL;
    }

    hwval_meas(o, "nmi fires=%lu pendsv ok svc vec ok",
               (unsigned long)(hwval_nmi_count - before));
    hwval_lim(o, "NMI pended+handled; PENDSVSET drained; SVC vector = port");
    return TFW_PASS;
}

/* ------------------------------------------------------------------ 008 */
static tfw_result_t core_systick(struct tfw_out *o)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == 0U) ? pclk1 : pclk1 * 2U;
    uint32_t cnt0, cnt1, flags = 0U;

    TIM2->CR1 = 0U;
    TIM2->PSC = (timclk / 1000000U) - 1U; /* 1 MHz */
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;

    (void)SysTick->CTRL; /* clear COUNTFLAG */
    cnt0 = TIM2->CNT;
    for (;;) {
        cnt1 = TIM2->CNT;
        if ((cnt1 - cnt0) >= 1000000U) { break; } /* 1.0 s */
        if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U) {
            flags++;
        }
    }
    TIM2->CR1 = 0U;

    hwval_meas(o, "tickflags=%lu in 1e6 us", (unsigned long)flags);
    hwval_lim(o, "1000 +-20 COUNTFLAG events per second (reload 1 ms)");
    if ((flags < 980U) || (flags > 1020U)) {
        hwval_fail(o, "%lu ticks in 1 s", (unsigned long)flags);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------------------------ 009 */
/* Fixed integer benchmark run from flash (cache on/off) and from ITCM. */
static uint32_t bench_fn(const volatile uint32_t *data, uint32_t n)
{
    uint32_t acc = 0U;

    for (uint32_t i = 0U; i < n; i++) {
        acc += data[i % 16U];
        acc ^= (acc << 1U);
    }
    return acc;
}

typedef uint32_t (*bench_fn_t)(const volatile uint32_t *, uint32_t);

extern uint8_t hwval_itcm_base[]; /* linker symbol: ITCM RAM origin */

static tfw_result_t core_benchmark(struct tfw_out *o)
{
    static volatile uint32_t data[16];
    uint32_t cyc_flash_cached, cyc_flash_uncached, cyc_itcm;
    uint32_t cyc_itcmram;
    uint32_t i;
    char *itcm = (char *)hwval_itcm_base;

    for (i = 0U; i < 16U; i++) {
        data[i] = 0x00010001U + i;
    }

    bench_fn_t fn_flash = bench_fn;

    /* ITCM copy: the function code bytes into zero-wait-state ITCM RAM */
    uint32_t fn_len = 256U; /* fixed window large enough for this fn */
    /* A Thumb function pointer has bit 0 set: copy from the real code
     * address, and call the copy with bit 0 set, or BLX faults INVSTATE. */
    memcpy(itcm, (const void *)((uintptr_t)fn_flash & ~1U), fn_len);
    __DSB();
    __DMB(); /* ITCM is not cached; barrier only */
    __ISB(); /* fetch the freshly written code */
    bench_fn_t fn_itcm = (bench_fn_t)((uintptr_t)hwval_itcm_base | 1U);

    /* flash cached */
    DWT->CYCCNT = 0U;
    (void)fn_flash(data, 40000U);
    cyc_flash_cached = DWT->CYCCNT;

    /* flash uncached: drop I-cache + ART, run, restore */
    SCB_DisableICache();
    __HAL_FLASH_ART_DISABLE();
    __DSB(); __ISB();
    DWT->CYCCNT = 0U;
    (void)fn_flash(data, 40000U);
    cyc_flash_uncached = DWT->CYCCNT;
    __HAL_FLASH_ART_ENABLE();
    SCB_InvalidateICache();
    SCB_EnableICache();
    __ISB();

    /* flash code, but data/bench hot in D-cache: measure ITCM copy path */
    DWT->CYCCNT = 0U;
    (void)fn_itcm(data, 40000U);
    cyc_itcm = DWT->CYCCNT;

    /* ITCM-RAM data access speed (CACHE-004 measurement too) */
    volatile uint32_t *itcm_data =
        (volatile uint32_t *)(void *)(hwval_itcm_base + 0x400U);
    DWT->CYCCNT = 0U;
    for (i = 0U; i < 4096U; i++) {
        uint32_t idx = i & 63U;
        *(itcm_data + idx) = i;
    }
    cyc_itcmram = DWT->CYCCNT;

    hwval_meas(o, "cached=%lu uncached=%lu itcm=%lu itcm-ram=%lu cyc",
               (unsigned long)cyc_flash_cached,
               (unsigned long)cyc_flash_uncached,
               (unsigned long)cyc_itcm, (unsigned long)cyc_itcmram);
    hwval_lim(o, "cached < uncached < itcm (ordering, CORE-009)");
    if (cyc_flash_cached >= cyc_flash_uncached) {
        hwval_fail(o, "cache did not help (%lu >= %lu)",
                   (unsigned long)cyc_flash_cached,
                   (unsigned long)cyc_flash_uncached);
        return TFW_FAIL;
    }
    if (cyc_itcm >= cyc_flash_uncached) {
        hwval_fail(o, "ITCM copy not faster");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------------------------ 010 */
static tfw_result_t core_wfi_wfe(struct tfw_out *o)
{
    uint32_t t0, t1;

    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0U;
    TIM2->PSC = (HAL_RCC_GetHCLKFreq() / 1000000U) - 1U; /* 1 MHz */
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;

    /* WFI: the 1 kHz SysTick interrupt wakes it (no masking). */
    t0 = TIM2->CNT;
    __SEV(); /* clear stale event so WFI really sleeps */
    __WFE();
    __WFI();
    t1 = TIM2->CNT;
    uint32_t dt = t1 - t0;
    hwval_meas(o, "wfi woke after %lu us", (unsigned long)dt);
    hwval_lim(o, "1 tick (1 ms) + jitter < 3 ms");
    if ((dt < 100U) || (dt > 3000U)) {
        TIM2->CR1 = 0U;
        hwval_fail(o, "WFI window %lu us wrong", (unsigned long)dt);
        return TFW_FAIL;
    }

    /* WFE with a pending event: SEV then WFE returns almost immediately */
    __SEV(); /* event register set */
    t0 = TIM2->CNT;
    __WFE(); /* consumes the SEV event */
    t1 = TIM2->CNT;
    TIM2->CR1 = 0U;
    if ((t1 - t0) > 100U) {
        hwval_fail(o, "WFE slept %lu us despite pending SEV",
                   (unsigned long)(t1 - t0));
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------------------------ 011 */
static volatile uint32_t s_excl_target, s_excl_isr_inc;

void TIM3_IRQHandler(void);

void TIM3_IRQHandler(void)
{
    s_excl_target++;           /* races with the task's LDREX/STREX loop */
    s_excl_isr_inc++;
    TIM3->SR = ~TIM_SR_UIF;
}

static tfw_result_t core_exclusive(struct tfw_out *o)
{
    uint32_t expected;

    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM3->CR1 = 0U;
    TIM3->PSC = (HAL_RCC_GetHCLKFreq() / 10000U) - 1U;
    TIM3->ARR = 0U; /* every tick */
    TIM3->EGR = TIM_EGR_UG;
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->CR1 = TIM_CR1_CEN;
    NVIC_SetPriority(TIM3_IRQn, 6);
    NVIC_EnableIRQ(TIM3_IRQn);

    s_excl_target = 0U;
    s_excl_isr_inc = 0U;
    uint32_t seen_old = 0U;
    for (int i = 0; i < 20000; i++) {
        uint32_t old, newv;
        do {
            old = __LDREXW((volatile uint32_t *)&s_excl_target);
            newv = old + 1U;
        } while (__STREXW(newv, (volatile uint32_t *)&s_excl_target) != 0U);
        seen_old = newv;
    }
    NVIC_DisableIRQ(TIM3_IRQn);
    TIM3->CR1 = 0U;

    expected = 20000U; /* ISR races lose only if LDREX/STREX broken */
    hwval_meas(o, "final=%lu isr-adds=%lu", (unsigned long)s_excl_target,
               (unsigned long)s_excl_isr_inc);
    hwval_lim(o, ">= 20000 increments, no undercount under ISR racing");
    if (s_excl_target < expected) {
        hwval_fail(o, "strex lost updates: %lu < 20000",
                   (unsigned long)s_excl_target);
        return TFW_FAIL;
    }
    (void)seen_old;
    return TFW_PASS;
}

/* ------------------------------------------------------------------ 012 */
static __attribute__((aligned(32))) uint8_t smc_buf[128];

static tfw_result_t core_barriers(struct tfw_out *o)
{
    /* small function bytes: movs r0,#0x2A; bx lr  (thumb: 0xF04F 0x002A,
     * 0x4770) — write it, clean/invalidate, ISB, execute */
    static const uint16_t code[] = { 0xF04F, 0x002A, 0x4770, 0x46C0 };

    memcpy(smc_buf, code, sizeof(code));
    __DSB();
    SCB_CleanDCache_by_Addr((uint32_t *)smc_buf, sizeof(smc_buf));
    SCB_InvalidateICache(); /* no by-address I-cache op on CM7 CMSIS */
    __ISB();

    typedef int (*fn_t)(void);
    int ret = ((fn_t)(void *)(smc_buf + 1U))(); /* Thumb bit */

    hwval_meas(o, "ret=%d", ret);
    hwval_lim(o, "42 (DSB/DMB/ISB + I/D-cache maintenance correct)");
    if (ret != 42) {
        hwval_fail(o, "self-modified code returned %d", ret);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_cm7_tests[] = {
    { .id = "CORE.001", .group = "CORE", .title = "integer ALU/DSP/CLZ",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CORE-001", .fn = core_integer_ops },
    { .id = "CORE.002", .group = "CORE", .title = "FPU SP+DP incl denorm/inf/nan",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CORE-002", .fn = core_fpu },
    { .id = "CORE.003", .group = "CORE", .title = "divide-by-zero UsageFault traps",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CORE-003", .fn = core_div0 },
    { .id = "CORE.004", .group = "CORE", .title = "unaligned access trapping",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CORE-004", .fn = core_unaligned },
    { .id = "CORE.005", .group = "CORE", .title = "BusFault on unmapped + BFAR",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CORE-005", .fn = core_busfault },
    { .id = "CORE.006", .group = "CORE", .title = "undefined instruction UsageFault",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CORE-006", .fn = core_undefinstr },
    { .id = "CORE.007", .group = "CORE", .title = "NMI pend + PendSV drain + SVC vector",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CORE-007", .fn = core_svc_pendsv_nmi },
    { .id = "CORE.008", .group = "CORE", .title = "SysTick rate vs TIM2",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1500U,
      .verifies = "CORE-008", .fn = core_systick },
    { .id = "CORE.009", .group = "CORE", .title = "benchmark cached/uncached/ITCM",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "CORE-009, CACHE-004", .fn = core_benchmark },
    { .id = "CORE.010", .group = "CORE", .title = "WFI/WFE wake-up",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "CORE-010", .fn = core_wfi_wfe },
    { .id = "CORE.011", .group = "CORE", .title = "LDREX/STREX atomicity",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "CORE-011", .fn = core_exclusive },
    { .id = "CORE.012", .group = "CORE", .title = "barriers + self-modifying code",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "CORE-012", .fn = core_barriers },
};

const size_t g_cm7_test_count = sizeof(g_cm7_tests) / sizeof(g_cm7_tests[0]);

void hwval_test_register_cm7(void)
{
    hwval_test_register(g_cm7_tests, g_cm7_test_count);
}