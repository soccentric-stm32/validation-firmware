/**
 * @file    hal_timebase.c
 * @brief   HAL 1 ms tick on TIM6.
 * @details SysTick belongs to FreeRTOS (FRT-002), so the HAL time base uses
 *          TIM6; TIM7 stays free for DAC triggering (DAC-003). TIM6 is excluded
 *          from destructive timer tests. uwTick/uwTickPrio stay defined in
 *          stm32f7xx_hal.c.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @target  STM32F767ZIT6
 * @srs     GEN-012
 */

#include "stm32f7xx_hal.h"

/** @brief TIM6/DAC vector; declared here as no header provides it. */
void TIM6_DAC_IRQHandler(void);

/**
 * @brief Start TIM6 as the 1 kHz HAL tick (overrides the HAL weak version).
 * @param[in] TickPriority NVIC priority for TIM6_DAC_IRQn.
 * @return Always HAL_OK.
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    /* APB timers run at 2x PCLK unless the APB prescaler is 1 (RM0410,
     * CL-023). */
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == 0U) ? pclk1 : pclk1 * 2U;
    uint32_t prescaller = (timclk / 1000000U) - 1U;

    __HAL_RCC_TIM6_CLK_ENABLE();

    TIM6->CR1 = 0U;
    TIM6->PSC = prescaller;      /* 1 MHz count */
    TIM6->ARR = 1000U - 1U;      /* 1 ms period */
    TIM6->EGR = TIM_EGR_UG;      /* Latch PSC/ARR, reset the counter. */
    TIM6->SR  = 0U;
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->CR1 = TIM_CR1_CEN | TIM_CR1_URS;

    uwTickPrio = TickPriority;
    NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);

    return HAL_OK;
}

/** @brief Mask the TIM6 update interrupt; used by Stop-mode tests (PWR-006). */
void HAL_SuspendTick(void)
{
    TIM6->DIER &= ~TIM_DIER_UIE;
}

/** @brief Clear pending TIM6 flags and unmask the update interrupt. */
void HAL_ResumeTick(void)
{
    TIM6->SR = 0U;
    TIM6->DIER |= TIM_DIER_UIE;
}

/**
 * @brief Advance uwTick on each TIM6 update.
 * @note ISR context.
 */
void TIM6_DAC_IRQHandler(void)
{
    if ((TIM6->SR & TIM_SR_UIF) != 0U) {
        TIM6->SR = ~TIM_SR_UIF;
        uwTick += uwTickFreq;
    }
    /* DAC1 underrun shares this vector; section 8.16 (DAC-005) adds its
     * dispatch to this handler when those tests exist. */
}
