/**
 * @file    hwval_board.h
 * @brief   Board identity, clock, memory map, pin and limit definitions.
 * @details Everything board-specific lives here so the suite can be adapted to
 *          another Nucleo-144 F7 board (NFR-009). Measured values come from
 *          Hardware.md.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @target  NUCLEO-F767ZI
 * @srs     GEN-004, GEN-009, NFR-009, GPIO-001
 */

#ifndef HWVAL_BOARD_H
#define HWVAL_BOARD_H

/**
 * @defgroup hwval_board Board definitions
 * @brief Board identity, clocks, memory map and on-board I/O.
 * @{
 */

/* --- Identity (Hardware.md section 2) ------------------------------------- */
#define HWVAL_BOARD_NAME        "NUCLEO-F767ZI" /**< Board name for the banner. */
#define HWVAL_MCU_NAME          "STM32F767ZIT6" /**< MCU part number. */
#define HWVAL_MCU_DEV_ID        0x451U       /**< DBGMCU_IDCODE DEV_ID, F76x/F77x. */
#define HWVAL_MCU_REV_ID        0x1001U      /**< DBGMCU_IDCODE REV_ID, rev Z. */
#define HWVAL_FLASH_SIZE_KB     2048U        /**< Flash size in KiB. */
#define HWVAL_PACKAGE           "LQFP144"    /**< Package name. */

/* --- Clocks (Hardware.md section 6, CON-005) ------------------------------ */
#define HWVAL_HSE_FREQ_HZ       8000000U   /**< HSE bypass from ST-LINK MCO on PH0, Hz. */
#define HWVAL_LSE_FREQ_HZ       32768U     /**< LSE crystal X1, Hz. */
#define HWVAL_SYSCLK_TARGET_HZ  216000000U /**< SYSCLK target, Hz (GEN-004, rev Z, 3.3 V). */
#define HWVAL_APB1_PRESCALER    RCC_HCLK_DIV4 /**< APB1 = HCLK / 4. */
#define HWVAL_APB2_PRESCALER    RCC_HCLK_DIV2 /**< APB2 = HCLK / 2. */

/* --- Flash layout (RM0410, ASM-004) --------------------------------------- */
#define HWVAL_FLASH_BASE        0x08000000U /**< Flash base (AXIM). */
#define HWVAL_SECTOR11_BASE     0x081C0000U /**< Sector 11; destructive tests only. */
#define HWVAL_FLASH_END         0x08200000U /**< First address past flash. */
#define HWVAL_OTP_BASE          0x1FF0F000U /**< OTP area; MPU read-only (SAFE-002). */

/* --- RTC backup registers ------------------------------------------------- */
/* RTC_BKP0R..BKP3R belong to the bootloader (SRS-BOOT-F767: trial state,
 * "stay in BOOT" request, hand-over record, last boot outcome) and BKP7R is
 * the link-mode flag (LINK-010). The post-reset bootcheck (TFW-007) uses
 * BKP8R..BKP13R so a resetting test never corrupts BOOT's trial state. */
#define HWVAL_BKP_BOOT_FIRST    0U          /**< First bootloader-owned BKPxR. */
#define HWVAL_BKP_BOOT_COUNT    4U          /**< Bootloader-owned BKPxR count. */
#define HWVAL_BKP_LINK_INDEX    7U          /**< BKP7R: link-mode flag. */
#define HWVAL_BKP_CHECK_MAGIC   (RTC->BKP8R)  /**< Bootcheck armed magic. */
#define HWVAL_BKP_CHECK_HASH    (RTC->BKP9R)  /**< Bootcheck case id hash. */
#define HWVAL_BKP_CHECK_EXPECT  (RTC->BKP10R) /**< Bootcheck expected verdict. */
#define HWVAL_BKP_CHECK_ARG0    (RTC->BKP11R) /**< Bootcheck per-case argument 0. */
#define HWVAL_BKP_CHECK_ARG1    (RTC->BKP12R) /**< Bootcheck per-case argument 1. */
#define HWVAL_BKP_RESET_FLAGS   (RTC->BKP13R) /**< RCC_CSR mirror taken at boot. */

/* --- RAM layout (RM0410, Hardware.md section 4) --------------------------- */
#define HWVAL_ITCMRAM_BASE      0x00000000U    /**< ITCM RAM base. */
#define HWVAL_ITCMRAM_SIZE      (16U * 1024U)  /**< ITCM RAM size, bytes. */
#define HWVAL_DTCMRAM_BASE      0x20000000U    /**< DTCM RAM base. */
#define HWVAL_DTCMRAM_SIZE      (128U * 1024U) /**< DTCM RAM size, bytes. */
#define HWVAL_SRAM1_BASE        0x20020000U    /**< SRAM1 base. */
#define HWVAL_SRAM1_SIZE        (368U * 1024U) /**< SRAM1 size, bytes. */
#define HWVAL_SRAM2_BASE        0x2007C000U    /**< SRAM2 base. */
#define HWVAL_SRAM2_SIZE        (16U * 1024U)  /**< SRAM2 size, bytes. */
#define HWVAL_BKPSRAM_BASE      0x40024000U    /**< Backup SRAM base. */
#define HWVAL_BKPSRAM_SIZE      (4U * 1024U)   /**< Backup SRAM size, bytes. */

/* --- On-board user I/O (Hardware.md section 9) ---------------------------- */
#define HWVAL_LED1_PORT         GPIOB      /**< LD1 green port; high = on. */
#define HWVAL_LED1_PIN          GPIO_PIN_0 /**< LD1 pin (PB0). */
#define HWVAL_LED2_PORT         GPIOB      /**< LD2 blue port. */
#define HWVAL_LED2_PIN          GPIO_PIN_7 /**< LD2 pin (PB7). */
#define HWVAL_LED3_PORT         GPIOB      /**< LD3 red port. */
#define HWVAL_LED3_PIN          GPIO_PIN_14 /**< LD3 pin (PB14). */
#define HWVAL_BTN1_PORT         GPIOC      /**< B1 port; high when pressed. */
#define HWVAL_BTN1_PIN          GPIO_PIN_13 /**< B1 pin (PC13). */

/* Reserved pins: never reconfigured by any code path (CON-005). */
#define HWVAL_PIN_PA13_SWDIO    1 /**< PA13 SWDIO reserved. */
#define HWVAL_PIN_PA14_SWCLK    1 /**< PA14 SWCLK reserved. */
#define HWVAL_PIN_PA15_SWO      0 /**< PA15 free; SWO is PB3 on ST-LINK. */
#define HWVAL_PIN_PH0_HSE       1 /**< PH0 HSE input reserved. */
#define HWVAL_PIN_PH1_HSE       1 /**< PH1 HSE reserved. */

/* --- VCP console (Hardware.md section 2) ----------------------------------- */
#define HWVAL_VCP_BAUD          115200U /**< ST-LINK VCP (USART3) baud rate. */

/* --- Watchdog (GEN-009) -------------------------------------------------- */
#define HWVAL_IWDG_TIMEOUT_MS   4000U /**< IWDG timeout, ms. */

/** @} */

#endif /* HWVAL_BOARD_H */
