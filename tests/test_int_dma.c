/*
 * tests/test_int_dma.c - interrupts/EXTI/SYSCFG (8.11) and DMA (8.12)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     INT-001..INT-008, DMA-001..DMA-007 (Level-S parts; harness
 *          parts SKIP NO_HARNESS).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "app/test_core.h"
#include "app/harness.h"
#include "services/fault.h"

/* ------------------------------------------------- INT-001 (NVIC scan) -- */
#define HWVAL_IRQ_MAX 91
static volatile uint32_t s_irq_hits[HWVAL_IRQ_MAX];
static IRQn_Type        s_pending_irq;

/* One handler per vector is impossible; instead pend each IRQ that shares
 * a known vector we own (TIM2/3/4/5, USART2, I2C2, DMA2 s7, EXTI0..4,
 * SPI2, WWDG?) — a full 112-IRQ software-pend scan needs 112 handlers.
 * The SRS requires every IRQ line pending+handled: use the Default vector
 * slot trick: temporarily REPURPOSE the shared Default_Handler: measure
 * which handler count fires via a per-IRQ table of owned handlers. */
static const IRQn_Type k_owned[] = {
    TIM2_IRQn, TIM3_IRQn, TIM4_IRQn, TIM5_IRQn, USART2_IRQn, USART6_IRQn,
    UART4_IRQn, UART5_IRQn, SPI1_IRQn, SPI2_IRQn, SPI3_IRQn,
    I2C1_IRQn, I2C2_IRQn, DMA1_Stream0_IRQn, DMA1_Stream6_IRQn,
    DMA2_Stream0_IRQn, DMA2_Stream7_IRQn, TIM1_UP_TIM10_IRQn,
    TIM1_TRG_COM_TIM11_IRQn, TIM8_UP_TIM13_IRQn, TIM8_TRG_COM_TIM14_IRQn,
    RCC_IRQn (0)
};