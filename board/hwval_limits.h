/**
 * @file    hwval_limits.h
 * @brief   Named pass/fail limits used by the tests, with their sources.
 * @details Every numeric pass/fail limit used by tests is defined here.
 *          Sources: [DS11532] electrical characteristics, [RM0410] clock and
 *          flash chapters, Hardware.md measured values, SRS sections.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     TFW-008
 */

#ifndef HWVAL_LIMITS_H
#define HWVAL_LIMITS_H

/* --- clock limits (RM0410 / DS11532 tables) ------------------------------- */
#define HWVAL_LIM_SYSCLK_MAX_HZ     216000000U /**< Max SYSCLK, Hz (scale 1 + OD, rev Z). */
#define HWVAL_LIM_HSI_TOL_PPM       10000U     /**< HSI tolerance, ppm (CLK-001: +-1 %). */
#define HWVAL_LIM_HSEBYP_TOL_PPM    1000U      /**< HSE bypass tolerance, ppm (CLK-002). */
#define HWVAL_LIM_LSE_TOL_PPM       100U       /**< LSE tolerance, ppm (CLK-004). */
#define HWVAL_LIM_LSI_MIN_HZ        17000U     /**< LSI minimum, Hz (CLK-005). */
#define HWVAL_LIM_LSI_MAX_HZ        47000U     /**< LSI maximum, Hz (CLK-005). */
#define HWVAL_LIM_RTCACC_PPM        100U       /**< RTC accuracy window, ppm (RTC-001). */

/* --- supply (PWR-001/002) -------------------------------------------------- */
#define HWVAL_LIM_VDDA_MV           3300U      /**< Nominal VDDA, mV. */
#define HWVAL_LIM_VDDA_TOL_MV       165U       /**< VDDA tolerance, mV (+-5 %). */

/* --- ADC (ADC-001/ADC-010, DS11532 5.19/5.21) ----------------------------- */
#define HWVAL_LIM_ADC_INL_LSB       5          /**< 12-bit total INL, LSB. */
#define HWVAL_LIM_ADC_DNL_LSB       1          /**< DNL, LSB. */
#define HWVAL_LIM_VREFINT_TOL_PCT   2          /**< VREFINT vs factory cal, %. */
#define HWVAL_LIM_TSENSOR_MIN_C     0          /**< Temp sensor bench min, C (ADC-002). */
#define HWVAL_LIM_TSENSOR_MAX_C     60         /**< Temp sensor bench max, C (ADC-002). */
#define HWVAL_LIM_ADC_NOISE_LSB     2          /**< Noise std dev limit, LSB (ADC-010). */

/* --- flash timings (DS11532 flash characteristics tables) ------------------ */
#define HWVAL_LIM_SECTOR_ERASE_MAX_MS 2000     /**< 256 KB sector erase, ms (FLS-009). */
#define HWVAL_LIM_PROG64_MAX_US     1200       /**< 64-bit word program, us (FLS-009). */

/* --- watchdog (WDG-001) ----------------------------------------------------- */
#define HWVAL_LIM_LSI_WDG_TOL_PCT   60         /**< IWDG timing tolerance, % (LSI 17-47 kHz). */

/* --- RAM (RAM-001..RAM-007) ------------------------------------------------- */
#define HWVAL_LIM_RAM_MARCH_PAT     0xA5A5A5A5U /**< March C- element pattern. */

/* --- UART/SPI timing (UART-001, SPI-001) ------------------------------------ */
/** Baud error limit, %; budget is 2.5 % with 16x oversampling. */
#define HWVAL_LIM_BAUD_TOL_PCT      3

/* Context switching (section 5 self-tests, FRT-024) has no limit beyond a
 * plausible non-zero latency. Observed: irq-latency 33 cycles, context switch
 * 708 cycles. */

/* --- DEV-004 package register code(s) (CL-014: confirm on bench) ---------- */
#define HWVAL_LIM_PKG_LQFP144      0x6U /**< LQFP144 package code. */
#define HWVAL_LIM_PKG_LQFP144_ALT  0x7U /**< Alternative LQFP144 package code. */

/* --- DEV-006 factory-calibration bands, raw 12-bit counts. Derived from the
 *     DS11532 typical values at VDDA 3.3 V: VREFINT ~= 1.212 V,
 *     TS_CAL1 @ 30 C ~= 0.76 V, TS_CAL2 @ 110 C ~= 0.96 V. ---------------- */
#define HWVAL_LIM_VREFINT_CAL_MIN  1300U /**< VREFINT_CAL minimum, counts. */
#define HWVAL_LIM_VREFINT_CAL_MAX  1750U /**< VREFINT_CAL maximum, counts. */
#define HWVAL_LIM_TS_CAL1_MIN      800U  /**< TS_CAL1 minimum, counts. */
#define HWVAL_LIM_TS_CAL1_MAX      1100U /**< TS_CAL1 maximum, counts. */
#define HWVAL_LIM_TS_CAL2_MIN      1000U /**< TS_CAL2 minimum, counts. */
#define HWVAL_LIM_TS_CAL2_MAX      1350U /**< TS_CAL2 maximum, counts. */

#endif /* HWVAL_LIMITS_H */
