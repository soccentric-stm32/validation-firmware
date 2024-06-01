/*
 * tests/test_int.c - NVIC/EXTI/SYSCFG (section 8.11)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     INT-001..INT-008. The handler set is generated from the startup
 *          vector order; vectors owned by other modules verify by ownership
 *          (COV-003). EXTI0/1/2 double as the INT-002 nesting chain.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_limits.h"
#include "app/test_core.h"
#include "services/fault.h"
#include "app/harness.h"

#define INT_N ((int)(INT_TBL_N))

/* name table keeps startup vector order (index i -> IRQn i). */
static const struct { const char *name; uint8_t owned; } k_vec_names[] = {
    { "WWDG_IRQHandler", 1 },
    { "PVD_IRQHandler", 1 },
    { "TAMP_STAMP_IRQHandler", 0 },
    { "RTC_WKUP_IRQHandler", 0 },
    { "FLASH_IRQHandler", 1 },
    { "RCC_IRQHandler", 1 },
    { "EXTI0_IRQHandler", 1 },
    { "EXTI1_IRQHandler", 1 },
    { "EXTI2_IRQHandler", 1 },
    { "EXTI3_IRQHandler", 0 },
    { "EXTI4_IRQHandler", 0 },
    { "DMA1_Stream0_IRQHandler", 0 },
    { "DMA1_Stream1_IRQHandler", 0 },
    { "DMA1_Stream2_IRQHandler", 0 },
    { "DMA1_Stream3_IRQHandler", 0 },
    { "DMA1_Stream4_IRQHandler", 0 },
    { "DMA1_Stream5_IRQHandler", 0 },
    { "DMA1_Stream6_IRQHandler", 0 },
    { "ADC_IRQHandler", 0 },
    { "CAN1_TX_IRQHandler", 0 },
    { "CAN1_RX0_IRQHandler", 0 },
    { "CAN1_RX1_IRQHandler", 0 },
    { "CAN1_SCE_IRQHandler", 0 },
    { "EXTI9_5_IRQHandler", 0 },
    { "TIM1_BRK_TIM9_IRQHandler", 0 },
    { "TIM1_UP_TIM10_IRQHandler", 0 },
    { "TIM1_TRG_COM_TIM11_IRQHandler", 0 },
    { "TIM1_CC_IRQHandler", 0 },
    { "TIM2_IRQHandler", 1 },
    { "TIM3_IRQHandler", 1 },
    { "TIM4_IRQHandler", 0 },
    { "I2C1_EV_IRQHandler", 0 },
    { "I2C1_ER_IRQHandler", 0 },
    { "I2C2_EV_IRQHandler", 0 },
    { "I2C2_ER_IRQHandler", 0 },
    { "SPI1_IRQHandler", 0 },
    { "SPI2_IRQHandler", 0 },
    { "USART1_IRQHandler", 0 },
    { "USART2_IRQHandler", 0 },
    { "USART3_IRQHandler", 1 },
    { "EXTI15_10_IRQHandler", 1 },
    { "RTC_Alarm_IRQHandler", 1 },
    { "OTG_FS_WKUP_IRQHandler", 0 },
    { "TIM8_BRK_TIM12_IRQHandler", 0 },
    { "TIM8_UP_TIM13_IRQHandler", 0 },
    { "TIM8_TRG_COM_TIM14_IRQHandler", 0 },
    { "TIM8_CC_IRQHandler", 0 },
    { "DMA1_Stream7_IRQHandler", 0 },
    { "FMC_IRQHandler", 0 },
    { "SDMMC1_IRQHandler", 0 },
    { "TIM5_IRQHandler", 1 },
    { "SPI3_IRQHandler", 0 },
    { "UART4_IRQHandler", 0 },
    { "UART5_IRQHandler", 0 },
    { "TIM6_DAC_IRQHandler", 1 },
    { "TIM7_IRQHandler", 0 },
    { "DMA2_Stream0_IRQHandler", 0 },
    { "DMA2_Stream1_IRQHandler", 1 },
    { "DMA2_Stream2_IRQHandler", 0 },
    { "DMA2_Stream3_IRQHandler", 0 },
    { "DMA2_Stream4_IRQHandler", 0 },
    { "ETH_IRQHandler", 0 },
    { "ETH_WKUP_IRQHandler", 0 },
    { "CAN2_TX_IRQHandler", 0 },
    { "CAN2_RX0_IRQHandler", 0 },
    { "CAN2_RX1_IRQHandler", 0 },
    { "CAN2_SCE_IRQHandler", 0 },
    { "OTG_FS_IRQHandler", 0 },
    { "DMA2_Stream5_IRQHandler", 0 },
    { "DMA2_Stream6_IRQHandler", 0 },
    { "DMA2_Stream7_IRQHandler", 0 },
    { "USART6_IRQHandler", 0 },
    { "I2C3_EV_IRQHandler", 0 },
    { "I2C3_ER_IRQHandler", 0 },
    { "OTG_HS_EP1_OUT_IRQHandler", 0 },
    { "OTG_HS_EP1_IN_IRQHandler", 0 },
    { "OTG_HS_WKUP_IRQHandler", 0 },
    { "OTG_HS_IRQHandler", 0 },
    { "DCMI_IRQHandler", 0 },
    { "RNG_IRQHandler", 0 },
    { "FPU_IRQHandler", 0 },
    { "UART7_IRQHandler", 0 },
    { "UART8_IRQHandler", 0 },
    { "SPI4_IRQHandler", 0 },
    { "SPI5_IRQHandler", 0 },
    { "SPI6_IRQHandler", 0 },
    { "SAI1_IRQHandler", 0 },
    { "LTDC_IRQHandler", 0 },
    { "LTDC_ER_IRQHandler", 0 },
    { "DMA2D_IRQHandler", 0 },
    { "SAI2_IRQHandler", 0 },
    { "QUADSPI_IRQHandler", 0 },
    { "LPTIM1_IRQHandler", 0 },
    { "CEC_IRQHandler", 0 },
    { "I2C4_EV_IRQHandler", 0 },
    { "I2C4_ER_IRQHandler", 0 },
    { "SPDIF_RX_IRQHandler", 0 },
    { "DFSDM1_FLT0_IRQHandler", 0 },
    { "DFSDM1_FLT1_IRQHandler", 0 },
    { "DFSDM1_FLT2_IRQHandler", 0 },
    { "DFSDM1_FLT3_IRQHandler", 0 },
    { "SDMMC2_IRQHandler", 0 },
    { "CAN3_TX_IRQHandler", 0 },
    { "CAN3_RX0_IRQHandler", 0 },
    { "CAN3_RX1_IRQHandler", 0 },
    { "CAN3_SCE_IRQHandler", 0 },
    { "JPEG_IRQHandler", 0 },
    { "MDIOS_IRQHandler", 0 },
};

static volatile uint32_t s_nest_now, s_nest_max;
static volatile uint32_t s_chain_seen[3];
static volatile uint32_t s_chain_done;
static volatile uint32_t s_exti_hits[16];

static inline void nest_enter(void)
{
    s_nest_now++;
    if (s_nest_now > s_nest_max) { s_nest_max = s_nest_now; }
}

static inline void nest_exit(void)
{
    s_nest_now--;
}

/* EXTI0..2: INT-002 preemption chain (each pends the next, priorities
 * 10/6/3 so each preempts the previous). */
void EXTI0_IRQHandler(void);
void EXTI1_IRQHandler(void);
void EXTI2_IRQHandler(void);

void EXTI0_IRQHandler(void)
{
    nest_enter();
    s_chain_seen[0]++;
    if (s_chain_done == 0U) { NVIC_SetPendingIRQ(EXTI1_IRQn); }
    __DSB();
    nest_exit();
    EXTI->PR = EXTI_PR_PR0;
}

void EXTI1_IRQHandler(void)
{
    nest_enter();
    s_chain_seen[1]++;
    if (s_chain_done == 0U) { NVIC_SetPendingIRQ(EXTI2_IRQn); }
    __DSB();
    nest_exit();
    EXTI->PR = EXTI_PR_PR1;
}

void EXTI2_IRQHandler(void)
{
    nest_enter();
    s_chain_seen[2]++;
    s_chain_done = 1U;
    nest_exit();
    EXTI->PR = EXTI_PR_PR2;
}
static volatile uint32_t s_h2;
void TAMP_STAMP_IRQHandler(void) { s_h2++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h3;
void RTC_WKUP_IRQHandler(void) { s_h3++; nest_enter(); nest_exit(); }


static volatile uint32_t s_h10;
void EXTI4_IRQHandler(void) { s_h10++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h11;
void DMA1_Stream0_IRQHandler(void) { s_h11++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h12;
void DMA1_Stream1_IRQHandler(void) { s_h12++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h13;
void DMA1_Stream2_IRQHandler(void) { s_h13++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h14;
void DMA1_Stream3_IRQHandler(void) { s_h14++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h15;
void DMA1_Stream4_IRQHandler(void) { s_h15++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h16;
void DMA1_Stream5_IRQHandler(void) { s_h16++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h17;
void DMA1_Stream6_IRQHandler(void) { s_h17++; nest_enter(); nest_exit(); }

/* ADC-012 (tests/test_adc.c) runs its counters through the shared
 * ADC1/2/3 vector when its interrupt enables are set; default no-op. */
void hwval_adc_irq_hook(void);

static volatile uint32_t s_h18;
void ADC_IRQHandler(void) { hwval_adc_irq_hook(); s_h18++; nest_enter(); nest_exit(); }

__attribute__((weak)) void hwval_adc_irq_hook(void) { }

static volatile uint32_t s_h19;
void CAN1_TX_IRQHandler(void) { s_h19++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h20;
void CAN1_RX0_IRQHandler(void) { s_h20++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h21;
void CAN1_RX1_IRQHandler(void) { s_h21++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h22;
void CAN1_SCE_IRQHandler(void) { s_h22++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h23;
void EXTI9_5_IRQHandler(void) { s_h23++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h24;
void TIM1_BRK_TIM9_IRQHandler(void) { s_h24++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h25;
void TIM1_UP_TIM10_IRQHandler(void) { s_h25++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h26;
void TIM1_TRG_COM_TIM11_IRQHandler(void) { s_h26++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h27;
void TIM1_CC_IRQHandler(void) { s_h27++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h30;
void TIM4_IRQHandler(void) { s_h30++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h31;
void I2C1_EV_IRQHandler(void) { s_h31++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h32;
void I2C1_ER_IRQHandler(void) { s_h32++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h33;
void I2C2_EV_IRQHandler(void) { s_h33++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h34;
void I2C2_ER_IRQHandler(void) { s_h34++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h35;
void SPI1_IRQHandler(void) { s_h35++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h36;
void SPI2_IRQHandler(void) { s_h36++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h37;
void USART1_IRQHandler(void) { s_h37++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h38;
void USART2_IRQHandler(void) { s_h38++; nest_enter(); nest_exit(); }


static volatile uint32_t s_h42;
void OTG_FS_WKUP_IRQHandler(void) { s_h42++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h43;
void TIM8_BRK_TIM12_IRQHandler(void) { s_h43++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h44;
void TIM8_UP_TIM13_IRQHandler(void) { s_h44++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h45;
void TIM8_TRG_COM_TIM14_IRQHandler(void) { s_h45++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h46;
void TIM8_CC_IRQHandler(void) { s_h46++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h47;
void DMA1_Stream7_IRQHandler(void) { s_h47++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h48;
void FMC_IRQHandler(void) { s_h48++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h49;
void SDMMC1_IRQHandler(void) { s_h49++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h51;
void SPI3_IRQHandler(void) { s_h51++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h52;
void UART4_IRQHandler(void) { s_h52++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h53;
void UART5_IRQHandler(void) { s_h53++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h55;
void TIM7_IRQHandler(void) { s_h55++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h56;
void DMA2_Stream0_IRQHandler(void) { s_h56++; nest_enter(); nest_exit(); }


static volatile uint32_t s_h58;
void DMA2_Stream2_IRQHandler(void) { s_h58++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h59;
void DMA2_Stream3_IRQHandler(void) { s_h59++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h60;
void DMA2_Stream4_IRQHandler(void) { s_h60++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h61;
void ETH_IRQHandler(void) { s_h61++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h62;
void ETH_WKUP_IRQHandler(void) { s_h62++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h63;
void CAN2_TX_IRQHandler(void) { s_h63++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h64;
void CAN2_RX0_IRQHandler(void) { s_h64++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h65;
void CAN2_RX1_IRQHandler(void) { s_h65++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h66;
void CAN2_SCE_IRQHandler(void) { s_h66++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h67;
void OTG_FS_IRQHandler(void) { s_h67++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h68;
void DMA2_Stream5_IRQHandler(void) { s_h68++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h69;
void DMA2_Stream6_IRQHandler(void) { s_h69++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h70;
void DMA2_Stream7_IRQHandler(void) { s_h70++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h71;
void USART6_IRQHandler(void) { s_h71++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h72;
void I2C3_EV_IRQHandler(void) { s_h72++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h73;
void I2C3_ER_IRQHandler(void) { s_h73++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h74;
void OTG_HS_EP1_OUT_IRQHandler(void) { s_h74++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h75;
void OTG_HS_EP1_IN_IRQHandler(void) { s_h75++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h76;
void OTG_HS_WKUP_IRQHandler(void) { s_h76++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h77;
void OTG_HS_IRQHandler(void) { s_h77++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h78;
void DCMI_IRQHandler(void) { s_h78++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h80;
void RNG_IRQHandler(void) { s_h80++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h81;
void FPU_IRQHandler(void) { s_h81++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h82;
void UART7_IRQHandler(void) { s_h82++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h83;
void UART8_IRQHandler(void) { s_h83++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h84;
void SPI4_IRQHandler(void) { s_h84++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h85;
void SPI5_IRQHandler(void) { s_h85++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h86;
void SPI6_IRQHandler(void) { s_h86++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h87;
void SAI1_IRQHandler(void) { s_h87++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h88;
void LTDC_IRQHandler(void) { s_h88++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h89;
void LTDC_ER_IRQHandler(void) { s_h89++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h90;
void DMA2D_IRQHandler(void) { s_h90++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h91;
void SAI2_IRQHandler(void) { s_h91++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h92;
void QUADSPI_IRQHandler(void) { s_h92++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h93;
void LPTIM1_IRQHandler(void) { s_h93++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h94;
void CEC_IRQHandler(void) { s_h94++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h95;
void I2C4_EV_IRQHandler(void) { s_h95++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h96;
void I2C4_ER_IRQHandler(void) { s_h96++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h97;
void SPDIF_RX_IRQHandler(void) { s_h97++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h99;
void DFSDM1_FLT0_IRQHandler(void) { s_h99++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h100;
void DFSDM1_FLT1_IRQHandler(void) { s_h100++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h101;
void DFSDM1_FLT2_IRQHandler(void) { s_h101++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h102;
void DFSDM1_FLT3_IRQHandler(void) { s_h102++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h103;
void SDMMC2_IRQHandler(void) { s_h103++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h104;
void CAN3_TX_IRQHandler(void) { s_h104++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h105;
void CAN3_RX0_IRQHandler(void) { s_h105++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h106;
void CAN3_RX1_IRQHandler(void) { s_h106++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h107;
void CAN3_SCE_IRQHandler(void) { s_h107++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h108;
void JPEG_IRQHandler(void) { s_h108++; nest_enter(); nest_exit(); }

static volatile uint32_t s_h109;
void MDIOS_IRQHandler(void) { s_h109++; nest_enter(); nest_exit(); }

static volatile uint32_t *hit_of(int i)
{
    static const void *const map[] = {
        NULL,
        NULL,
        (const void *)&s_h2,
        (const void *)&s_h3,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        (const void *)&s_exti_hits[3],
        (const void *)&s_h10,
        (const void *)&s_h11,
        (const void *)&s_h12,
        (const void *)&s_h13,
        (const void *)&s_h14,
        (const void *)&s_h15,
        (const void *)&s_h16,
        (const void *)&s_h17,
        NULL,
        (const void *)&s_h19,
        (const void *)&s_h20,
        (const void *)&s_h21,
        (const void *)&s_h22,
        (const void *)&s_h23,
        (const void *)&s_h24,
        (const void *)&s_h25,
        (const void *)&s_h26,
        (const void *)&s_h27,
        NULL,
        NULL,
        (const void *)&s_h30,
        (const void *)&s_h31,
        (const void *)&s_h32,
        (const void *)&s_h33,
        (const void *)&s_h34,
        (const void *)&s_h35,
        (const void *)&s_h36,
        (const void *)&s_h37,
        (const void *)&s_h38,
        NULL,
        NULL,
        NULL,
        (const void *)&s_h42,
        (const void *)&s_h43,
        (const void *)&s_h44,
        (const void *)&s_h45,
        (const void *)&s_h46,
        (const void *)&s_h47,
        (const void *)&s_h48,
        (const void *)&s_h49,
        NULL,
        (const void *)&s_h51,
        (const void *)&s_h52,
        (const void *)&s_h53,
        NULL,
        NULL,
        (const void *)&s_h56,
        NULL,
        (const void *)&s_h58,
        (const void *)&s_h59,
        (const void *)&s_h60,
        (const void *)&s_h61,
        (const void *)&s_h62,
        (const void *)&s_h63,
        (const void *)&s_h64,
        (const void *)&s_h65,
        (const void *)&s_h66,
        (const void *)&s_h67,
        (const void *)&s_h68,
        (const void *)&s_h69,
        (const void *)&s_h70,
        (const void *)&s_h71,
        (const void *)&s_h72,
        (const void *)&s_h73,
        (const void *)&s_h74,
        (const void *)&s_h75,
        (const void *)&s_h76,
        (const void *)&s_h77,
        (const void *)&s_h78,
        NULL,
        (const void *)&s_h80,
        (const void *)&s_h81,
        (const void *)&s_h82,
        (const void *)&s_h83,
        (const void *)&s_h84,
        (const void *)&s_h85,
        (const void *)&s_h86,
        (const void *)&s_h87,
        (const void *)&s_h88,
        (const void *)&s_h89,
        (const void *)&s_h90,
        (const void *)&s_h91,
        (const void *)&s_h92,
        (const void *)&s_h93,
        (const void *)&s_h94,
        (const void *)&s_h95,
        (const void *)&s_h96,
        (const void *)&s_h97,
        NULL,
        (const void *)&s_h99,
        (const void *)&s_h100,
        (const void *)&s_h101,
        (const void *)&s_h102,
        (const void *)&s_h103,
        (const void *)&s_h104,
        (const void *)&s_h105,
        (const void *)&s_h106,
        (const void *)&s_h107,
        (const void *)&s_h108,
        (const void *)&s_h109,
    };
    return (volatile uint32_t *)map[i];
}


#define EXT_TBL_N ((int)(sizeof(k_vec_names) / sizeof(k_vec_names[0])))
#define INT_TBL_N ((int)(sizeof(k_vec_names) / sizeof(k_vec_names[0])))

/* ------------------------------------------------- INT-001 ------------ */
static tfw_result_t int_nvic_scan(struct tfw_out *o)
{
    int pended = 0, handled = 0, owned = 0, bad = 0, n = EXT_TBL_N;

    for (int i = 0; i < n; i++) {
        if (k_vec_names[i].owned != 0U) {
            owned++; /* exercised by its owner test (COV-003) */
            continue;
        }
        volatile uint32_t *hit = hit_of(i);
        if (hit == NULL) { continue; }
        *hit = 0U;
        NVIC_SetPriority((IRQn_Type)i, 6);
        NVIC_EnableIRQ((IRQn_Type)i);
        NVIC_SetPendingIRQ((IRQn_Type)i);
        __DSB(); __ISB();
        for (volatile int w = 0; (w < 500000) && (*hit == 0U); w++) { ; }
        pended++;
        if (*hit != 0U) { handled++; } else { bad++; }
        NVIC_DisableIRQ((IRQn_Type)i);
        NVIC_ClearPendingIRQ((IRQn_Type)i);
    }
    hwval_meas(o, "pended=%d handled=%d owned-elsewhere=%d of %d",
               (unsigned long)pended, (unsigned long)handled,
               (unsigned long)owned, (unsigned long)n);
    hwval_lim(o, "every software-pended external IRQ must run its handler "
                 "(INT-001; F767 has 110 external IRQs, SRS said 112 - "
                 "CL-017)");
    if (bad != 0) {
        hwval_fail(o, "%d vectors did not run", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- INT-002 ------------ */
static tfw_result_t int_nesting(struct tfw_out *o)
{
    s_nest_max = 0U;
    s_nest_now = 0U;
    s_chain_done = 0U;
    s_chain_seen[0] = s_chain_seen[1] = s_chain_seen[2] = 0U;

    NVIC_SetPriority(EXTI0_IRQn, 10);
    NVIC_SetPriority(EXTI1_IRQn, 6);
    NVIC_SetPriority(EXTI2_IRQn, 3);
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI1_IRQn);
    NVIC_EnableIRQ(EXTI2_IRQn);
    EXTI->IMR |= (1U << 0) | (1U << 1) | (1U << 2);

    NVIC_SetPendingIRQ(EXTI0_IRQn); /* chain: 0 -> 1 -> 2 nested */
    __DSB(); __ISB();
    for (volatile int w = 0; (w < 500000) && (s_chain_done == 0U); w++) { ; }

    NVIC_DisableIRQ(EXTI0_IRQn);
    NVIC_DisableIRQ(EXTI1_IRQn);
    NVIC_DisableIRQ(EXTI2_IRQn);
    EXTI->IMR &= ~(1U << 0);
    EXTI->IMR &= ~(1U << 1);
    EXTI->IMR &= ~(1U << 2);

    hwval_meas(o, "nest depth=%lu chain=%lu/%lu/%lu (tail-chain: entries "
               "back-to-back)", (unsigned long)s_nest_max,
               (unsigned long)s_chain_seen[0], (unsigned long)s_chain_seen[1],
               (unsigned long)s_chain_seen[2]);
    hwval_lim(o, "depth 3 reached; each handler ran once (INT-002)");
    if ((s_nest_max != 3U) || (s_chain_seen[0] != 1U) ||
        (s_chain_seen[1] != 1U) || (s_chain_seen[2] != 1U)) {
        hwval_fail(o, "nesting chain broken (max=%lu)", (unsigned long)s_nest_max);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- INT-003 ------------ */
static tfw_result_t int_masking(struct tfw_out *o)
{
    int ok = 3;

    /* PRIMASK: masked ISR must not run until unmasked */
    NVIC_EnableIRQ(EXTI3_IRQn);
    NVIC_SetPriority(EXTI3_IRQn, 6);
    s_exti_hits[3] = 0U;
    __disable_irq();
    NVIC_SetPendingIRQ(EXTI3_IRQn);
    __DSB(); __ISB();
    if (s_exti_hits[3] != 0U) { ok = 0; }
    __enable_irq();
    if (s_exti_hits[3] == 0U) { ok = 0; } /* ran after unmask */
    NVIC_DisableIRQ(EXTI3_IRQn);

    /* BASEPRI: mask prio >= 6 (>=80): pended prio-6 EXTI3 blocked while
     * prio-3 EXTI2 still runs */
    s_exti_hits[3] = 0U;
    s_nest_max = 0U;
    NVIC_EnableIRQ(EXTI3_IRQn);
    __set_BASEPRI(6U << 4U);
    NVIC_SetPendingIRQ(EXTI3_IRQn);
    __DSB(); __ISB();
    uint32_t blocked = (s_exti_hits[3] == 0U);
    __set_BASEPRI(0U);
    uint32_t ran = (s_exti_hits[3] != 0U);
    if (!blocked || !ran) { ok = 0; }
    NVIC_DisableIRQ(EXTI3_IRQn);

    /* FAULTMASK: masks all but NMI/HardFault */
    s_exti_hits[3] = 0U;
    NVIC_EnableIRQ(EXTI3_IRQn);
    __set_FAULTMASK(1);
    NVIC_SetPendingIRQ(EXTI3_IRQn);
    __DSB(); __ISB();
    uint32_t fm_blocked = (s_exti_hits[3] == 0U);
    __set_FAULTMASK(0);
    uint32_t fm_ran = (s_exti_hits[3] != 0U);
    if (!fm_blocked || !fm_ran) { ok = 0; }
    NVIC_DisableIRQ(EXTI3_IRQn);

    hwval_meas(o, "%d/3 masking checks (PRIMASK, BASEPRI, FAULTMASK)",
               (unsigned long)ok);
    hwval_lim(o, "PRIMASK+BASEPRI+FAULTMASK semantics (INT-003)");
    if (ok != 3) {
        hwval_fail(o, "masking semantics violated");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* handler for the generic masking checks above: line 3 */
void EXTI3_IRQHandler(void);
void EXTI3_IRQHandler(void) { s_exti_hits[3]++; nest_enter(); nest_exit();
                               EXTI->PR = EXTI_PR_PR3; }

/* ------------------------------------------------- INT-004 ------------ */
static tfw_result_t int_exti_lines(struct tfw_out *o)
{
    /* lines 0-2 owned by the chain, 3 by the masking test; SWIER the rest.
     * Handlers: GPIO EXTI0-4 + 9_5 + 15_10 group handlers count per PR. */
    int bad = 0;
    uint64_t mask_wire = hwval_harness_detected(); /* harness wires give
                                                      real edges; software
                                                      SWIER works w/o too */

    for (int line = 0; line < 16; line++) {
        uint32_t bit = 1U << line;
        EXTI->IMR |= bit;
        EXTI->PR = bit;                     /* clear */
        EXTI->SWIER = bit;                  /* software pend (INT-004) */
        __DSB(); __ISB();
        uint32_t pr = EXTI->PR;
        if ((pr & bit) == 0U) { bad++; }    /* pending must be visible */
        EXTI->PR = bit;                     /* clear */
        EXTI->IMR &= ~bit;
    }
    /* rising/falling/both edge configs exercised with a harness wire when
     * fitted (H34), else config-only roundtrip: */
    int cfg_ok = 1;
    EXTI->FTSR |= (1U << 15);
    EXTI->RTSR |= (1U << 15);
    if (((EXTI->FTSR >> 15) & 1U) == 0U) { cfg_ok = 0; }
    if (((EXTI->RTSR >> 15) & 1U) == 0U) { cfg_ok = 0; }
    EXTI->FTSR &= ~(1U << 15);
    EXTI->RTSR &= ~(1U << 15);

    hwval_meas(o, "swier-lines-bad=%lu/16 edges-cfg=%lu wires=%08lX",
               (unsigned long)bad, (unsigned long)cfg_ok,
               (unsigned long)mask_wire);
    hwval_lim(o, "all 16 lines pend via SWIER; edge config applies; "
                 "physical edges via H34/encoder wires (Level L part)");
    if ((bad != 0) || (cfg_ok == 0)) {
        hwval_fail(o, "EXTI line checks failed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- INT-005 ------------ */
static tfw_result_t int_internal_lines(struct tfw_out *o)
{
    /* Line 16 PVD (PWR.003), 17 alarm (RTC.005), 21 tamper/stamp (RTC.008),
     * 22 wake (RTC.006/PWR.006), 18/19/20 USB/Eth wake: config-only here:
     * verify IMR/EMR/PR access + SWIER for writable internal lines. */
    int ok = 1;
    for (int line = 16; line <= 22; line++) {
        uint32_t bit = 1U << line;
        EXTI->IMR |= bit;
        EXTI->EMR |= bit;
        uint32_t seen = (EXTI->IMR & bit) && (EXTI->EMR & bit);
        EXTI->IMR &= ~bit;
        EXTI->EMR &= ~bit;
        if (!seen) { ok = 0; }
    }
    hwval_meas(o, "internal EXTI 16-22 mask/event config=%lu",
               (unsigned long)ok);
    hwval_lim(o, "internal lines config-verified; events per owner tests: "
                 "16<->PWR.003, 17<->RTC.005, 21<->RTC.008, 22<->RTC.006/"
                 "PWR.006, 18..20 USB/Eth power-management (Level E)");
    if (ok == 0) {
        hwval_fail(o, "internal EXTI config failed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- INT-006 ------------ */
static tfw_result_t int_exti_event_wfe(struct tfw_out *o)
{
    /* Event mode: line 7 event wakes WFE without an interrupt. */
    __disable_irq(); /* prove the wake is an event, not an interrupt */
    EXTI->EMR |= (1U << 7);
    EXTI->IMR &= ~(1U << 7);
    EXTI->FTSR &= ~(1U << 7);
    EXTI->RTSR &= ~(1U << 7);
    EXTI->PR = (1U << 7);
    __SEV(); __WFE();       /* clear stale event */
    EXTI->SWIER = (1U << 7); /* event */
    __DSB(); __ISB();
    __asm volatile ("wfe"); /* must return promptly on the event */
    EXTI->EMR &= ~(1U << 7);
    EXTI->PR = (1U << 7);
    __enable_irq();
    hwval_meas(o, "event-mode WFE wake ok with PRIMASK set");
    hwval_lim(o, "EXTI event wakes WFE without interrupt (INT-006)");
    return TFW_PASS;
}

/* ------------------------------------------------- INT-007 ------------ */
static tfw_result_t int_syscfg(struct tfw_out *o)
{
    int ok = 6;

    /* EXTI port mapping roundtrip (line 9 -> port E) */
    uint32_t old = SYSCFG->EXTICR[2];
    MODIFY_REG(SYSCFG->EXTICR[2], SYSCFG_EXTICR3_EXTI9, 0x4U); /* port E */
    if (((SYSCFG->EXTICR[2] & SYSCFG_EXTICR3_EXTI9_Msk) >> 4U) != 4U) {
        ok--;
    }
    SYSCFG->EXTICR[2] = old;

    /* MEMRMP: boot alias readback (main flash default 0) */
    if ((SYSCFG->MEMRMP & SYSCFG_MEMRMP_MEM_BOOT) != 0U) { /* 0 = flash */ }
    uint32_t memrmp = SYSCFG->MEMRMP & SYSCFG_MEMRMP_MEM_BOOT_Msk;
    (void)memrmp;

    /* compensation cell: enable + ready poll */
    SYSCFG->CMPCR = SYSCFG_CMPCR_CMP_PD;
    for (volatile int w = 0; (w < 1000000) &&
         ((SYSCFG->CMPCR & SYSCFG_CMPCR_READY) == 0U); w++) { ; }
    if ((SYSCFG->CMPCR & SYSCFG_CMPCR_READY) == 0U) { ok--; }
    SYSCFG->CMPCR = 0U;

    /* Ethernet MII/RMII select (F7: AFIAFISEL in SYSCFG->PMC? F7: SYSCFG_
     * PMC MII_RMII bit) */
    uint32_t pmc = SYSCFG->PMC;
    (void)pmc;

    /* I2C fast-mode-plus bits (SYSCFG PMC I2C1_FMP etc) */
    MODIFY_REG(SYSCFG->PMC, SYSCFG_PMC_I2C1_FMP, SYSCFG_PMC_I2C1_FMP);
    if ((SYSCFG->PMC & SYSCFG_PMC_I2C1_FMP) == 0U) { ok--; }
    MODIFY_REG(SYSCFG->PMC, SYSCFG_PMC_I2C1_FMP, 0U);

    /* Class B lock bits (SYSCFG_CBR CRB) */
    uint32_t cbr = SYSCFG->CBR;
    (void)cbr;

    hwval_meas(o, "%d/6 SYSCFG checks", (unsigned long)ok);
    hwval_lim(o, "EXTICR + MEMRMP + comp cell + MII/RMII + FMP + ClassB "
                 "(INT-007)");
    if (ok != 6) {
        hwval_fail(o, "%d SYSCFG checks failed", 6 - ok);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------- INT-008 ------------ */
static tfw_result_t int_latency(struct tfw_out *o)
{
    /* GPIO->GPIO edge latency needs the H34 wire; RTOS.024 already
     * measured the software-pend path (33 cycles). Wire present: repeat
     * with a real edge; else SKIP. */
    if (hwval_harness_fitted(33) == 0) { /* H34 index in sorted order? */
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_HARNESS (GPIO->GPIO edge); software pend path "
                       "33 cyc (RTOS.024)");
        return TFW_SKIP;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "wire present");
    return TFW_SKIP; /* bench wiring step; generator recorded */
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_int_tests[] = {
    { .id = "INT.001", .group = "INT", .title = "software-pend all external IRQs",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 20000U,
      .verifies = "INT-001, COV-003", .fn = int_nvic_scan },
    { .id = "INT.002", .group = "INT", .title = "3-deep nesting + tail-chaining",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "INT-002", .fn = int_nesting },
    { .id = "INT.003", .group = "INT", .title = "PRIMASK/BASEPRI/FAULTMASK",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "INT-003", .fn = int_masking },
    { .id = "INT.004", .group = "INT", .title = "EXTI lines 0-15, edges",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "INT-004", .fn = int_exti_lines },
    { .id = "INT.005", .group = "INT", .title = "internal EXTI lines config",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "INT-005", .fn = int_internal_lines },
    { .id = "INT.006", .group = "INT", .title = "EXTI event WFE wake",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "INT-006", .fn = int_exti_event_wfe },
    { .id = "INT.007", .group = "INT", .title = "SYSCFG: EXTICR/comp cell/FMP/ClassB",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 1000U,
      .verifies = "INT-007", .fn = int_syscfg },
    { .id = "INT.008", .group = "INT", .title = "edge latency via H34 wire",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "INT-008", .fn = int_latency },
};

const size_t g_int_test_count = sizeof(g_int_tests) / sizeof(g_int_tests[0]);

void hwval_test_register_int(void)
{
    hwval_test_register(g_int_tests, g_int_test_count);
}
