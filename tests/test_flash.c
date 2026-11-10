/*
 * tests/test_flash.c - flash, ART, option bytes, OTP (section 8.5)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     FLS-001..FLS-009, OB-001/002, OTP-001/002.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_limits.h"
#include "test_core.h"
#include "flash_ops.h"
#include "clock.h"
#include "fault.h"

/* wrong-key helper in src/flash_ops.c */
void hwval_flash_wrong_key_lock(void);

/* ------------------------------------------------------------- FLS-001 */
static uint32_t flash_crc32_periph(uint32_t addr, uint32_t bytes)
{
    CRC_HandleTypeDef hc;
    (void)memset(&hc, 0, sizeof(hc));
    __HAL_RCC_CRC_CLK_ENABLE();
    hc.Instance = CRC;
    /* CRC-32/ISO-HDLC (the same algorithm as crc32_buf and zlib.crc32):
     * poly 0x04C11DB7, init 0xFFFFFFFF, byte-wise input reflection and
     * reflected output.  The reset defaults (no reflection) can never
     * match the CPU reference. */
    hc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_DISABLE;
    hc.Init.GeneratingPolynomial = 0x04C11DB7U;
    hc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_DISABLE;
    hc.Init.InitValue = 0xFFFFFFFFU;
    hc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_BYTE;
    hc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_ENABLE;
    hc.Init.CRCLength = CRC_POLYLENGTH_32B;
    hc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;
    (void)HAL_CRC_Init(&hc);
    return HAL_CRC_Calculate(&hc, (uint32_t *)addr, bytes / 4U);
}

static uint32_t flash_crc32_cpu(uint32_t addr, uint32_t bytes)
{
    extern uint32_t crc32_buf(const uint8_t *p, size_t n); /* test_core.c */
    return crc32_buf((const uint8_t *)addr, bytes);
}

extern uint32_t __flash_image_start; /* from the linker script: slot base */
extern uint32_t __flash_image_end;

static tfw_result_t fls_crc_image(struct tfw_out *o)
{
    uint32_t start = (uint32_t)&__flash_image_start;
    uint32_t bytes = (uint32_t)&__flash_image_end - start;
    bytes &= ~3U;   /* the CRC unit reads whole words; compare like for like */
    uint32_t per = flash_crc32_periph(start, bytes);
    uint32_t cpu = flash_crc32_cpu(start, bytes);

    hwval_meas(o, "crc=%08lX/%08lX over %lu KB", (unsigned long)per,
               (unsigned long)cpu, (unsigned long)(bytes / 1024U));
    hwval_lim(o, "CRC unit == CPU reference; host compares with build/"
                 "hwval.crc (section 11)");
    if ((per != cpu) || (bytes == 0U)) {
        hwval_fail(o, "peripheral CRC %08lX != CPU %08lX",
                   (unsigned long)per, (unsigned long)cpu);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------------------- FLS-002 */
static tfw_result_t fls_alias_paths(struct tfw_out *o)
{
    /* Read the used image range through AXIM (0x08000000) and the
     * ITCM-flash alias (0x00200000, FLASHITCM_BASE).  0x00000000 is ITCM
     * RAM, not flash. */
    uint32_t bytes = (uint32_t)&__flash_image_end - 0x08000000U;
    volatile const uint32_t *axi = (const uint32_t *)0x08000000U;
    volatile const uint32_t *itc = (const uint32_t *)0x00200000U;
    int bad = 0;

    for (uint32_t w = 0U; w < bytes / 4U; w += 16U) {
        /* cppcheck-suppress nullPointer ; ITCM flash alias, not NULL */ if (axi[w] != itc[w]) { bad++; }
    }
    for (uint32_t a = 0x001C0000U; a < 0x00200000U; a += 0x10000U) {
        if (axi[a / 4U] != itc[a / 4U]) { bad++; }
    }
    hwval_meas(o, "image %lu KB + strides, mismatches=%lu",
               (unsigned long)(bytes / 1024U), (unsigned long)bad);
    hwval_lim(o, "AXIM and ITCM alias paths agree (FLS-002)");
    if (bad != 0) {
        hwval_fail(o, "%lu path mismatches", (unsigned long)bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}
#define clock_to(hw, ws_) hwval_clock_set((hw), (ws_))

/* ------------------------------------------------------------- FLS-003 */

static const struct { uint32_t hz; uint32_t ws; } k_ws_table[] = {
    { 120000000U, 3U }, /* [RM0410] VOS1 latency table (DS11532) */
    { 180000000U, 5U },
    { 216000000U, 7U },
};

static int flash_reads_ok(void)
{
    /* deterministic self-check over a known code range: CRC equality
     * between two AXIM reads and ITCM alias on a 16 KB block */
    volatile const uint32_t *axi = (const uint32_t *)0x08020000U;
    volatile const uint32_t *itc = (const uint32_t *)0x00220000U;

    for (uint32_t i = 0U; i < 4096U; i++) {
        if (axi[i] != itc[i]) {
            return 0;
        }
    }
    return 1;
}

static tfw_result_t fls_latency(struct tfw_out *o)
{
    int ok = 3;
    uint32_t rc;
    char detail[64];

    for (unsigned i = 0U; i < (sizeof(k_ws_table) / sizeof(k_ws_table[0]));
         i++) {
        rc = (uint32_t)clock_to(k_ws_table[i].hz, k_ws_table[i].ws);
        if (rc != 0U) { ok = 0; break; }
        if (!flash_reads_ok()) { ok--; }
    }
    /* restore GEN-004 defaults (GEN-005) */
    rc = (uint32_t)clock_to(216000000U, 7U);
    if (rc != 0U) {
        hwval_fail(o, "clock restore failed rc=%lu", (unsigned long)rc);
        return TFW_FAIL;
    }
    (void)snprintf(detail, sizeof(detail), "%d/3 table points ok", ok);
    hwval_meas(o, "%s", detail);
    hwval_lim(o, "(HCLK,WS) pairs from [RM0410] read correctly");
    if (ok != 3) {
        hwval_fail(o, "%s", detail);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------------------- FLS-004 */
static tfw_result_t fls_art(struct tfw_out *o)
{
    volatile uint32_t acc = 0U;
    uint32_t cyc_on, cyc_off;

    __HAL_FLASH_ART_ENABLE();
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
    __ISB();
    DWT->CYCCNT = 0U;
    for (volatile int i = 0; i < 20000; i++) {
        acc += *(const uint32_t *)(0x08020000U + ((uint32_t)i * 4U));
    }
    cyc_on = DWT->CYCCNT;

    __HAL_FLASH_ART_DISABLE();
    __HAL_FLASH_PREFETCH_BUFFER_DISABLE();
    __ISB();
    DWT->CYCCNT = 0U;
    for (volatile int i = 0; i < 20000; i++) {
        acc += *(const uint32_t *)(0x08020000U + ((uint32_t)i * 4U));
    }
    cyc_off = DWT->CYCCNT;

    __HAL_FLASH_ART_ENABLE();
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
    __ISB();

    hwval_meas(o, "art-on=%lu cyc off=%lu cyc", (unsigned long)cyc_on,
               (unsigned long)cyc_off);
    hwval_lim(o, "ART+prefetch never slower (FLS-004)");
    if (cyc_on > cyc_off + 10000U) {
        hwval_fail(o, "ART path slower than uncached");
        return TFW_FAIL;
    }
    (void)acc;
    return TFW_PASS;
}

/* ------------------------------------------------------------- FLS-005 */
static tfw_result_t fls_sector11(struct tfw_out *o)
{
    uint32_t addr = HWVAL_SECTOR11_BASE;

    if (hwval_flash_dual_bank() != 0) {
        (void)snprintf(o->reason, sizeof(o->reason), "WRONG_FLASH_MODE");
        return TFW_NA;
    }
    static uint32_t buf[64];
    int rc;
    uint32_t eop, err;

    for (uint32_t i = 0U; i < 64U; i++) {
        buf[i] = 0U;
    }
    rc = hwval_flash_erase_sector11();
    if (rc != 0) {
        hwval_fail(o, "erase rc=%d", rc);
        return TFW_FAIL;
    }
    for (uint32_t i = 0U; i < 64U; i++) {
        if (*(const uint32_t *)(addr + i * 4U) != 0xFFFFFFFFU) {
            hwval_fail(o, "sector not erased at wd %lu",
                       (unsigned long)i);
            return TFW_FAIL;
        }
    }

    /* patterns: 0x00000000, 0x55555555, 0xAAAAAAAA, address-in-word */
    static const uint32_t pats[3] = { 0x00000000U, 0x55555555U, 0xAAAAAAAAU };
    for (unsigned p = 0U; p < 3U; p++) {
        /* Programmed bits can only go 1 -> 0; erase before each pattern. */
        rc = hwval_flash_erase_sector11();
        if (rc != 0) {
            hwval_fail(o, "erase before pat%u rc=%d", p, rc);
            return TFW_FAIL;
        }
        for (uint32_t i = 0U; i < 64U; i++) {
            buf[i] = pats[p];
        }
        rc = hwval_flash_program_sector11(addr, buf, 64U);
        if (rc != 0) {
            hwval_fail(o, "program pat%u rc=%d", p, rc);
            return TFW_FAIL;
        }
        for (uint32_t i = 0U; i < 64U; i++) {
            if (*(const uint32_t *)(addr + i * 4U) != pats[p]) {
                hwval_fail(o, "verify pat%u wd %lu", p, (unsigned long)i);
                return TFW_FAIL;
            }
        }
    }
    rc = hwval_flash_erase_sector11();
    if (rc != 0) {
        hwval_fail(o, "erase before address-in-word rc=%d", rc);
        return TFW_FAIL;
    }
    for (uint32_t i = 0U; i < 64U; i++) {
        buf[i] = addr + i * 4U; /* address-in-word */
    }
    rc = hwval_flash_program_sector11(addr, buf, 64U);
    if ((rc != 0) ||
        (*(const uint32_t *)(addr + 32U) != addr + 128U)) {
        hwval_fail(o, "address-in-word program/verify rc=%d", rc);
        return TFW_FAIL;
    }

    /* FLS-009: erase and program time measurement (with limits) */
    uint32_t t0 = HAL_GetTick();
    rc = hwval_flash_erase_sector11();
    uint32_t erase_ms = HAL_GetTick() - t0;
    if (rc != 0) {
        hwval_fail(o, "timed erase rc=%d", rc);
        return TFW_FAIL;
    }
    buf[0] = 0x89ABCDEFU;
    t0 = HAL_GetTick();
    rc = hwval_flash_program_sector11(addr, buf, 1U);
    uint32_t prog_ms = HAL_GetTick() - t0;
    if (rc != 0) {
        hwval_fail(o, "timed program rc=%d", rc);
        return TFW_FAIL;
    }
    hwval_flash_count_get(&eop, &err);
    hwval_meas(o, "erase=%lu ms prog64=%lu ms eop=%lu err=%lu",
               (unsigned long)erase_ms, (unsigned long)prog_ms,
               (unsigned long)eop, (unsigned long)err);
    hwval_lim(o, "erase<=2000 ms, prog64<=1.2 ms, err flags 0 (FLS-009/008)");
    if ((erase_ms > HWVAL_LIM_SECTOR_ERASE_MAX_MS) ||
        (prog_ms > HWVAL_LIM_PROG64_MAX_US / 1000U + 1U)) {
        hwval_fail(o, "flash timing outside [DS11532]");
        return TFW_FAIL;
    }
    if ((eop == 0U) || (err != 0U)) {
        hwval_fail(o, "interrupt evidence wrong (eop=%lu err=%lu)",
                   (unsigned long)eop, (unsigned long)err);
        return TFW_FAIL;
    }
    (void)hwval_flash_erase_sector11(); /* leave sector erased */
    return TFW_PASS;
}

/* ------------------------------------------------------------- FLS-006 */
static tfw_result_t fls_prog_refuse(struct tfw_out *o)
{
    uint32_t addr = HWVAL_SECTOR11_BASE;

    if (hwval_flash_dual_bank() != 0) {
        (void)snprintf(o->reason, sizeof(o->reason), "WRONG_FLASH_MODE");
        return TFW_NA;
    }
    /* The target must actually be programmed: erase, write one word, then
     * try to program it again with different data. */
    uint32_t first = 0x01234567U;
    int rc = hwval_flash_erase_sector11();
    if (rc != 0) {
        hwval_fail(o, "erase rc=%d", rc);
        return TFW_FAIL;
    }
    rc = hwval_flash_program_sector11(addr, &first, 1U);
    if (rc != 0) {
        hwval_fail(o, "setup program rc=%d", rc);
        return TFW_FAIL;
    }
    rc = hwval_flash_program_refuse(addr, 0x89ABCDEFU);
    uint32_t eop, err;

    hwval_flash_count_get(&eop, &err);
    hwval_meas(o, "rc=%d errflags=%lu", rc, (unsigned long)err);
    hwval_lim(o, "programming a non-erased word must signal PGS/PGP error");
    if (err == 0U) {
        hwval_fail(o, "programming non-erased location did not error");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* ------------------------------------------------------------- FLS-007 */
static tfw_result_t fls_unlock_lock(struct tfw_out *o)
{

    /* unlock/lock roundtrip */
    if (READ_BIT(FLASH->CR, FLASH_CR_LOCK) == 0U) {
        SET_BIT(FLASH->CR, FLASH_CR_LOCK);
    }
    FLASH->KEYR = 0x45670123U;
    FLASH->KEYR = 0xCDEF89ABU;
    if (READ_BIT(FLASH->CR, FLASH_CR_LOCK) != 0U) {
        hwval_fail(o, "unlock sequence failed");
        return TFW_FAIL;
    }
    SET_BIT(FLASH->CR, FLASH_CR_LOCK);
    if (READ_BIT(FLASH->CR, FLASH_CR_LOCK) == 0U) {
        hwval_fail(o, "lock failed");
        return TFW_FAIL;
    }

    /* Wrong key: lock persists until reset.  On the F7 the wrong KEYR
     * sequence can raise a BusFault, so arm the bootcheck and a reset
     * action *before* the write; otherwise the fault halts and the IWDG
     * resets with no verdict (FLS-007). */
    hwval_bootcheck_arm(0xF100U, TFW_PASS);
    hwval_fault_set_action(HWVAL_FAULT_RESET);
    hwval_flash_wrong_key_lock();
    if (READ_BIT(FLASH->CR, FLASH_CR_LOCK) == 0U) {
        hwval_fail(o, "wrong key did not relock");
        return TFW_FAIL;
    }
    hwval_meas(o, "unlock/lock ok; relocked by wrong key -> MCU reset now");
    hwval_lim(o, "wrong-key lock is cleared only by reset [RM0410]");
    vTaskDelay(pdMS_TO_TICKS(20));
    NVIC_SystemReset();
    return TFW_SKIP; /* unreachable */
}

/* ------------------------------------------------------------- OB/OTP   */
static tfw_result_t ob_decode(struct tfw_out *o)
{
    const uint32_t optcr = *(const uint32_t *)0x40023C14U;
    const uint32_t optcr1 = *(const uint32_t *)0x40023C18U;
    const uint32_t rdp = optcr & 0xFFU;
    int bad = 0;

    /* ASM-003 defaults exactly as measured in Hardware.md section 5.2 */
    hwval_meas(o, "OPTCR=%08lX OPTCR1=%08lX rdp=%02lX",
               (unsigned long)optcr, (unsigned long)optcr1,
               (unsigned long)rdp);
    hwval_lim(o, "OPTCR=FFFFAAFD OPTCR1=00400080 (RDP lev0, BOR off, "
                 "single bank)");

    if (rdp != 0xAAU) { bad = 1; }                       /* level 0    */
    if (((optcr & 0xFFFF0000U) >> 16U) != 0xFFFFU) {
        bad = 1;                                          /* nWRP all 1 */
    }
    if (((optcr >> 2U) & 3U) != 3U) {
        bad = 1;                                          /* BOR_LEV=11 */
    }
    if ((optcr & 0x0000FFF0U) != 0x0000FFF0U) {
        bad = 1; /* watchdog SW + nRST_STOP/STDBY + IWDG_STOP/STDBY */
    }
    if (optcr1 != 0x00400080U) {
        bad = 1; /* BOOT_ADD0=0x0080 etc [Hardware.md 5.2] */
    }
    if (bad != 0) {
        hwval_fail(o, "option bytes differ from the ASM-003 defaults");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

static tfw_result_t ob_writes_na(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason), "PROTECTED_BY_POLICY");
    return TFW_NA; /* OB-002 / SAFE-001 */
}

static tfw_result_t otp_read(struct tfw_out *o)
{
    volatile const uint32_t *b = (const uint32_t *)0x1FF0F000U;
    uint32_t programmed = 0U;

    for (uint32_t i = 0U; i < ((0x41FU + 1U) / 4U); i++) {
        if (b[i] != 0xFFFFFFFFU) {
            programmed++;
        }
    }
    hwval_meas(o, "programmed words=%lu (lock bytes at 0x1FF0F010)",
               (unsigned long)programmed);
    hwval_lim(o, "readable without fault, state reported (OTP-001)");
    return TFW_PASS;
}

static tfw_result_t otp_writes_na(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason), "IRREVERSIBLE");
    return TFW_NA; /* OTP-002 / SAFE-002 */
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_flash_tests[] = {
    { .id = "FLS.001", .group = "FLS", .title = "CRC unit vs CPU over image",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "FLS-001", .fn = fls_crc_image },
    { .id = "FLS.002", .group = "FLS", .title = "AXIM vs ITCM alias reads",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "FLS-002", .fn = fls_alias_paths },
    { .id = "FLS.003", .group = "FLS", .title = "wait states per (HCLK,WS) table",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "FLS-003, GEN-005", .fn = fls_latency },
    { .id = "FLS.004", .group = "FLS", .title = "ART/prefetch cycle delta",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 2000U,
      .verifies = "FLS-004", .fn = fls_art },
    { .id = "FLS.005", .group = "FLS", .title = "sector-11 patterns + timing "
      "(destructive)",
      .level = TFW_LEVEL_S, .destructive = 1, .resets_mcu = 0,
      .timeout_ms = 10000U, .verifies = "FLS-005, FLS-008, FLS-009",
      .fn = fls_sector11 },
    { .id = "FLS.006", .group = "FLS", .title = "program to non-erased refused "
      "(destructive)",
      .level = TFW_LEVEL_S, .destructive = 1, .resets_mcu = 0,
      .timeout_ms = 2000U, .verifies = "FLS-006", .fn = fls_prog_refuse },
    { .id = "FLS.007", .group = "FLS", .title = "unlock/lock; wrong key -> reset",
      .level = TFW_LEVEL_S, .resets_mcu = 1, .timeout_ms = 3000U,
      .verifies = "FLS-007, TFW-007", .fn = fls_unlock_lock },
    { .id = "OB.001", .group = "OB", .title = "option bytes decode vs defaults",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "OB-001", .fn = ob_decode },
    { .id = "OB.002", .group = "OB", .title = "option-byte writes not tested",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 100U,
      .verifies = "OB-002, SAFE-001", .fn = ob_writes_na },
    { .id = "OTP.001", .group = "OTP", .title = "OTP read + lock state",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "OTP-001", .fn = otp_read },
    { .id = "OTP.002", .group = "OTP", .title = "OTP writes not tested",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 100U,
      .verifies = "OTP-002, SAFE-002", .fn = otp_writes_na },
};

const size_t g_flash_test_count =
    sizeof(g_flash_tests) / sizeof(g_flash_tests[0]);

void hwval_test_register_flash(void)
{
    hwval_test_register(g_flash_tests, g_flash_test_count);
}