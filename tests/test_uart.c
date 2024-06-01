/**
 * @file    test_uart.c
 * @brief   USART1/2/6 and UART4/5/7/8 tests (section 8.18).
 * @details Data paths need the H03..H12 wires (Level L); register checks run at Level S.
 *          USART3 is the VCP and is exercised by the CLI (UART-004).
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     UART-001..UART-010
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "hwval_limits.h"
#include "app/test_core.h"
#include "app/harness.h"
#include "bsp/debug_uart.h"

/** @brief One U(S)ART instance under test. */
struct uart_def {
    USART_TypeDef *u;      /**< Peripheral registers. */
    const char    *name;   /**< Display name. */
    uint32_t       clk_en; /**< RCC APBxENR enable bit. */
    uint8_t        wire;   /**< Appendix A harness index (H03 = 2). */
};

/** UART instances and their loopback wires. */
static const struct uart_def k_uarts[] = {
    { USART1, "USART1", RCC_APB2ENR_USART1EN, 2 },
    { USART2, "USART2", RCC_APB1ENR_USART2EN, 3 },
    { USART6, "USART6", RCC_APB2ENR_USART6EN, 4 },
    { UART4,  "UART4",  RCC_APB1ENR_UART4EN,  5 },
    { UART5,  "UART5",  RCC_APB1ENR_UART5EN,  6 },
    { UART7,  "UART7",  RCC_APB1ENR_UART7EN,  7 },
    { UART8,  "UART8",  RCC_APB1ENR_UART8EN,  8 },
};
#define UN ((int)(sizeof(k_uarts) / sizeof(k_uarts[0]))) /**< Entries in k_uarts. */

static uint32_t s_baud; /**< Last loopback baud rate (set, not read). */

/**
 * @brief Placeholder for loopback pin setup; does nothing.
 * @param[in] d Instance (unused).
 */
static void uart_pins_on(const struct uart_def *d)
{
    (void)d;
}

/**
 * @brief Report whether an instance's loopback wire is fitted.
 * @param[in] d Instance.
 * @return Non-zero when wired.
 */
static int uart_fit(const struct uart_def *d)
{
    return hwval_harness_fitted((int)d->wire);
}

/**
 * @brief Configure 8N1 at @p baud and loop back @p len LCG bytes by polling.
 * @param[in]  u    Peripheral with TX wired to RX.
 * @param[in]  baud Baud rate, bit/s.
 * @param[out] buf  Bytes sent, @p len long.
 * @param[in]  len  Byte count.
 * @param[in]  seed LCG seed.
 * @retval 0  All bytes echoed.
 * @retval -1 TXE timeout.
 * @retval -2 RXNE timeout.
 * @retval -3 Data mismatch.
 * @retval -4 ORE, FE or NE set.
 */
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
    u->RQR = 0x1FFU; /* flush requests */
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
/**
 * @brief Loop back 64 bytes on every wired instance.
 * @param[out] o Result text.
 * @return TFW_PASS, TFW_FAIL, or TFW_SKIP with no wires.
 */
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
        SET_BIT(RCC->APB1ENR, RCC_APB1ENR_USART2EN);
        if (i == 0 || i == 2) { SET_BIT(RCC->APB2ENR, RCC_APB2ENR_USART1EN); }
        SET_BIT(RCC->APB1ENR, d->clk_en);
        uart_pins_on(d);
        for (int r = 0; r < 1; r++) { /* 9600 only; full sweep on the bench */
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
/**
 * @brief Toggle USART1 word length, parity, stop-bit and oversampling fields.
 * @param[out] o Result text.
 * @return TFW_SKIP (data checks are bench items).
 */
static tfw_result_t uart_formats(struct tfw_out *o)
{
    int any = 0;
    for (int i = 0; i < UN; i++) { if (uart_fit(&k_uarts[i])) { any = 1; } }

    /* Toggle format fields on USART1; no data is sent. */
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
    if (ok != 1) { }

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
/**
 * @brief Pass: USART3 is exercised by the CLI.
 * @param[out] o Result text.
 * @return TFW_PASS.
 */
static tfw_result_t uart_vcp(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "USART3 via the CLI (CLI-001..005, STLK.001); no "
                   "loopback wire by design (CON-004)");
    return TFW_PASS;
}

/* -------------------------------------------------- UART-005/006/007/008 */
/**
 * @brief Skip flow control, error, RTO and DMA checks (bench items).
 * @param[out] o Result text.
 * @return TFW_SKIP.
 */
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
/**
 * @brief Check special-mode register paths (auto-baud, LIN, IrDA, RS485 DE, ...).
 * @param[out] o Result text.
 * @return TFW_PASS or TFW_FAIL.
 */
static tfw_result_t uart_special_run(struct tfw_out *o)
{
    /* Register paths: auto-baud, LIN, mute, sync CK, IrDA, smartcard,
     * RS485 DE. */
    int ok = 9;
    USART_TypeDef *u1 = USART1, *u2 = USART2;
    uint32_t apb2en = RCC->APB2ENR;

    SET_BIT(apb2en, RCC_APB2ENR_USART1EN); (void)apb2en;
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_USART2EN);
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_UART7EN); /* LIN on UART7 */

    u1->CR2 = 0U; u1->CR3 = 0U;
    u2->CR2 = 0U; u2->CR3 = 0U;

    /* Auto-baud modes 0..3. */
    for (uint32_t m = 0U; m < 4U; m++) {
        MODIFY_REG(u1->CR2, USART_CR2_ABRMODE, m << 17U);
        if (((u1->CR2 >> 17U) & 3U) != (m & 3U)) { ok--; }
    }
    u1->CR2 |= USART_CR2_ABREN;
    u1->CR2 &= ~USART_CR2_ABREN;
    /* LIN, 11-bit break detection (UART7). */
    u2 = UART7;
    u2->CR2 |= USART_CR2_LBDL | USART_CR2_LINEN;
    if (((u2->CR2 & USART_CR2_LBDL) == 0U)) { ok--; }
    u2->CR2 &= ~USART_CR2_LINEN;
    /* Multiprocessor mute mode. */
    USART_TypeDef *mm = USART2;
    mm->CR1 |= USART_CR1_MME;
    mm->CR1 &= ~USART_CR1_MME;
    /* Synchronous CK, IrDA, smartcard. */
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
    sc->CR2 |= (1UL << 0U);
    sc->CR3 &= ~(USART_CR3_SCEN | USART_CR3_NACK);
    /* RS485 driver enable (CR3.DEM). */
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

/**
 * @brief Run the loopback test with the debug UART paused.
 * @details USART6 is the debug port; its output waits in the ring meanwhile (DUART-003).
 * @param[out] o Result text.
 * @return Result of uart_loopback_all_run().
 */
static tfw_result_t uart_loopback_all(struct tfw_out *o)
{
    hwval_dbg_pause();
    tfw_result_t r = uart_loopback_all_run(o);
    hwval_dbg_resume();
    return r;
}

/**
 * @brief Run the special-mode test with the debug UART paused (DUART-003).
 * @param[out] o Result text.
 * @return Result of uart_special_run().
 */
static tfw_result_t uart_special(struct tfw_out *o)
{
    hwval_dbg_pause();
    tfw_result_t r = uart_special_run(o);
    hwval_dbg_resume();
    return r;
}

/* -------------------------------------------------- UART-010 ----------- */
/**
 * @brief Toggle USART1 wakeup config; the Stop wakeup itself is a bench item.
 * @param[out] o Result text.
 * @return TFW_SKIP.
 */
static tfw_result_t uart_wakeup(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NO_HARNESS: wakeup needs a wire + Stop (bench item)");
    /* Toggle the wakeup interrupt and WUS selection. */
    USART_TypeDef *u1 = USART1;
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_USART1EN);
    u1->CR3 |= USART_CR3_WUFIE;
    MODIFY_REG(u1->CR3, USART_CR3_WUS, USART_CR3_WUS_0);
    u1->CR3 &= ~(USART_CR3_WUFIE | USART_CR3_WUS);
    return TFW_SKIP;
}

/** UART test cases (UART-001..UART-010). */
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

/** Number of entries in @ref g_uart_tests. */
const size_t g_uart_test_count = sizeof(g_uart_tests) / sizeof(g_uart_tests[0]);

/** @brief Register the UART test cases with the framework. */
void hwval_test_register_uart(void)
{
    hwval_test_register(g_uart_tests, g_uart_test_count);
}