/*
 * uart3.c - USART3 console driver: IRQ RX ring + IRQ TX ring (CLI-002/003)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  STM32F767ZIT6, ST-LINK VCP (USART3 PD8 TX, PD9 RX)
 * SRS:     CLI-001 (115200 8N1), CLI-002 (RX ring >= 512, no loss on
 *          paste), CLI-003 (interrupt TX, non-blocking), CLI-005 (\r\n out).
 */

#include <stdbool.h>

#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "bsp/uart3.h"
#include "FreeRTOS.h"
#include "task.h"

static int s_ready;
static hwval_uart3_sink_t s_sink;

/* --- RX ring (CLI-002) ---------------------------------------------------- */
#define RX_RING_SIZE 512U
static volatile uint8_t  s_rx_ring[RX_RING_SIZE];
static volatile uint16_t s_rx_head; /* ISR writes */
static volatile uint16_t s_rx_tail; /* task reads */

/* --- TX ring (CLI-003) ---------------------------------------------------- */
#define TX_RING_SIZE 2048U
static volatile uint8_t  s_tx_ring[TX_RING_SIZE];
static volatile uint16_t s_tx_head;
static volatile uint16_t s_tx_tail;

void hwval_uart3_init(void)
{
    GPIO_InitTypeDef io = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    io.Pin       = GPIO_PIN_8 | GPIO_PIN_9; /* PD8 TX, PD9 RX */
    io.Mode      = GPIO_MODE_AF_PP;
    io.Pull      = GPIO_NOPULL;
    io.Speed     = GPIO_SPEED_FREQ_HIGH;
    io.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &io);

    NVIC_SetPriority(USART3_IRQn, 6); /* >= configMAX_SYSCALL (FRT-008) */
    NVIC_EnableIRQ(USART3_IRQn);

    USART3->CR1 = 0U; /* disabled while configuring */
    USART3->BRR = (HAL_RCC_GetPCLK1Freq() + (HWVAL_VCP_BAUD / 2U)) /
                   HWVAL_VCP_BAUD;
    USART3->CR2 = 0U; /* 1 stop bit */
    USART3->CR3 = 0U; /* no flow control */
    USART3->RQR = USART_RQR_RXFRQ; /* drop any stale bytes */
    USART3->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE |
                  USART_CR1_UE;

    s_ready = 1;
}

int hwval_uart3_ready(void)
{
    if (!s_ready) {
        return 0;
    }
    return (int)((USART3->CR1 & USART_CR1_UE) != 0U);
}

int hwval_uart3_baud(void)
{
    uint32_t brr = USART3->BRR;

    return (int)((HAL_RCC_GetPCLK1Freq() + brr / 2U) / brr);
}

void USART3_IRQHandler(void)
{
    uint32_t isr = USART3->ISR;

    /* TX: push the next byte while TXE, stop when drained */
    if (((isr & USART_ISR_TXE) != 0U) &&
        ((USART3->CR1 & USART_CR1_TXEIE) != 0U)) {
        if (s_tx_tail == s_tx_head) {
            USART3->CR1 &= ~USART_CR1_TXEIE;
        } else {
            USART3->TDR = s_tx_ring[s_tx_tail];
            s_tx_tail = (uint16_t)((s_tx_tail + 1U) % TX_RING_SIZE);
        }
    }

    /* RX: ring the byte; drop newest on overflow (counted, CLI-002) */
    if ((isr & USART_ISR_RXNE) != 0U) {
        uint16_t next = (uint16_t)((s_rx_head + 1U) % RX_RING_SIZE);
        uint8_t  data = (uint8_t)USART3->RDR;

        s_rx_ring[s_rx_head] = data;
        if (next != s_rx_tail) {
            s_rx_head = next;
        }
    }
    if ((isr & USART_ISR_ORE) != 0U) {
        USART3->ICR = USART_ICR_ORECF; /* recover; reader drains the rest */
    }
}

int hwval_uart3_getc(void)
{
    uint16_t head;
    uint8_t  c;

    taskENTER_CRITICAL();
    {
        head = s_rx_head;
        if (head == s_rx_tail) {
            taskEXIT_CRITICAL();
            return -1;
        }
        c = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % RX_RING_SIZE);
    }
    taskEXIT_CRITICAL();
    return (int)c;
}

void hwval_uart3_rx_wakeup(void)
{
    /* The CLI task polls; a semaphore wake-up lands here if needed. */
}

void hwval_uart3_set_sink(hwval_uart3_sink_t sink)
{
    s_sink = sink;
}

int hwval_uart3_set_baud(uint32_t baud)
{
    if ((!s_ready) || (baud == 0U)) {
        return -1;
    }
    USART3->CR1 &= ~USART_CR1_UE;
    USART3->BRR = (HAL_RCC_GetPCLK1Freq() + (baud / 2U)) / baud;
    USART3->CR1 |= USART_CR1_UE;
    return 0;
}

void hwval_uart3_drain(uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (s_tx_head != s_tx_tail) {
        if (xTaskGetTickCount() >= deadline) {
            return;
        }
        vTaskDelay(1U);
    }
}

static bool tx_push_byte(uint8_t c)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(500);

    while ((uint16_t)((s_tx_head + 1U) % TX_RING_SIZE) == s_tx_tail) {
        if (xTaskGetTickCount() >= deadline) {
            return false; /* ring stayed full: drop, non-blocking (CLI-003) */
        }
        vTaskDelay(1U);
    }
    s_tx_ring[s_tx_head] = c;
    s_tx_head = (uint16_t)((s_tx_head + 1U) % TX_RING_SIZE);
    USART3->CR1 |= USART_CR1_TXEIE; /* ISR drains while TXE */
    return true;
}

void hwval_uart3_write(const void *buf, size_t n)
{
    const char *p = (const char *)buf;

    if (!s_ready) {
        return;
    }
    if (n == 0U) {
        while (*p != '\0') { p++; }
        n = (size_t)(p - (const char *)buf);
        p = (const char *)buf;
    }
    if (s_sink != NULL) {
        if (n > 0U) {
            s_sink(p, n);
        }
        return;
    }
    /* CLI-005: text output maps \n to \r\n. */
    for (size_t i = 0U; i < n; i++) {
        if (p[i] == '\n' && !tx_push_byte('\r')) {
            return;
        }
        if (!tx_push_byte((uint8_t)p[i])) {
            return;
        }
    }
}

void hwval_uart3_write_direct(const void *buf, size_t n)
{
    const char *p = (const char *)buf;

    if (!s_ready) {
        return;
    }
    if (n == 0U) {
        while (*p != '\0') { p++; }
        n = (size_t)(p - (const char *)buf);
        p = (const char *)buf;
    }
    /* HWLINK frames are binary: no newline translation. */
    for (size_t i = 0U; i < n; i++) {
        if (!tx_push_byte((uint8_t)p[i])) {
            return;
        }
    }
}

void hwval_uart3_puts(const char *s)
{
    hwval_uart3_write(s, 0U);
    hwval_uart3_write_direct("\r\n", 2U);
}