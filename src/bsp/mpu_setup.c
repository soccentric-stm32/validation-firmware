/*
 * mpu_setup.c - default MPU regions (GEN-007, SAFE-001..SAFE-004)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  Cortex-M7 MPU, 8 regions, 32-byte granularity
 * SRS:     GEN-007 (DMA buffers non-cacheable), SAFE-002 (OTP read-only),
 *          SAFE-004 (sector 11 restricted), MPU-00x self-tests in 8.3.
 */

#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_sections.h"
#include "bsp/mpu_setup.h"

/* Symbols exported by the linker script. */
extern uint32_t __noncacheable_start;
extern uint32_t __noncacheable_end;
extern uint32_t __sram1_start;
extern uint32_t __sram1_end;

void hwval_mpu_setup(void)
{
    MPU_Region_InitTypeDef r;

    HAL_MPU_Disable();

    /* Region 0: .noncacheable in SRAM2 tail — normal memory, TEX=1 (non-
     * cacheable), shareable, full access, XN. Buffers placed with
     * __attribute__((section(".noncacheable"))) are safe for DMA. */
    r.Number             = MPU_REGION_NUMBER0;
    r.BaseAddress        = (uint32_t)&__noncacheable_start;
    r.Size               = MPU_REGION_SIZE_8KB;
    r.Enable             = MPU_REGION_ENABLE;
    r.AccessPermission   = MPU_REGION_FULL_ACCESS;
    r.IsBufferable       = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsCacheable        = MPU_ACCESS_NOT_CACHEABLE;
    r.IsShareable        = MPU_ACCESS_SHAREABLE;
    r.SubRegionDisable = 0x00U;
    r.TypeExtField       = MPU_TEX_LEVEL1;
    r.DisableExec        = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&r);

    /* Region 3: .sram1 at the SRAM1 base (128 KB) — NFR-004 put the bulk
     * DMA-fed test buffers here (out of DTCM); the region is non-cacheable
     * so CPU fills are directly visible to the DMA masters (same pattern
     * as region 0, GEN-007, and to be kept clear of REGION7 test use). */
    r.Number             = MPU_REGION_NUMBER3;
    r.BaseAddress        = (uint32_t)&__sram1_start;
    r.Size               = MPU_REGION_SIZE_128KB;
    r.Enable             = MPU_REGION_ENABLE;
    r.AccessPermission   = MPU_REGION_FULL_ACCESS;
    r.IsBufferable       = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsCacheable        = MPU_ACCESS_NOT_CACHEABLE;
    r.IsShareable        = MPU_ACCESS_SHAREABLE;
    r.SubRegionDisable = 0x00U;
    r.TypeExtField       = MPU_TEX_LEVEL1;
    r.DisableExec        = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&r);

    /* Region 1: OTP + calibration constants (0x1FF0F000, 4 KB) — read-only
     * for every privilege level, XN (SAFE-002; DEV-006 reads stay legal). */
    r.Number             = MPU_REGION_NUMBER1;
    r.BaseAddress        = HWVAL_OTP_BASE;
    r.Size               = MPU_REGION_SIZE_4KB;
    r.Enable             = MPU_REGION_ENABLE;
    r.AccessPermission   = MPU_REGION_PRIV_RO_URO;
    r.IsBufferable       = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsCacheable        = MPU_ACCESS_CACHEABLE;
    r.IsShareable        = MPU_ACCESS_NOT_SHAREABLE;
    r.TypeExtField       = MPU_TEX_LEVEL0;
    r.DisableExec        = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&r);

    /* Region 2: flash sector 11 (256 KB) — privileged RW only, XN.
     * Unprivileged tasks cannot touch the destructive-test area; the flash
     * wrapper still checks every address (SAFE-004). */
    r.Number             = MPU_REGION_NUMBER2;
    r.BaseAddress        = HWVAL_SECTOR11_BASE;
    r.Size               = MPU_REGION_SIZE_256KB;
    r.Enable             = MPU_REGION_ENABLE;
    r.AccessPermission   = MPU_REGION_PRIV_RW;
    r.IsBufferable       = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsCacheable        = MPU_ACCESS_CACHEABLE;
    r.IsShareable        = MPU_ACCESS_NOT_SHAREABLE;
    r.TypeExtField       = MPU_TEX_LEVEL0;
    r.DisableExec        = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&r);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}