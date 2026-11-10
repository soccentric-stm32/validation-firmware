/*
 * board/hwval_board.h - board pins, clock and limit definitions
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  NUCLEO-F767ZI
 * SRS:     GEN-004, GEN-009, NFR-009, GPIO-001; measured values Hardware.md
 *
 * Everything board-specific lives here so the suite can be adapted to
 * another Nucleo-144 F7 board (NFR-009).
 */

#ifndef HWVAL_BOARD_H
#define HWVAL_BOARD_H

/* --- Identity (Hardware.md section 2) ------------------------------------- */
#define HWVAL_BOARD_NAME        "NUCLEO-F767ZI"
#define HWVAL_MCU_NAME          "STM32F767ZIT6"
#define HWVAL_MCU_DEV_ID        0x451U       /* DBGMCU_IDCODE, F76x/F77x    */
#define HWVAL_MCU_REV_ID        0x1001U      /* rev Z                       */
#define HWVAL_FLASH_SIZE_KB     2048U
#define HWVAL_PACKAGE           "LQFP144"

/* --- Clocks (Hardware.md section 6, CON-005) ------------------------------ */
#define HWVAL_HSE_FREQ_HZ       8000000U   /* ST-LINK MCO to PH0, bypass   */
#define HWVAL_LSE_FREQ_HZ       32768U     /* X1 fitted                    */
#define HWVAL_SYSCLK_TARGET_HZ  216000000U /* GEN-004 max for rev Z, 3.3 V */
#define HWVAL_APB1_PRESCALER    RCC_HCLK_DIV4
#define HWVAL_APB2_PRESCALER    RCC_HCLK_DIV2

/* --- Flash layout (RM0410, ASM-004) --------------------------------------- */
#define HWVAL_FLASH_BASE        0x08000000U
#define HWVAL_SECTOR11_BASE     0x081C0000U /* destructive tests only       */
#define HWVAL_FLASH_END         0x08200000U
#define HWVAL_OTP_BASE          0x1FF0F000U /* SAFE-002: MPU read-only      */

/* --- RTC backup registers ------------------------------------------------- */
/* RTC_BKP0R..BKP3R belong to the bootloader (SRS-BOOT-F767: trial state,
 * "stay in BOOT" request, hand-over record, last boot outcome) and BKP7R is
 * the link-mode flag (LINK-010).  The post-reset bootcheck (TFW-007) uses
 * BKP8R..BKP13R so a resetting test never corrupts BOOT's trial state. */
#define HWVAL_BKP_BOOT_FIRST    0U          /* BKP0R..BKP3R: BOOT only     */
#define HWVAL_BKP_BOOT_COUNT    4U
#define HWVAL_BKP_LINK_INDEX    7U          /* BKP7R: link-mode flag       */
#define HWVAL_BKP_CHECK_MAGIC   (RTC->BKP8R)  /* bootcheck armed magic     */
#define HWVAL_BKP_CHECK_HASH    (RTC->BKP9R)  /* case id hash              */
#define HWVAL_BKP_CHECK_EXPECT  (RTC->BKP10R) /* expected verdict          */
#define HWVAL_BKP_CHECK_ARG0    (RTC->BKP11R) /* per-case argument         */
#define HWVAL_BKP_CHECK_ARG1    (RTC->BKP12R) /* per-case argument         */
#define HWVAL_BKP_RESET_FLAGS   (RTC->BKP13R) /* RCC_CSR mirror at boot    */

/* --- RAM layout (RM0410, Hardware.md section 4) --------------------------- */
#define HWVAL_ITCMRAM_BASE      0x00000000U
#define HWVAL_ITCMRAM_SIZE      (16U * 1024U)
#define HWVAL_DTCMRAM_BASE      0x20000000U
#define HWVAL_DTCMRAM_SIZE      (128U * 1024U)
#define HWVAL_SRAM1_BASE        0x20020000U
#define HWVAL_SRAM1_SIZE        (368U * 1024U)
#define HWVAL_SRAM2_BASE        0x2007C000U
#define HWVAL_SRAM2_SIZE        (16U * 1024U)
#define HWVAL_BKPSRAM_BASE      0x40024000U
#define HWVAL_BKPSRAM_SIZE      (4U * 1024U)

/* --- On-board user I/O (Hardware.md section 9) ---------------------------- */
#define HWVAL_LED1_PORT         GPIOB      /* LD1 green, high = on         */
#define HWVAL_LED1_PIN          GPIO_PIN_0
#define HWVAL_LED2_PORT         GPIOB      /* LD2 blue                     */
#define HWVAL_LED2_PIN          GPIO_PIN_7
#define HWVAL_LED3_PORT         GPIOB      /* LD3 red                      */
#define HWVAL_LED3_PIN          GPIO_PIN_14
#define HWVAL_BTN1_PORT         GPIOC      /* B1, high when pressed        */
#define HWVAL_BTN1_PIN          GPIO_PIN_13

/* Reserved pins: never reconfigured by any code path (CON-005). */
#define HWVAL_PIN_PA13_SWDIO    1
#define HWVAL_PIN_PA14_SWCLK    1
#define HWVAL_PIN_PA15_SWO      0          /* PB3 is SWO on ST-LINK; PA15 free */
#define HWVAL_PIN_PH0_HSE       1
#define HWVAL_PIN_PH1_HSE       1

/* --- VCP console (Hardware.md section 2) ----------------------------------- */
#define HWVAL_VCP_BAUD          115200U

/* --- Watchdog (GEN-009) -------------------------------------------------- */
#define HWVAL_IWDG_TIMEOUT_MS   4000U

#endif /* HWVAL_BOARD_H */