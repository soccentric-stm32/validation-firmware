/**
 * @file    test_int_dma.c
 * @brief   Interrupt/EXTI/SYSCFG (8.11) and DMA (8.12) draft.
 * @details Unfinished draft, not in the build; superseded by test_int.c and test_dma.c.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     INT-001..INT-008, DMA-001..DMA-007
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
#define HWVAL_IRQ_MAX 91                            /**< Size of the per-IRQ hit table. */
static volatile uint32_t s_irq_hits[HWVAL_IRQ_MAX]; /**< Handler hits per IRQ. */
static IRQn_Type         s_pending_irq;             /**< IRQ under test. */

/** IRQs with a handler owned by this firmware, pended one at a time. A full
 *  software-pend scan of every line would need one handler per vector. */
static const IRQn_Type k_owned[] = {TIM2_IRQn,
                                    TIM3_IRQn,
                                    TIM4_IRQn,
                                    TIM5_IRQn,
                                    USART2_IRQn,
                                    USART6_IRQn,
                                    UART4_IRQn,
                                    UART5_IRQn,
                                    SPI1_IRQn,
                                    SPI2_IRQn,
                                    SPI3_IRQn,
                                    I2C1_IRQn,
                                    I2C2_IRQn,
                                    DMA1_Stream0_IRQn,
                                    DMA1_Stream6_IRQn,
                                    DMA2_Stream0_IRQn,
                                    DMA2_Stream7_IRQn,
                                    TIM1_UP_TIM10_IRQn,
                                    TIM1_TRG_COM_TIM11_IRQn,
                                    TIM8_UP_TIM13_IRQn,
                                    TIM8_TRG_COM_TIM14_IRQn,
                                    RCC_IRQn(0)};