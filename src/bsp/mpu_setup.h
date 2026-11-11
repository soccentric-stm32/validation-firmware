/*
 * mpu_setup.h - default MPU configuration (GEN-007, SAFE-002)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 */

#ifndef HWVAL_MPU_SETUP_H
#define HWVAL_MPU_SETUP_H

/* Regions programmed before any task exists:
 *  - 8 KiB non-cacheable block for DMA buffers (GEN-007, .noncacheable);
 *  - OTP/system-info page read-only for ALL privilege levels (SAFE-002);
 *  - flash sector 11 privileged-only RW so destructive flash tests can use
 *    it but no user task can (SAFE-004);
 *  - background map active for privileged mode (PrivilegedDefault). */
void hwval_mpu_setup(void);

#endif /* HWVAL_MPU_SETUP_H */