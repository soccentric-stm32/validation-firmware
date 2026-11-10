/*
 * debug_uart.c - debug text on USART6 (PG14, Arduino D1), TX only
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-06
 * SRS:     DUART-001..DUART-004
 *
 * Register-level and polled (see debug_uart.h for why).  Writers take
 * PRIMASK only for the copy of one line into the ring.  The drain task runs
 * at priority 1 with the heartbeat and watchdog tasks, so the runner (2)
 * and CLI (3) always preempt it; it sleeps 10 ms whenever the ring is empty.
 */

#include <stdarg.h>
#include <stdio.h>
#include "stm32f7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "debug_uart.h"

#define DBG_BAUD          115200U
#define DBG_RING_SIZE     1024U           /* power of two */
#define DBG_LINE_MAX      128U
#define DBG_TAG           "HWVAL"
#define DBG_TX_PIN        14U             /* PG14 = USART6_TX, AF8 */
#define DBG_TX_AF         8U
#define DBG_WAIT_LOOPS    200000U         /* about 1 ms at 216 MHz */
#define DBG_TASK_PRIO     1U
#define DBG_TASK_STACK    256U
#define DBG_IDLE_MS       10U

static char s_ring[DBG_RING_SIZE];
static volatile uint32_t s_head;          /* written by hwval_dbg()     */
static volatile uint32_t s_tail;          /* written by the drain side  */
static volatile uint32_t s_paused;
static volatile uint32_t s_dropped;
static volatile uint8_t s_ready;

static void dbg_wait(uint32_t flag)
{
    for (uint32_t i = 0U; i < DBG_WAIT_LOOPS; i++) {
        if ((USART6->ISR & flag) != 0U) {
            return;
        }
    }
}

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

void hwval_dbg_init(void)
{
    dbg_hw_init();
    s_ready = 1U;
}

/* Send the queued characters.  The pause check and the TDR write are one
 * critical section, so a test that pauses the port (from a higher-priority
 * task) can never be handed a stray byte. */
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

static void task_dbg(void *arg)
{
    (void)arg;
    for (;;) {
        dbg_drain();
        vTaskDelay(pdMS_TO_TICKS(DBG_IDLE_MS));
    }
}

int hwval_dbg_task_create(void)
{
    return (xTaskCreate(task_dbg, "dbg", DBG_TASK_STACK, NULL,
                        DBG_TASK_PRIO, NULL) == pdPASS) ? 0 : -1;
}

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

void hwval_dbg_pause(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_paused++;
    __set_PRIMASK(primask);
    if (s_ready != 0U) {
        dbg_wait(USART_ISR_TC);   /* let the character in flight finish */
    }
}

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
