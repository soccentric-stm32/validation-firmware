/**
 * @file    mpu_setup.h
 * @brief   Default MPU configuration.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     GEN-007, SAFE-002, SAFE-004
 */

#ifndef HWVAL_MPU_SETUP_H
#define HWVAL_MPU_SETUP_H

/**
 * @brief Program the default MPU regions and enable the MPU.
 * @details Region 0: 8 KiB .noncacheable DMA block (GEN-007). Region 1: OTP
 *          page read-only at all privilege levels (SAFE-002). Region 2:
 *          sector 11 privileged RW only (SAFE-004). Region 3: 128 KiB .sram1
 *          non-cacheable (GEN-007). The background map stays on for
 *          privileged code.
 * @pre No task exists yet.
 */
void hwval_mpu_setup(void);

#endif /* HWVAL_MPU_SETUP_H */
