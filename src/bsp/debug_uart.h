/**
 * @file    debug_uart.h
 * @brief   Debug text on USART6 TX (PG14, Arduino D1), 115200 8N1.
 * @details USART3 is the console/HWLINK port, so debug lines go to USART6 in
 *          the BOOT/demo format `[   5012] HWVAL I health: passed`.
 *          hwval_dbg() copies a line into a 1 KB ring; a priority-1 task
 *          drains it by polling. No interrupt or DMA: the interrupt and DMA
 *          tests own those (DUART-002). PG14/PG9 are console-owned in the pin
 *          table (DUART-004).
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-06
 * @srs     DUART-001..DUART-004
 */

#ifndef HWVAL_DEBUG_UART_H
#define HWVAL_DEBUG_UART_H

#include <stdint.h>

/**
 * @defgroup bsp_debug_uart Debug UART
 * @brief Polled, ring-buffered debug output on USART6.
 * @{
 */

/**
 * @brief Configure PG14 as USART6_TX and start USART6.
 * @pre Clock tree configured.
 */
void hwval_dbg_init(void);

/**
 * @brief Create the drain task.
 * @pre Scheduler not started.
 * @retval 0  Created.
 * @retval -1 xTaskCreate failed.
 */
int hwval_dbg_task_create(void);

/**
 * @brief Queue one timestamped debug line; never blocks.
 * @details Drops the whole line when the ring is full and truncates it to
 *          128 bytes. No-op before @ref hwval_dbg_init.
 * @param[in] level 'E', 'W', 'I' or 'D'.
 * @param[in] fmt   printf format, without newline.
 */
void hwval_dbg(char level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * @brief Stop touching USART6 until the matching resume; nests (DUART-003).
 * @details Waits up to ~1 ms for the character in flight. Output stays queued.
 */
void hwval_dbg_pause(void);

/**
 * @brief Undo one @ref hwval_dbg_pause; the last one re-initialises USART6
 *        and PG14, since a test may have reset them (DUART-003).
 */
void hwval_dbg_resume(void);

/**
 * @brief Drain the ring by polling before an intentional reset.
 * @param[in] timeout_ms Drain-loop iteration budget (roughly ms).
 * @note No-op while paused.
 */
void hwval_dbg_flush(uint32_t timeout_ms);

#define HWVAL_TASK_CLI    1U /**< CLI task number for @ref hwval_task_report. */
#define HWVAL_TASK_RUNNER 2U /**< Runner task number. */
#define HWVAL_TASK_HB     3U /**< Heartbeat task number. */
#define HWVAL_TASK_WDG    4U /**< Watchdog task number. */

/**
 * @brief Print "task <n> <name>: <purpose>" at most once a second per task.
 * @details Each application task calls this from its loop.
 * @param[in] number HWVAL_TASK_* (1..4); others are ignored.
 */
void hwval_task_report(unsigned number);

/** @} */

#endif /* HWVAL_DEBUG_UART_H */
