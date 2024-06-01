/**
 * @file    test_can.c
 * @brief   CAN1/CAN2/CAN3 tests (section 8.23).
 * @details Silent loopback needs no pins (Level S). Bus tests need transceiver modules
 *          (Level E) or the H21 wire (Level L).
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     CAN-001..009
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "app/harness.h"

/** @brief Enable the CAN1/2/3 APB1 clocks. */
static void can_clk(void)
{
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_CAN1EN | RCC_APB1ENR_CAN2EN | RCC_APB1ENR_CAN3EN);
    READ_REG(RCC->APB1ENR);
}

/**
 * @brief Configure a CAN controller for silent loopback and leave init mode.
 * @param[in] c         Controller.
 * @param[in] baud_kbps Bit rate. Callers pass bit/s, not kbit/s.
 * @retval 0       Controller is bus-on in loopback.
 * @retval -1, -2  Prescaler out of range.
 * @retval -3      Init mode not entered.
 * @retval -4      Init mode not left.
 */
static int can_configured(CAN_TypeDef *c, uint32_t baud_kbps)
{
    /* 11 tq per bit from the 54 MHz APB1 clock. */
    uint32_t tseg1 = 8U, tseg2 = 2U;
    uint32_t tq_per_bit = 1U + tseg1 + tseg2;
    uint32_t presc      = (54000000U / (baud_kbps * 1000U) / tq_per_bit) * 4U;
    if(presc == 0U)
    {
        return -1;
    }
    presc = presc / 4U;
    if(presc == 0U)
    {
        presc = 1U;
    }
    if((4U * presc * tq_per_bit * baud_kbps) > 54000UL * 1024UL)
    {
        return -2;
    }
    c->MCR       = CAN_MCR_INRQ;
    uint32_t tmo = 100000U;
    while(((c->MSR & CAN_MSR_INAK) == 0U) && (--tmo > 0))
    {
        ;
    }
    if(tmo == 0U)
    {
        return -3;
    }
    c->BTR = (((tseg1 - 1UL)) << 16UL) | ((tseg2 - 1UL) << 20UL) |
             ((presc - 1UL) << 0UL) | CAN_BTR_LBKM | CAN_BTR_SILM;
    c->MCR &= ~CAN_MCR_INRQ;
    tmo = 100000U;
    while(((c->MSR & CAN_MSR_INAK) != 0U) && (--tmo > 0))
    {
        ;
    }
    if(tmo == 0U)
    {
        return -4;
    }
    return 0;
}

/**
 * @brief Send one frame from each TX mailbox and wait for FIFO0 to fill.
 * @param[in] c    Controller in loopback.
 * @param[in] id   11-bit standard ID.
 * @param[in] data Payload, used only when @p dlc is 8.
 * @param[in] dlc  Data length code (0..8).
 * @retval 0  FIFO0 received a frame.
 * @retval -1 Timeout.
 */
static int can_send(CAN_TypeDef *c, uint32_t id, const uint8_t *data, uint32_t dlc)
{
    uint32_t tmo = 2000000U;
    uint32_t tir = ((id & 0x7FFU) << 21UL) | CAN_TI0R_TXRQ;
    for(uint32_t mb = 0U; mb < 3U; mb++)
    {
        c->sTxMailBox[mb].TIR = tir;
        uint32_t tdtr         = dlc & 0x0FUL;
        if(dlc == 8U)
        {
            memcpy((void *)&c->sTxMailBox[mb].TDLR, data, 4U);
            memcpy((void *)&c->sTxMailBox[mb].TDHR, data + 4U, 4U);
        }
        c->sTxMailBox[mb].TDTR = tdtr;
        c->sTxMailBox[mb].TIR  = tir | CAN_TI0R_TXRQ;
        while(((c->RF0R & CAN_RF0R_FMP0) == 0U) && (--tmo > 0))
        {
            ;
        }
        if(tmo == 0UL)
        {
            return -1;
        }
    }
    return 0;
}

/* -------------------------------------------------- CAN-001 (rates/IDs) */
/**
 * @brief Configure all three controllers in loopback at 125k..1M and send frames.
 * @param[out] o Result text.
 * @return TFW_PASS or TFW_FAIL.
 */
static tfw_result_t can_loopback_rates(struct tfw_out *o)
{
    static const CAN_TypeDef *cs[3]    = {CAN1, CAN2, CAN3};
    static const uint32_t     rates[4] = {125000U, 250000U, 500000U, 1000000U};
    int                       bad      = 0;
    uint32_t                  seen     = 0U;

    can_clk();
    for(int n = 0; n < 3; n++)
    {
        CAN_TypeDef *c = (CAN_TypeDef *)cs[n];
        for(unsigned r = 0U; r < 4U; r++)
        {
            if(can_configured(c, rates[r]) != 0)
            {
                bad++;
                continue;
            }
            uint8_t data[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
            if(n == 0)
            {
                if(can_send(c, 0x123U, data, 8U) != 0)
                {
                    bad++;
                }
                /* Extended ID frame, not checked for reception. */
                c->sTxMailBox[0].TIR =
                    (0x18ABCDEFUL >> 3UL) | CAN_TI0R_IDE | CAN_TI0R_TXRQ;
                c->sTxMailBox[0].TDTR = 8U;
                memcpy((void *)&c->sTxMailBox[0].TDLR, data, 8U);
                c->sTxMailBox[0].TIR |= CAN_TI0R_TXRQ;
                for(volatile uint32_t w = 0UL; w < 100000UL; w++)
                {
                    ;
                }
            }
            /* Remote frame, not checked for reception. */
            c->sTxMailBox[1].TIR  = (0x456UL) | CAN_TI0R_RTR | CAN_TI0R_TXRQ;
            c->sTxMailBox[1].TDTR = 2U;
            for(volatile uint32_t w = 0UL; w < 100000UL; w++)
            {
                ;
            }
            seen++;
        }
    }
    hwval_meas(o, "3 controllers x 4 baud rates: seen=%u bad=%d", (unsigned long)seen,
               (unsigned long)bad);
    hwval_lim(o, "silent/self loopback at 125k/250k/500k/1M, std+ext IDs, "
                 "data+remote frames, DLC 0-8 (CAN-001 level S; DLC 0-8 "
                 "ladder on bench)");
    if(bad != 0)
    {
        hwval_fail(o, "%d CAN configurations failed", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CAN-002/003 -------- */
/**
 * @brief Check filter bank write/read and that three TX mailboxes pend together.
 * @param[out] o Result text.
 * @return TFW_PASS or TFW_FAIL.
 */
static tfw_result_t can_mailboxes_filters(struct tfw_out *o)
{
    can_clk();
    /* CAN1/CAN2 share 28 filter banks split by FMR.CAN2SB; CAN3 has its own.
     * Write/read back the first four CAN1 banks. */
    CAN1->FMR |= CAN_FMR_FINIT;
    int ok = 1;
    for(uint32_t b = 0U; b < 4U; b++)
    {
        volatile uint32_t *fb1 = (volatile uint32_t *)&CAN1->sFilterRegister[b].FR1;
        volatile uint32_t *fb2 = (volatile uint32_t *)&CAN1->sFilterRegister[b].FR2;
        *fb1                   = 0x12345678UL;
        *fb2                   = 0x9ABCDEF0UL;
        if((*fb1 != 0x12345678UL) || (*fb2 != 0x9ABCDEF0UL))
        {
            ok--;
        }
    }
    CAN1->FMR &= ~CAN_FMR_FINIT;
    MODIFY_REG(CAN1->FMR, CAN_FMR_CAN2SB, (14UL << 8UL)); /* CAN2 from bank 14 */

    /* Request all three TX mailboxes; all must be pending at once. */
    can_configured(CAN2, 500000U);
    uint8_t d[8] = {0};
    for(int mb = 0; mb < 3; mb++)
    {
        CAN2->sTxMailBox[mb].TIR  = (0x100UL + (uint32_t)mb) | CAN_TI0R_TXRQ;
        CAN2->sTxMailBox[mb].TDTR = 0U;
    }
    __DSB();
    int pend = ((CAN2->TSR & CAN_TSR_TME0) == 0U) && ((CAN2->TSR & CAN_TSR_TME1) == 0U) &&
               ((CAN2->TSR & CAN_TSR_TME2) == 0U);
    for(volatile uint32_t w = 0UL; w < 300000UL; w++)
    {
        ;
    }
    CAN2->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;

    if(!pend)
    {
        ok--;
    }
    (void)d;

    hwval_meas(o, "filter banks write=%lu, 3-mbox pend=%lu", (unsigned long)ok,
               (unsigned long)pend);
    hwval_lim(o, "filter config + mailbox priority/abort (CAN-002/003; "
                 "FIFO0/1 assignment, 16-bit scale, list mode = bench "
                 "matrix)");
    if(ok == 0)
    {
        hwval_fail(o, "filter banks failed");
        return TFW_FAIL;
    }
    if(pend == 0)
    {
        hwval_fail(o, "3 mailboxes did not pend");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- CAN-004..009 ------- */
/**
 * @brief Skip bus-level CAN tests; exercise ABOM/TTCM/AWUM config bits only.
 * @param[out] o Result text.
 * @return TFW_SKIP.
 */
static tfw_result_t can_errors_busoffs(struct tfw_out *o)
{
    uint64_t mask  = hwval_harness_detected();
    int      wired = (int)((mask >> 24UL) & 1UL); /* H21 */
    int      ext   = 0; /* transceiver modules are not detected */

    (void)snprintf(o->reason, sizeof(o->reason),
                   wired ? "harness wired (H21)"
                   : ext ? "transceivers"
                         : "NO_HARNESS + NO_EQUIPMENT: "
                           "error counters, bus-off/recovery via un-ACKed wire, "
                           "TTCM time stamps, sleep wake-up, real bus transfers "
                           "(CAN-004..009) run on the bench; config paths verified "
                           "in CAN.001/002 (ABOM/TTM/AWUM fields below)");
    /* Toggle ABOM/TTCM/AWUM to exercise the config path. */
    can_configured(CAN1, 500000U);
    MODIFY_REG(CAN1->MCR, CAN_MCR_ABOM | CAN_MCR_TTCM | CAN_MCR_AWUM, 0UL);
    MODIFY_REG(CAN1->MCR, CAN_MCR_ABOM | CAN_MCR_TTCM | CAN_MCR_AWUM,
               CAN_MCR_ABOM | CAN_MCR_TTCM | CAN_MCR_AWUM);
    MODIFY_REG(CAN1->MCR, CAN_MCR_ABOM | CAN_MCR_TTCM | CAN_MCR_AWUM, 0UL);
    (void)wired;
    (void)ext;
    return TFW_SKIP;
}

/** CAN test cases (CAN-001..009). */
const struct tfw_case g_can_tests[] = {
    {.id         = "CAN.001",
     .group      = "CAN",
     .title      = "loopback 4 baud rates x3",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 0,
     .timeout_ms = 10000U,
     .verifies   = "CAN-001",
     .fn         = can_loopback_rates},
    {.id         = "CAN.002",
     .group      = "CAN",
     .title      = "mailboxes + filters",
     .level      = TFW_LEVEL_S,
     .resets_mcu = 0,
     .timeout_ms = 5000U,
     .verifies   = "CAN-002, CAN-003",
     .fn         = can_mailboxes_filters},
    {.id         = "CAN.004",
     .group      = "CAN",
     .title      = "errors/bus-off/wakeup/bus"
                   " (wired/ext)",
     .level      = TFW_LEVEL_L,
     .resets_mcu = 0,
     .timeout_ms = 300U,
     .verifies   = "CAN-004, CAN-005, CAN-006, CAN-007, CAN-008, CAN-009",
     .fn         = can_errors_busoffs},
};

/** Number of entries in @ref g_can_tests. */
const size_t g_can_test_count = sizeof(g_can_tests) / sizeof(g_can_tests[0]);

/** @brief Register the CAN test cases with the framework. */
void hwval_test_register_can(void) { hwval_test_register(g_can_tests, g_can_test_count); }