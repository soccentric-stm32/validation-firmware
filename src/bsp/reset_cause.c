/*
 * reset_cause.c - read, report and clear RCC_CSR reset flags (GEN-010)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     GEN-010, RST-001..RST-005 (section 8.8 builds on this)
 */

#include <stdio.h>
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "bsp/reset_cause.h"
#include "bsp/uart3.h"

static uint32_t s_flags_at_boot; /* snapshot for TFW-007 post-reset checks */

uint32_t hwval_reset_flags_snapshot(void) { return s_flags_at_boot; }

static const struct {
    uint32_t    flag;
    const char *name;
} k_causes[] = {
    {RCC_CSR_LPWRRSTF, "low-power"},
    {RCC_CSR_WWDGRSTF, "window-wdg"},
    {RCC_CSR_IWDGRSTF, "ind-wdg"},
    {RCC_CSR_SFTRSTF,  "software"},
    {RCC_CSR_PORRSTF,  "power-on"},
    {RCC_CSR_PINRSTF,  "pin"},
    {RCC_CSR_BORRSTF,  "brownout"},
};

uint32_t hwval_reset_cause_flags(void)
{
    return RCC->CSR;
}

const char *hwval_reset_cause_name(uint32_t cause)
{
    static char buf[80];
    char       *out    = buf;
    size_t      remain = sizeof(buf) - 1U;

    buf[0] = '\0';
    for (unsigned i = 0; i < (sizeof(k_causes) / sizeof(k_causes[0])); i++) {
        if ((cause & k_causes[i].flag) != 0U) {
            size_t len = 0U;
            while (k_causes[i].name[len] != '\0') { len++; }
            if (out != buf) {
                if (remain >= 1U) { *out = '+'; out++; remain--; }
                else              { break; }
            }
            if (len > remain) { len = remain; }
            for (size_t j = 0U; j < len; j++) { *out++ = k_causes[i].name[j]; }
            remain -= len;
        }
    }
    *out = '\0';
    if (buf[0] == '\0') {
        return "unknown";
    }
    return buf;
}

void hwval_reset_cause_report(void)
{
    uint32_t flags = RCC->CSR; /* snapshot before clearing */

    s_flags_at_boot = flags;
    /* mirror into RTC BKP13R so bootcheck can see the causes that were set
     * even after the flags are cleared (TFW-007) */
    RTC->WPR = 0xCAU; RTC->WPR = 0x53U;
    HWVAL_BKP_RESET_FLAGS = flags;
    RTC->WPR = 0xFFU;

    char line[128];
    int n = snprintf(line, sizeof(line), "reset cause: %s (CSR %08lX)\r\n",
                     hwval_reset_cause_name(flags), (unsigned long)flags);
    (void)n;
    hwval_uart3_write(line, 0U);

    SET_BIT(RCC->CSR, RCC_CSR_RMVF); /* clear all flags */
}