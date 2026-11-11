/*
 * flash_ops.h - project-owned flash access wrapper (SAFE-004)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     SAFE-004 (destructive flash limited to sector 11, enforced by an
 *          address check), SAFE-011 (nDBANK boot check),
 *          address check), FLS-005..006, FLS-008 (interrupts), FLS-009.
 */

#ifndef HWVAL_FLASH_OPS_H
#define HWVAL_FLASH_OPS_H

#include <stdint.h>

/* Destructive operations are rejected unless the whole range is inside
 * flash sector 11 (0x081C0000..0x081FFFFF, ASM-004). */
int  hwval_flash_range_in_sector11(uint32_t addr, uint32_t len);
int  hwval_flash_erase_sector11(void);               /* returns 0 ok */
int  hwval_flash_program_sector11(uint32_t addr, const uint32_t *data,
                                  uint32_t words);   /* 0 ok */
int  hwval_flash_program_refuse(uint32_t addr, uint32_t value); /* FLS-006 */
void hwval_flash_count_get(uint32_t *eop, uint32_t *err);

/* SAFE-011: 0 = single-bank flash (nDBANK set), 1 = dual-bank mode. */
int  hwval_flash_dual_bank(void);

/* DEV-002/DEV-003: the 16-bit FLASHSIZE register at 0x1FF0F442, in KB. */
uint32_t hwval_flash_size_kb(void);
void hwval_flash_bank_report(void);

/* IRQ handler override (hal_flash_irq.c is not compiled in). */
void FLASH_IRQHandler(void);

#endif /* HWVAL_FLASH_OPS_H */