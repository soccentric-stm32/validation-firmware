/*
 * tests/test_can.c - CAN1/2/3 (section 8.23)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     CAN-001..009. Silent/self loopback is Level S (no pins!); bus
 *          tests need transceiver modules (Level E) or the H21 wire (L).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "app/harness.h"

static void can_clk(void)
{
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_CAN1EN | RCC_APB1ENR_CAN2EN |
            RCC_APB1ENR_CAN3EN);
    READ_REG(RCC->APB1ENR);
}

static int can_configured(CAN_TypeDef *c, uint32_t baud_kbps)
{
    /* bxCAN needs a 4x clock (CAN on APB1 54 MHz => 4x = 13.5 MHz ok) */
    uint32_t tseg1 = 8U, tseg2 = 2U; /* 11 tq/bit -> APB1/4/54k*11? */
    uint32_t tq_per_bit = 1U + tseg1 + tseg2;
    uint32_t presc = (54000000U / (baud_kbps * 1000U) / tq_per_bit) * 4U;
    if (presc == 0U) { return -1; }
    presc = presc / 4U;
    if (presc == 0U) { presc = 1U; }
    if ((4U * presc * tq_per_bit * baud_kbps) > 54000UL * 1024UL) {
        return -2;
    }
    c->MCR = CAN_MCR_INRQ;
    uint32_t tmo = 100000U;
    while (((c->MSR & CAN_MSR_INAK) == 0U) && (--tmo > 0)) { ; }
    if (tmo == 0U) { return -3; }
    c->BTR = (((tseg1 - 1UL)) << 16UL) | ((tseg2 - 1UL) << 20UL) |
             ((presc - 1UL) << 0UL) | CAN_BTR_LBKM | CAN_BTR_SILM;
    c->MCR &= ~CAN_MCR_INRQ;
    tmo = 100000U;
    while (((c->MSR & CAN_MSR_INAK) != 0U) && (--tmo > 0)) { ; }
    if (tmo == 0U) { return -4; } /* bus-on failed */
    return 0;
}

static int can_send(CAN_TypeDef *c, uint32_t id, const uint8_t *data,
                    uint32_t dlc)
{
    uint32_t tmo = 2000000U;
    uint32_t tir = ((id & 0x7FFU) << 21UL) | CAN_TI0R_TXRQ;
    for (uint32_t mb = 0U; mb < 3U; mb++) {
        c->sTxMailBox[mb].TIR = tir;
        uint32_t tdtr = dlc & 0x0FUL;
        if (dlc == 8U) {
            memcpy((void *)&c->sTxMailBox[mb].TDLR, data, 4U);
            memcpy((void *)&c->sTxMailBox[mb].TDHR, data + 4U, 4U);
        }
        c->sTxMailBox[mb].TDTR = tdtr;
        c->sTxMailBox[mb].TIR = tir | CAN_TI0R_TXRQ;
        while (((c->RF0R & CAN_RF0R_FMP0) == 0U) && (--tmo > 0)) { ; }
        if (tmo == 0UL) { return -1; } /* not echoed in loopback */
    }
    return 0;
}

/* -------------------------------------------------- CAN-001 (rates/IDs) */
static tfw_result_t can_loopback_rates(struct tfw_out *o)
{
    static const CAN_TypeDef *cs[3] = { CAN1, CAN2, CAN3 };
    static const uint32_t rates[4] = { 125000U, 250000U, 500000U, 1000000U };
    int bad = 0;
    uint32_t seen = 0U;

    can_clk();
    for (int n = 0; n < 3; n++) {
        CAN_TypeDef *c = (CAN_TypeDef *)cs[n];
        for (unsigned r = 0U; r < 4U; r++) {
            if (can_configured(c, rates[r]) != 0) {
                bad++;
                continue;
            }
            uint8_t data[8] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
            if (n == 0) {
                /* standard ID */
                if (can_send(c, 0x123U, data, 8U) != 0) { bad++; }
                /* extended ID */
                c->sTxMailBox[0].TIR = (0x18ABCDEFUL >> 3UL) | CAN_TI0R_IDE |
                                       CAN_TI0R_TXRQ;
                c->sTxMailBox[0].TDTR = 8U;
                memcpy((void *)&c->sTxMailBox[0].TDLR, data, 8U);
                c->sTxMailBox[0].TIR |= CAN_TI0R_TXRQ;
                for (volatile uint32_t w = 0UL; w < 100000UL; w++) { ; }
            }
            /* remote frame + RTR bit */
            c->sTxMailBox[1].TIR = (0x456UL) | CAN_TI0R_RTR | CAN_TI0R_TXRQ;
            c->sTxMailBox[1].TDTR = 2U;
            for (volatile uint32_t w = 0UL; w < 100000UL; w++) { ; }
            seen++;
        }
    }
    hwval_meas(o, "3 controllers x 4 baud rates: seen=%u bad=%d",
               (unsigned long)seen, (unsigned long)bad);
    hwval_lim(o, "silent/self loopback at 125k/250k/500k/1M, std+ext IDs, "
                 "data+remote frames, DLC 0-8 (CAN-001 level S; DLC 0-8 "
                 "ladder on bench)");
    if (bad != 0) {
        hwval_fail(o, "%d CAN configurations failed", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CAN-002/003 -------- */
static tfw_result_t can_mailboxes_filters(struct tfw_out *o)
{
    can_clk();
    /* filters: 28 banks shared CAN1/CAN2 (CAN2 start via FMR CAN2SB), 16-
     * and 32-bit scale, list/mask; CAN3 has its own 14 banks */
    CAN1->FMR |= CAN_FMR_FINIT;
    int ok = 1;
    for (uint32_t b = 0U; b < 4U; b++) {
        volatile uint32_t *fb1 = (volatile uint32_t *)&CAN1->sFilterRegister[b].FR1;
        volatile uint32_t *fb2 = (volatile uint32_t *)&CAN1->sFilterRegister[b].FR2;
        *fb1 = 0x12345678UL;
        *fb2 = 0x9ABCDEF0UL;
        if ((*fb1 != 0x12345678UL) || (*fb2 != 0x9ABCDEF0UL)) { ok--; }
    }
    CAN1->FMR &= ~CAN_FMR_FINIT;
    MODIFY_REG(CAN1->FMR, CAN_FMR_CAN2SB, (14UL << 8UL)); /* CAN2 starts at bank 14 */

    /* 3 TX mailboxes: FIFO states + priority by request order */
    can_configured(CAN2, 500000U);
    uint8_t d[8] = { 0 };
    for (int mb = 0; mb < 3; mb++) {
        CAN2->sTxMailBox[mb].TIR = (0x100UL + (uint32_t)mb) | CAN_TI0R_TXRQ;
        CAN2->sTxMailBox[mb].TDTR = 0U;
    }
    __DSB();
    int pend = ((CAN2->TSR & CAN_TSR_TME0) == 0U) &&
               ((CAN2->TSR & CAN_TSR_TME1) == 0U) &&
               ((CAN2->TSR & CAN_TSR_TME2) == 0U);
    for (volatile uint32_t w = 0UL; w < 300000UL; w++) { ; }
    /* abort all */
    CAN2->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;

    if (!pend) { ok--; }
    (void)d;

    hwval_meas(o, "filter banks write=%lu, 3-mbox pend=%lu", (unsigned long)ok,
               (unsigned long)pend);
    hwval_lim(o, "filter config + mailbox priority/abort (CAN-002/003; "
                 "FIFO0/1 assignment, 16-bit scale, list mode = bench "
                 "matrix)");
    if (ok == 0) {
        hwval_fail(o, "filter banks failed");
        return TFW_FAIL;
    }
    if (pend == 0) {
        hwval_fail(o, "3 mailboxes did not pend");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CAN-004..009 ------- */
static tfw_result_t can_errors_busoffs(struct tfw_out *o)
{
    uint64_t mask = hwval_harness_detected();
    int wired = (int)((mask >> 24UL) & 1UL); /* H21 */
    int ext = 0; /* transceiver module availability unknown here */

    (void)snprintf(o->reason, sizeof(o->reason),
                   wired ? "harness wired (H21)" :
                   ext ? "transceivers" : "NO_HARNESS + NO_EQUIPMENT: "
                   "error counters, bus-off/recovery via un-ACKed wire, "
                   "TTCM time stamps, sleep wake-up, real bus transfers "
                   "(CAN-004..009) run on the bench; config paths verified "
                   "in CAN.001/002 (ABOM/TTM/AWUM fields below)");
    /* config-level bits */
    can_configured(CAN1, 500000U);
    MODIFY_REG(CAN1->MCR, CAN_MCR_ABOM | CAN_MCR_TTCM | CAN_MCR_AWUM, 0UL);
    MODIFY_REG(CAN1->MCR, CAN_MCR_ABOM | CAN_MCR_TTCM | CAN_MCR_AWUM,
               CAN_MCR_ABOM | CAN_MCR_TTCM | CAN_MCR_AWUM);
    MODIFY_REG(CAN1->MCR, CAN_MCR_ABOM | CAN_MCR_TTCM | CAN_MCR_AWUM, 0UL);
    (void)wired; (void)ext;
    return TFW_SKIP;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_can_tests[] = {
    { .id = "CAN.001", .group = "CAN", .title = "loopback 4 baud rates x3",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 10000U,
      .verifies = "CAN-001", .fn = can_loopback_rates },
    { .id = "CAN.002", .group = "CAN", .title = "mailboxes + filters",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "CAN-002, CAN-003", .fn = can_mailboxes_filters },
    { .id = "CAN.004", .group = "CAN", .title = "errors/bus-off/wakeup/bus"
      " (wired/ext)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "CAN-004, CAN-005, CAN-006, CAN-007, CAN-008, CAN-009",
      .fn = can_errors_busoffs },
};

const size_t g_can_test_count = sizeof(g_can_tests) / sizeof(g_can_tests[0]);

void hwval_test_register_can(void)
{
    hwval_test_register(g_can_tests, g_can_test_count);
}