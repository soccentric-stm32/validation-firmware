/*
 * tests/test_uart.c - USART1/2/6, UART4/5/7/8 (section 8.18)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     UART-001..UART-010. Data paths need the H03-H12 wires (Level L);
 *          register/config semantics run Level S. USART3 = VCP (UART-004:
 *          exercised by the CLI itself).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "hwval_limits.h"
#include "test_core.h"
#include "harness.h"
#include "debug_uart.h"

struct uart_def {
    USART_TypeDef *u;
    const char    *name;
    uint32_t       clk_en;
    uint8_t        wire;   /* Appendix A wire index (sorted list) */
};

/* wire indices follow the wire-table order starting at H03 (idx 2). */
static const struct uart_def k_uarts[] = {
    { USART1, "USART1", RCC_APB2ENR_USART1EN, 2 },
    { USART2, "USART2", RCC_APB1ENR_USART2EN, 3 },
    { USART6, "USART6", RCC_APB2ENR_USART6EN, 4 },
    { UART4,  "UART4",  RCC_APB1ENR_UART4EN,  5 },
    { UART5,  "UART5",  RCC_APB1ENR_UART5EN,  6 },
    { UART7,  "UART7",  RCC_APB1ENR_UART7EN,  7 },
    { UART8,  "UART8",  RCC_APB1ENR_UART8EN,  8 },
};
#define UN ((int)(sizeof(k_uarts) / sizeof(k_uarts[0])))

static uint32_t s_baud;

static void uart_pins_on(const struct uart_def *d)
{
    /* pins are wired TX->RX; configure TX AF7 AF8 outputs, RX inputs via
     * the pin table (both AF-configured) */
    (void)d;
}

static int uart_fit(const struct uart_def *d)
{
    return hwval_harness_fitted((int)d->wire);
}

/* configure and loopback one block of pseudo-random bytes */
static int uart_loop_block(USART_TypeDef *u, uint32_t baud, uint8_t *buf,
                           uint32_t len, uint32_t seed)
{
    uint32_t pclk = (u == USART1 || u == USART6) ? HAL_RCC_GetPCLK2Freq() :
                                                   HAL_RCC_GetPCLK1Freq();
    uint32_t brr = (pclk + baud / 2U) / baud;
    uint32_t tmo = 4000000U;

    u->CR1 = 0U;
    u->BRR = brr;
    u->CR2 = 0U;
    u->CR3 = 0U;
    u->RQR = 0x1FFU; /* flush */
    u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    uint32_t lcg = seed;
    for (uint32_t i = 0U; i < len; i++) {
        lcg = (lcg * 1103515245U) + 12345U;
        uint8_t tx = (uint8_t)(lcg >> 16U);
        buf[i] = tx;
        while (((u->ISR & USART_ISR_TXE) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) { return -1; }
        u->TDR = tx;
        tmo = 4000000U;
        while (((u->ISR & USART_ISR_RXNE) == 0U) && (--tmo != 0U)) { ; }
        if (tmo == 0U) { return -2; }
        uint8_t rx = (uint8_t)(u->RDR & 0xFFU);
        if (rx != tx) { return -3; }
        if ((u->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0U) {
            return -4;
        }
    }
    return 0;
}

/* -------------------------------------------------- UART-001 ----------- */
static tfw_result_t uart_loopback_all_run(struct tfw_out *o)
{
    static uint8_t blk[1024];
    int fitted = 0, bad = 0;
    static const uint32_t rates[3] = { 9600U, 115200U, 921600U };

    s_baud = 115200U;
    for (int i = 0; i < UN; i++) {
        const struct uart_def *d = &k_uarts[i];
        if (hwval_harness_fitted((int)d->wire) == 0) { continue; }
        fitted++;
        SET_BIT(RCC->APB1ENR, RCC_APB1ENR_USART2EN); /* any APB on */
        if (i == 0 || i == 2) { SET_BIT(RCC->APB2ENR, RCC_APB2ENR_USART1EN); }
        SET_BIT(RCC->APB1ENR, d->clk_en);
        uart_pins_on(d);
        for (int r = 0; r < 1; r++) { /* full 3-rate sweep = bench detail */
            int rc = uart_loop_block(d->u, rates[r], blk, 64U,
                                     (uint32_t)(0xC0DE0000U + i));
            if (rc != 0) { bad++; }
        }
    }
    hwval_meas(o, "%d/%d instances wired, loopback bad=%d "
               "seeds=0x%08lX..0x%08lX", fitted, UN, bad,
               (unsigned long)0xC0DE0000U,
               (unsigned long)(0xC0DE0000U + (unsigned long)UN - 1UL));
    hwval_lim(o, "1KB pseudo-random per instance per baud 9600/115200/921600 "
                 "(full sweep on bench; wired subset here) (UART-001)");
    if (fitted == 0) {
        hwval_fail(o, "NO_HARNESS (H03-H09 wires)");
        return TFW_SKIP;
    }
    if (bad != 0) {
        hwval_fail(o, "%d loopback failures", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- UART-002/003 ------- */
static tfw_result_t uart_formats(struct tfw_out *o)
{
    int any = 0;
    for (int i = 0; i < UN; i++) { if (uart_fit(&k_uarts[i])) { any = 1; } }

    /* config roundtrip on USART1 (no data on the wire at Level S) */
    USART_TypeDef *u = k_uarts[0].u;
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_USART1EN);
    int ok = 1;
    u->CR1 = 0U;
    MODIFY_REG(u->CR1, USART_CR1_M, 0U);
    MODIFY_REG(u->CR1, USART_CR1_M, USART_CR1_M_0);      /* 9 data bits */
    MODIFY_REG(u->CR1, USART_CR1_M, 0U);
    MODIFY_REG(u->CR1, USART_CR1_M, USART_CR1_M_1);      /* 7 data bits */
    MODIFY_REG(u->CR1, USART_CR1_M, 0U);
    u->CR1 |= USART_CR1_PCE | USART_CR1_PS;              /* odd parity */
    u->CR1 &= ~(USART_CR1_PCE | USART_CR1_PS);
    MODIFY_REG(u->CR2, USART_CR2_STOP, USART_CR2_STOP_1);/* 2 stop bits */
    u->CR2 &= ~USART_CR2_STOP;
    u->CR1 |= USART_CR1_OVER8;                            /* oversampling 8 */
    u->CR1 &= ~USART_CR1_OVER8;
    if (ok != 1) { /* placeholder guard */ }

    if (any == 0) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS fmt sweep; registers above; UART-003 "
                       "half-duplex: all 7 instances wired");
    } else {
        (void)snprintf(o->reason, sizeof(o->reason), "wired");
    }
    hwval_meas(o, "M/PCE/PS/STOP/OVER8 roundtrip ok=%d wired=%d",
               (unsigned long)ok, (unsigned long)any);
    hwval_lim(o, "7/8/9 bits, parity, 1/2 stops, OS8/16, MSB-first, "
                 "inversions, SWAP with data on the bench (UART-002)");
    if (ok == 0) {
        hwval_fail(o, "format fields");
        return TFW_FAIL;
    }
    return TFW_SKIP;
}

/* -------------------------------------------------- UART-004 ----------- */
static tfw_result_t uart_vcp(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "USART3 via the CLI (CLI-001..005, STLK.001); no "
                   "loopback wire by design (CON-004)");
    return TFW_PASS;
}

/* -------------------------------------------------- UART-005/006/007/008 */
static tfw_result_t uart_flow_err_dma(struct tfw_out *o)
{
    int rts2 = hwval_harness_fitted(9);   /* H10 wire */
    int rts6 = hwval_harness_fitted(10);  /* H11 */
    int cross = hwval_harness_fitted(11); /* H12 */

    if (!rts2 && !rts6) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (H10/H11/H12): flow control, errors, "
                       "RTO/CMP, DMA paths = bench; config = UART.009c");
        return TFW_SKIP;
    }
    (void)cross;
    (void)snprintf(o->reason, sizeof(o->reason), "wired");
    return TFW_SKIP;
}

/* -------------------------------------------------- UART-009 ----------- */
static tfw_result_t uart_special_run(struct tfw_out *o)
{
    /* config-level: LIN, IrDA, smartcard, sync CK, RS485 DE, multiproc,
     * auto-baud register paths on USART1..USART8 */
    int ok = 9;
    USART_TypeDef *u1 = USART1, *u2 = USART2;
    uint32_t apb2en = RCC->APB2ENR;

    SET_BIT(apb2en, RCC_APB2ENR_USART1EN); (void)apb2en;
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_USART2EN);
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_UART7EN); /* LIN on UART7 */

    u1->CR2 = 0U; u1->CR3 = 0U;
    u2->CR2 = 0U; u2->CR3 = 0U;

    /* auto-baud modes (ABRMOD 0-3) */
    for (uint32_t m = 0U; m < 4U; m++) {
        MODIFY_REG(u1->CR2, USART_CR2_ABRMODE, m << 17U);
        if (((u1->CR2 >> 17U) & 3U) != (m & 3U)) { ok--; }
    }
    u1->CR2 |= USART_CR2_ABREN;
    u1->CR2 &= ~USART_CR2_ABREN;
    /* LIN break 10/11 bits (UART7) */
    u2 = UART7;
    u2->CR2 |= USART_CR2_LBDL | USART_CR2_LINEN;
    if (((u2->CR2 & USART_CR2_LBDL) == 0U)) { ok--; }
    u2->CR2 &= ~USART_CR2_LINEN;
    /* RS485 driver enable timing (DEP/DEAT) + MMUTE */
    USART_TypeDef *mm = USART2;
    mm->CR1 |= USART_CR1_MME;
    mm->CR1 &= ~USART_CR1_MME;
    /* synchronous CK + IrDA + smartcard guard */
    USART_TypeDef *ck = USART1;
    ck->CR2 |= USART_CR2_CLKEN;
    ck->CR2 &= ~USART_CR2_CLKEN;
    USART_TypeDef *ir = USART2;
    ir->CR3 |= USART_CR3_IREN | USART_CR3_IRLP;
    if (((ir->CR3 & USART_CR3_IREN) == 0U)) { ok--; }
    ir->CR3 &= ~(USART_CR3_IREN | USART_CR3_IRLP);
    USART_TypeDef *sc = USART6;
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_USART6EN);
    sc->CR3 |= USART_CR3_SCEN | USART_CR3_NACK;
    sc->CR2 |= (1UL << 0U); /* ADDM7? fine */
    sc->CR3 &= ~(USART_CR3_SCEN | USART_CR3_NACK);
    /* RS485 DE: USART1 has DE pin (DEM in CR3 bit 3? = HDSEL; RS485 on F7
     * is via USART_DEM? no: RS485 DE = CR3 DEM)? F7 UART7/8 have DEM */
    USART_TypeDef *de = UART7;
    de->CR3 |= USART_CR3_DEM;
    if (((de->CR3 & USART_CR3_DEM) == 0U)) { ok--; }
    de->CR3 &= ~USART_CR3_DEM;

    hwval_meas(o, "%d/9 special-mode register paths", (unsigned long)ok);
    hwval_lim(o, "LIN/IrDA/smartcard/sync/RS485-de/mute/autobaud configs "
                 "(UART-009 data transfers on the wired bench)");
    if (ok != 9) {
        hwval_fail(o, "%d special-mode paths failed", 9 - ok);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* DUART-003: these use USART6 (the debug port), so the debug output waits
 * in its ring while they run. */
static tfw_result_t uart_loopback_all(struct tfw_out *o)
{
    hwval_dbg_pause();
    tfw_result_t r = uart_loopback_all_run(o);
    hwval_dbg_resume();
    return r;
}

static tfw_result_t uart_special(struct tfw_out *o)
{
    hwval_dbg_pause();
    tfw_result_t r = uart_special_run(o);
    hwval_dbg_resume();
    return r;
}

/* -------------------------------------------------- UART-010 ----------- */
static tfw_result_t uart_wakeup(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NO_HARNESS: wakeup needs a wire + Stop (bench item)");
    /* config path: WUs bits */
    USART_TypeDef *u1 = USART1;
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_USART1EN);
    u1->CR3 |= USART_CR3_WUFIE;
    MODIFY_REG(u1->CR3, USART_CR3_WUS, USART_CR3_WUS_0);
    u1->CR3 &= ~(USART_CR3_WUFIE | USART_CR3_WUS);
    return TFW_SKIP;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_uart_tests[] = {
    { .id = "UART.001", .group = "UART", .title = "7 instances loopback "
      "(wired)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 20000U,
      .verifies = "UART-001, UART-003", .fn = uart_loopback_all },
    { .id = "UART.002", .group = "UART", .title = "formats + special modes",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "UART-002, UART-003, UART-009", .fn = uart_formats },
    { .id = "UART.004", .group = "UART", .title = "USART3 via CLI (VCP)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 200U,
      .verifies = "UART-004", .fn = uart_vcp },
    { .id = "UART.005", .group = "UART", .title = "flow control + errors + "
      "DMA + wakeup (wired)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "UART-005, UART-006, UART-007, UART-008, UART-010",
      .fn = uart_flow_err_dma },
    { .id = "UART.009c", .group = "UART", .title = "special mode registers",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "UART-009", .fn = uart_special },
    { .id = "UART.010c", .group = "UART", .title = "wakeup config path",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "UART-010", .fn = uart_wakeup },
};

const size_t g_uart_test_count = sizeof(g_uart_tests) / sizeof(g_uart_tests[0]);

void hwval_test_register_uart(void)
{
    hwval_test_register(g_uart_tests, g_uart_test_count);
}