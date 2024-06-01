/**
 * @file    flash_ops.h
 * @brief   Project-owned flash access wrapper restricted to sector 11.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     SAFE-004 (destructive flash limited to sector 11 by an address
 *          check), SAFE-011 (nDBANK boot check), FLS-005..006, FLS-008
 *          (interrupts), FLS-009
 */

#ifndef HWVAL_FLASH_OPS_H
#define HWVAL_FLASH_OPS_H

#include <stdint.h>

/**
 * @defgroup bsp_flash Flash wrapper
 * @brief Sector-11-only erase/program, flash IRQ counters and bank info.
 * @{
 */

/**
 * @brief Check that a range lies inside sector 11 (0x081C0000..0x081FFFFF).
 * @param[in] addr Start address.
 * @param[in] len  Length, bytes.
 * @return Non-zero if inside, else 0.
 */
int  hwval_flash_range_in_sector11(uint32_t addr, uint32_t len);

/**
 * @brief Erase sector 11 with EOP/error interrupts enabled.
 * @details Blocks until done; invalidates the D-cache over the sector.
 * @retval 0  Erased.
 * @retval -2 Unlock failed.
 * @retval -3 Timeout.
 * @retval -4 Flash error flag set.
 */
int  hwval_flash_erase_sector11(void);

/**
 * @brief Program 32-bit words into sector 11.
 * @param[in] addr  Destination address; whole range must be in sector 11.
 * @param[in] data  Source words.
 * @param[in] words Number of words.
 * @retval 0  Programmed.
 * @retval -1 Range outside sector 11 (SAFE-004).
 * @retval -2 Unlock failed.
 * @retval -3 Timeout.
 * @retval -4 Flash error flag set.
 */
int  hwval_flash_program_sector11(uint32_t addr, const uint32_t *data,
                                  uint32_t words);

/**
 * @brief Attempt one word program without an address check (FLS-006).
 * @details Used to show that the controller refuses a protected address;
 *          resets the error counter and clears error flags afterwards.
 * @param[in] addr  Target address.
 * @param[in] value Word to write.
 * @retval 0  Controller finished (accepted or refused).
 * @retval -3 Timeout.
 * @warning Bypasses the SAFE-004 check; pass only addresses expected to fail.
 */
int  hwval_flash_program_refuse(uint32_t addr, uint32_t value);

/**
 * @brief Read the flash IRQ counters (FLS-008).
 * @param[out] eop End-of-operation interrupts.
 * @param[out] err Error interrupts.
 */
void hwval_flash_count_get(uint32_t *eop, uint32_t *err);

/**
 * @brief Report the flash bank mode (SAFE-011).
 * @return 0 for single-bank (nDBANK set), 1 for dual-bank.
 */
int  hwval_flash_dual_bank(void);

/**
 * @brief Read the FLASHSIZE register at 0x1FF0F442 (DEV-002/DEV-003).
 * @return Flash size, KB.
 */
uint32_t hwval_flash_size_kb(void);

/** @brief Print a console warning when the flash is in dual-bank mode. */
void hwval_flash_bank_report(void);

/**
 * @brief Flash IRQ: count EOP and errors, clear the flags (FLS-008).
 * @note Overrides the HAL handler; hal_flash_irq.c is not compiled in.
 */
void FLASH_IRQHandler(void);

/** @} */

#endif /* HWVAL_FLASH_OPS_H */
