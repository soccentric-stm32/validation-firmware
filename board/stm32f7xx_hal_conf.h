/**
 * @file    stm32f7xx_hal_conf.h
 * @brief   Project-owned STM32F7 HAL configuration.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @target  STM32F767ZIT6, NUCLEO-F767ZI
 * @srs     CON-002 (drivers used as shipped), GEN-036/008 (assert text via
 *          fault.c)
 */

#ifndef STM32F7xx_HAL_CONF_H
#define STM32F7xx_HAL_CONF_H

/* Oscillators (Hardware.md section 6; CON-005: PH0 carries the ST-LINK 8 MHz
 * MCO in bypass mode). */
#define HSE_VALUE    8000000U     /**< HSE frequency, Hz. */
#define HSE_STARTUP_TIMEOUT 100U  /**< HSE start-up timeout, ms. */
#define HSI_VALUE   16000000U     /**< HSI frequency, Hz. */
#define LSI_VALUE    32000U       /**< LSI typical frequency, Hz. */
#define LSE_VALUE    32768U       /**< LSE frequency, Hz. */
#define LSE_STARTUP_TIMEOUT 5000U /**< LSE start-up timeout, ms. */
#define EXTERNAL_SAI1_CLOCK_VALUE 48000U /**< SAI1 external clock, Hz. */
#define EXTERNAL_SAI2_CLOCK_VALUE 48000U /**< SAI2 external clock, Hz. */
#define EXTERNAL_CLOCK_VALUE 12288000U   /**< I2S external clock, Hz. */

#define VDD_VALUE   3300U /**< VDD, mV. */
/* Legacy PHY timeouts required by the as-shipped ETH HAL. */
#ifndef PHY_READ_TO
#define PHY_READ_TO 5000U /**< PHY read timeout, ms. */
#endif
#ifndef PHY_WRITE_TO
#define PHY_WRITE_TO 5000U /**< PHY write timeout, ms. */
#endif
#define TICK_INT_PRIORITY 15U /**< HAL tick (TIM6) priority; lowest. */
#define USE_RTOS 0U           /**< HAL RTOS locking off. */
#define PREFETCH_ENABLE 1U    /**< Flash prefetch on. */

/* Enabled HAL modules; the list grows per SRS section (NFR-008). */
#define HAL_MODULE_ENABLED         /**< HAL core. */
#define HAL_CORTEX_MODULE_ENABLED  /**< CORTEX (NVIC/SysTick/MPU). */
#define HAL_RCC_MODULE_ENABLED     /**< RCC. */
#define HAL_GPIO_MODULE_ENABLED    /**< GPIO. */
#define HAL_PWR_MODULE_ENABLED     /**< PWR. */
#define HAL_FLASH_MODULE_ENABLED   /**< FLASH. */
#define HAL_DMA_MODULE_ENABLED     /**< DMA. */
#define HAL_IWDG_MODULE_ENABLED    /**< IWDG. */
#define HAL_CRC_MODULE_ENABLED     /**< CRC. */
#define HAL_ETH_MODULE_ENABLED     /**< ETH, for the LwIP stack (ETH-007/008). */

#include "stm32f7xx_hal_rcc.h"
#include "stm32f7xx_hal_gpio.h"
#include "stm32f7xx_hal_cortex.h"
#include "stm32f7xx_hal_pwr.h"
#include "stm32f7xx_hal_pwr_ex.h"
#include "stm32f7xx_hal_flash.h"
#include "stm32f7xx_hal_flash_ex.h"
#include "stm32f7xx_hal_dma.h"
#include "stm32f7xx_hal_iwdg.h"
#include "stm32f7xx_hal_crc.h"
#include "stm32f7xx_hal_eth.h"

#ifdef USE_FULL_ASSERT
/**
 * @brief Report a failed HAL assert_param.
 * @param[in] file Source file name.
 * @param[in] line Source line.
 */
void assert_failed(uint8_t *file, uint32_t line);
#endif

#ifdef  USE_FULL_ASSERT
/** HAL parameter check; calls assert_failed on failure. @param expr Condition. */
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
/** HAL parameter check; compiled out. @param expr Condition (unused). */
  #define assert_param(expr) ((void)0U)
#endif

#endif /* STM32F7xx_HAL_CONF_H */
