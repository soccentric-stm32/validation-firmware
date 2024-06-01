/**
 * @file    clock.c
 * @brief   216 MHz core clock and test-time clock switching.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @target  STM32F767ZIT6 rev Z @ 3.3 V (voltage scale 1)
 * @srs     GEN-004 (216 MHz, scale 1, OD, 7 WS, ART, prefetch, caches),
 *          GEN-005 (the configuration tests must restore); [DS11532],
 *          [RM0410] RCC chapter
 */

#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "bsp/clock.h"
#include "protocol/link.h"

/**
 * @brief Configure the GEN-004 default clock tree.
 * @warning Hangs in a NOP loop if the PLL or over-drive fails.
 */
void hwval_clock_init(void)
{
    /* PWR register access needs the PWR interface clock (APB1) first. */
    __HAL_RCC_PWR_CLK_ENABLE();

    /* Voltage scale 1 (2.7-3.6 V) before raising the frequency. No wait for
     * PWR_FLAG_VOSRDY (CL-009): it never sets before the PLL is up on this
     * part (measured: 0 for >1.5 s while scale 1 was in effect), and ST's
     * own Nucleo applications do not wait either. */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState            = RCC_HSE_BYPASS; /* ST-LINK MCO 8 MHz (CON-005) */
    osc.HSIState            = RCC_HSI_OFF;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM            = 8;              /* VCO in: 1 MHz               */
    osc.PLL.PLLN            = 432;            /* VCO: 432 MHz                */
    osc.PLL.PLLP            = RCC_PLLP_DIV2;  /* SYSCLK: 216 MHz             */
    osc.PLL.PLLQ            = 9;              /* PLLQ: 48 MHz (USB/RNG/SD)   */

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        while (1) { /* PLL did not lock: hang with a marker for the debugger */
            __NOP();
        }
    }

    /* 216 MHz needs over-drive in voltage scale 1 ([DS11532]). HAL_PWREx
     * enables OD and waits for ODRDY and ODSWRDY internally. */
    if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
        while (1) {
            __NOP();
        }
    }

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* HCLK 216 MHz               */
    clk.APB1CLKDivider = HWVAL_APB1_PRESCALER; /* 54 MHz, timers 108 MHz  */
    clk.APB2CLKDivider = HWVAL_APB2_PRESCALER; /* 108 MHz, timers 216 MHz */
    /* 7 wait states for VOS1 @ 216 MHz. */
    (void)HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7);

    /* ART accelerator, prefetch and caches (GEN-004). */
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
    __HAL_FLASH_ART_ENABLE();
    SCB_EnableICache();
    SCB_EnableDCache();

    /* HAL_RCC_ClockConfig re-runs HAL_InitTick(uwTickPrio) in this HAL
     * generation; re-init the tick if it is not 1 kHz. */
    if (uwTickFreq != 1U) {
        HAL_InitTick(uwTickPrio);
    }
}

/**
 * @brief Switch HCLK via HSI to a PLL frequency with a given flash latency.
 * @param[in] hclk_hz Target HCLK, Hz.
 * @param[in] ws      Flash latency.
 * @return 0 on success, -1..-4 for the failing step.
 */
int hwval_clock_set(uint32_t hclk_hz, uint32_t ws)
{
    RCC_ClkInitTypeDef clk = { 0 };
    RCC_OscInitTypeDef osc = { 0 };

    /* UART baud is derived from PCLK1; while the tree is off-nominal,
     * unsolicited link traffic would be garbage. */
    hwval_link_set_quiet(1);

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) {
        return -1;
    }
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState            = RCC_HSE_BYPASS;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM            = 8U;
    /* VCO = HSE / PLLM * PLLN = 1 MHz * PLLN; SYSCLK = VCO / PLLP.
     * PLLP is DIV2, so PLLN must be twice the requested SYSCLK in MHz. */
    osc.PLL.PLLN            = (hclk_hz / 1000000U) * 2U;
    osc.PLL.PLLP            = RCC_PLLP_DIV2;
    osc.PLL.PLLQ            = 9U;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return -2;
    }
    if (hclk_hz >= 180000000U) {
        if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
            return -3;
        }
    }
    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, ws) != HAL_OK) {
        return -4;
    }
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
    __HAL_FLASH_ART_ENABLE();
    SCB_EnableICache();
    SCB_EnableDCache();
    return 0;
}

/**
 * @brief Restore the default tree and leave link quiet mode.
 * @return Result of @ref hwval_clock_set.
 */
int hwval_clock_restore_default(void)
{
    int rc = hwval_clock_set(216000000U, 7U);
    hwval_link_set_quiet(0);
    return rc;
}

/**
 * @brief Read the current clock tree.
 * @param[out] t Snapshot; @c pllvco_hz is not written.
 */
void hwval_clock_get_tree(hwval_clock_tree_t *t)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    uint32_t apb1_pre = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
    uint32_t apb2_pre = (RCC->CFGR & RCC_CFGR_PPRE2) >> RCC_CFGR_PPRE2_Pos;

    t->hclk_hz     = HAL_RCC_GetHCLKFreq();
    t->sysclk_hz   = HAL_RCC_GetSysClockFreq();
    t->pclk1_hz    = pclk1;
    t->pclk2_hz    = pclk2;
    /* Timer clock is 2x PCLK unless the APB prescaler is 1 (RM0410, CL-023);
     * "divide by 1" is any code 0xx. */
    t->apb1_tim_hz = ((apb1_pre & 0x4U) == 0U) ? pclk1 : pclk1 * 2U;
    t->apb2_tim_hz = ((apb2_pre & 0x4U) == 0U) ? pclk2 : pclk2 * 2U;
    t->hse_hz      = HWVAL_HSE_FREQ_HZ;
    t->hsi_hz      = HSI_VALUE;
    t->lsi_hz      = LSI_VALUE;
    t->lse_hz      = LSE_VALUE;
    t->pllq_hz     = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_CLK48);
}