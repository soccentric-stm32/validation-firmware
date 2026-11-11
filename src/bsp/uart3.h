/*
 * uart3.h - minimal polled USART3 console
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     CLI-001 (USART3 PD8/PD9, 115200 8N1 on the ST-LINK VCP);
 *          the interrupt/DMA ring-buffer CLI driver lands in section 6.
 */

#ifndef HWVAL_UART3_H
#define HWVAL_UART3_H

#include <stddef.h>
#include <stdint.h>

/* Init after the clock tree is up (baud is computed from PCLK1). */
void hwval_uart3_init(void);

/* true once init has run; fault handlers check before printing. */
int hwval_uart3_ready(void);

/* Send n bytes, or the whole NUL-terminated string when n == 0.
 * When a sink is installed (link mode), text is delivered there instead of
 * the UART so that LINK-002 can turn it into HWLINK Log events. */
void hwval_uart3_write(const void *buf, size_t n);

/* Bypass the sink and enqueue bytes on USART3 (used by HWLINK framing). */
void hwval_uart3_write_direct(const void *buf, size_t n);

/* Sink used by link mode; NULL restores direct text output. */
typedef void (*hwval_uart3_sink_t)(const char *data, size_t len);
void hwval_uart3_set_sink(hwval_uart3_sink_t sink);

/* Change the VCP baud rate (CLI-004, LINK-001).  0 on success. */
int hwval_uart3_set_baud(uint32_t baud);

/* Wait for the TX ring to drain, up to timeout_ms. */
void hwval_uart3_drain(uint32_t timeout_ms);

/* One line, \n expanded to \r\n (CLI-005). */
void hwval_uart3_puts(const char *s);

/* Current baud (CLI-004 bookkeeping for the CLI). */
int hwval_uart3_baud(void);

/* RX: pop one byte from the ring buffer, -1 if empty (CLI-002). */
int hwval_uart3_getc(void);
/* RX notify hook (section 6 replaces polling with a semaphore). */
void hwval_uart3_rx_wakeup(void);

#endif /* HWVAL_UART3_H */