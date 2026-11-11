/*
 * debug_uart.h - debug text on USART6 (PG14, Arduino D1), TX only
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-06
 * SRS:     DUART-001..DUART-004
 *
 * USART3 is the HWVAL console and HWLINK port, so debug lines go to USART6,
 * 115200 8N1, in the format BOOT and the demo applications use:
 *
 *     [   5012] HWVAL I health: passed, CONFIRMED written
 *
 * DUART-002: hwval_dbg() copies the line into a 1 KB ring and returns; a
 * priority-1 task drains it by polling.  No interrupt and no DMA, because
 * the interrupt and DMA tests own those (test_int.c defines
 * USART6_IRQHandler, test_int_dma.c sweeps DMA2).
 * DUART-003: tests that use USART6 or reset it call hwval_dbg_pause() and
 * hwval_dbg_resume(); output waits in the ring meanwhile.
 * DUART-004: PG14/PG9 are console-owned in the pin table, so the GPIO tests
 * never drive them.
 */

#ifndef HWVAL_DEBUG_UART_H
#define HWVAL_DEBUG_UART_H

#include <stdint.h>

/* Configure PG14 as USART6_TX and start USART6 (call after the clocks). */
void hwval_dbg_init(void);

/* Create the drain task (call before the scheduler starts). */
int hwval_dbg_task_create(void);

/* One line: level 'E', 'W', 'I' or 'D', printf-style, no newline. */
void hwval_dbg(char level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Stop touching USART6 until the matching resume; nests. */
void hwval_dbg_pause(void);

/* Re-initialise USART6 and PG14 (a test may have reset them) and go on. */
void hwval_dbg_resume(void);

/* Write out what is queued by polling, at most timeout_ms; for the paths
 * that reset the MCU on purpose. */
void hwval_dbg_flush(uint32_t timeout_ms);

/* Task numbers for hwval_task_report(). */
#define HWVAL_TASK_CLI    1U
#define HWVAL_TASK_RUNNER 2U
#define HWVAL_TASK_HB     3U
#define HWVAL_TASK_WDG    4U

/* Once-a-second "task <n> <name>: <purpose>" line for one of the four
 * application tasks.  Each task calls it from its loop; it returns at once
 * unless a second has passed since that task last printed. */
void hwval_task_report(unsigned number);

#endif /* HWVAL_DEBUG_UART_H */
