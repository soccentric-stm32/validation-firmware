/**
 * @file    debug_uart.c
 * @brief   Debug text on USART6 TX (PG14, Arduino D1), register-level, polled.
 * @details Writers mask interrupts (PRIMASK) only to copy one line into the
 *          ring. The drain task runs at priority 1 with the heartbeat and
 *          watchdog tasks, so the runner and CLI always preempt it; it sleeps
 *          10 ms when the ring is empty.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-06
 * @srs     DUART-001..DUART-004
 */

#include <stdarg.h>
#include <stdio.h>
#include "stm32f7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp/debug_uart.h"

#define DBG_BAUD          115200U /**< USART6 baud rate. */
#define DBG_RING_SIZE     1024U   /**< Ring size, bytes; power of two. */
#define DBG_LINE_MAX      128U    /**< Max line length incl. CR LF. */
#define DBG_TAG           "HWVAL" /**< Source tag in every line. */
#define DBG_TX_PIN        14U     /**< PG14 = USART6_TX. */
#define DBG_TX_AF         8U      /**< AF8 = USART6. */
#define DBG_WAIT_LOOPS    200000U /**< Flag-poll bound, about 1 ms at 216 MHz. */
#define DBG_TASK_PRIO     1U      /**< Drain task priority. */
#define DBG_TASK_STACK    256U    /**< Drain task stack, words. */
#define DBG_IDLE_MS       10U     /**< Drain task sleep, ms. */

static char s_ring[DBG_RING_SIZE];        /**< Output ring. */
static volatile uint32_t s_head;          /**< Write index; hwval_dbg() only. */
static volatile uint32_t s_tail;          /**< Read index; drain side only. */
static volatile uint32_t s_paused;        /**< Pause nesting depth. */
static volatile uint32_t s_dropped;       /**< Lines dropped on a full ring. */
static volatile uint8_t s_ready;          /**< Non-zero after hwval_dbg_init(). */

/**
 * @brief Busy-wait for a USART6 ISR flag, bounded to about 1 ms.
 * @param[in] flag USART_ISR_* mask.
 */
static void dbg_wait(uint32_t flag)
{
    for (uint32_t i = 0U; i < DBG_WAIT_LOOPS; i++) {
        if ((USART6->ISR & flag) != 0U) {
            return;
        }
    }
}

/** @brief Clock GPIOG/USART6, mux PG14 to AF8 and enable USART6 TX. */
static void dbg_hw_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
    (void)RCC->APB2ENR;

    const uint32_t shift = DBG_TX_PIN * 2U;
    GPIOG->MODER = (GPIOG->MODER & ~(3U << shift)) | (2U << shift);
    GPIOG->OSPEEDR = (GPIOG->OSPEEDR & ~(3U << shift)) | (1U << shift);
    const uint32_t af_shift = (DBG_TX_PIN - 8U) * 4U;
    GPIOG->AFR[1] = (GPIOG->AFR[1] & ~(0xFU << af_shift)) |
                    (DBG_TX_AF << af_shift);

    USART6->CR1 = 0U;
    USART6->CR2 = 0U;
    USART6->CR3 = 0U;
    USART6->BRR = (HAL_RCC_GetPCLK2Freq() + (DBG_BAUD / 2U)) / DBG_BAUD;
    USART6->CR1 = USART_CR1_TE | USART_CR1_UE;
}

/** @brief Configure USART6 and enable hwval_dbg(). */
void hwval_dbg_init(void)
{
    dbg_hw_init();
    s_ready = 1U;
}

/**
 * @brief Send queued characters until the ring is empty, paused or TX stalls.
 * @details The pause check and the TDR write share one critical section, so a
 *          higher-priority test that pauses the port never gets a stray byte.
 */
static void dbg_drain(void)
{
    while (s_tail != s_head) {
        dbg_wait(USART_ISR_TXE);
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        int sent = 0;
        if ((s_paused == 0U) && ((USART6->ISR & USART_ISR_TXE) != 0U)) {
            USART6->TDR = (uint8_t)s_ring[s_tail & (DBG_RING_SIZE - 1U)];
            s_tail++;
            sent = 1;
        }
        __set_PRIMASK(primask);
        if (sent == 0) {
            return;       /* paused, or USART6 not ready: try later */
        }
    }
}

/**
 * @brief Drain task: drain, then sleep DBG_IDLE_MS.
 * @param[in] arg Unused.
 */
static void task_dbg(void *arg)
{
    (void)arg;
    for (;;) {
        dbg_drain();
        vTaskDelay(pdMS_TO_TICKS(DBG_IDLE_MS));
    }
}

/** Name and purpose per application task, indexed by task number - 1. */
static const struct {
    const char *name;    /**< Task name. */
    const char *purpose; /**< One-line purpose. */
} s_task_info[4] = {
    {"cli", "text CLI and HWLINK on USART3, runs commands from the host"},
    {"runner", "runs the boot test suite, then jobs from the CLI or link"},
    {"hb", "heartbeat: LD1 1 Hz idle, 5 Hz while testing, LD3 on failure"},
    {"wdg", "kicks the IWDG while the runner shows progress, BOOT health"},
};
static uint32_t s_task_last[4];  /**< Tick of each task's last report, ms. */
static uint8_t s_task_primed[4]; /**< Non-zero once a task has reported. */

/**
 * @brief Print a task's status line at most once a second.
 * @param[in] number Task number, 1..4.
 */
void hwval_task_report(unsigned number)
{
    if ((number < 1U) || (number > 4U)) {
        return;
    }
    const unsigned i = number - 1U;
    const uint32_t now = HAL_GetTick();
    if ((s_task_primed[i] != 0U) && ((now - s_task_last[i]) < 1000U)) {
        return;
    }
    s_task_primed[i] = 1U;
    s_task_last[i] = now;
    hwval_dbg('I', "task %u %s: %s", number, s_task_info[i].name,
              s_task_info[i].purpose);
}

/**
 * @brief Create the drain task.
 * @return 0 on success, -1 if xTaskCreate fails.
 */
int hwval_dbg_task_create(void)
{
    return (xTaskCreate(task_dbg, "dbg", DBG_TASK_STACK, NULL,
                        DBG_TASK_PRIO, NULL) == pdPASS) ? 0 : -1;
}

/**
 * @brief Format a line and copy it into the ring; drop it when full.
 * @param[in] level Level character.
 * @param[in] fmt   printf format.
 */
void hwval_dbg(char level, const char *fmt, ...)
{
    if (s_ready == 0U) {
        return;
    }
    char line[DBG_LINE_MAX];
    int n = snprintf(line, sizeof(line), "[%7lu] " DBG_TAG " %c ",
                     (unsigned long)HAL_GetTick(), level);
    if (n < 0) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    int m = vsnprintf(&line[n], sizeof(line) - (size_t)n - 2U, fmt, args);
    va_end(args);
    if (m > 0) {
        size_t room = sizeof(line) - (size_t)n - 3U;
        n += ((size_t)m <= room) ? m : (int)room;
    }
    line[n++] = '\r';
    line[n++] = '\n';

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if ((uint32_t)n > DBG_RING_SIZE - (s_head - s_tail)) {
        s_dropped++;
    } else {
        for (int i = 0; i < n; i++) {
            s_ring[(s_head + (uint32_t)i) & (DBG_RING_SIZE - 1U)] = line[i];
        }
        s_head += (uint32_t)n;
    }
    __set_PRIMASK(primask);
}

/** @brief Increment the pause depth and let the character in flight finish. */
void hwval_dbg_pause(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_paused++;
    __set_PRIMASK(primask);
    if (s_ready != 0U) {
        dbg_wait(USART_ISR_TC);
    }
}

/** @brief Decrement the pause depth; re-init USART6 when it reaches 0. */
void hwval_dbg_resume(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (s_paused > 0U) {
        s_paused--;
    }
    uint32_t paused = s_paused;
    __set_PRIMASK(primask);
    if ((paused == 0U) && (s_ready != 0U)) {
        dbg_hw_init();
    }
}

/**
 * @brief Drain by polling, then wait for TC.
 * @param[in] timeout_ms Maximum drain iterations.
 */
void hwval_dbg_flush(uint32_t timeout_ms)
{
    if ((s_ready == 0U) || (s_paused != 0U)) {
        return;
    }
    for (uint32_t ms = 0U; (ms < timeout_ms) && (s_tail != s_head); ms++) {
        dbg_drain();
    }
    dbg_wait(USART_ISR_TC);
}
