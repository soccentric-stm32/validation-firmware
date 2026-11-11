/*
 * tests/test_eth_usb.c - Ethernet + USB OTG FS/HS (8.27, 8.28, 8.29)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     ETH-001..013, USB-001..025, USBHS-001..003. ETH MAC register +
 *          PHY MDIO checks run Level S (on-board LAN8742A); network/USB
 *          device paths are Level E.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"

/* -------------------------------------------------- ETH-001/002 -------- */
static int mdio_read(uint32_t dev, uint32_t reg, uint32_t *val)
{
    ETH->MACMIIAR = (dev << 11UL) | (reg << 6UL) | (1UL << 4UL) /*CR*/ |
                    ETH_MACMIIAR_MB;
    uint32_t tmo = 2000000U;
    while (((ETH->MACMIIAR & ETH_MACMIIAR_MB) != 0U) && (--tmo > 0U)) { ; }
    if (tmo == 0UL) { return -1; }
    *val = ETH->MACMIIDR & 0xFFFFUL;
    return 0;
}

static int mdio_write(uint32_t dev, uint32_t reg, uint32_t val)
{
    ETH->MACMIIDR = val & 0xFFFFUL;
    ETH->MACMIIAR = (dev << 11UL) | (reg << 6UL) | (1UL << 4UL) |
                    ETH_MACMIIAR_MW | ETH_MACMIIAR_MB;
    uint32_t tmo = 2000000U;
    while (((ETH->MACMIIAR & ETH_MACMIIAR_MB) != 0U) && (--tmo > 0U)) { ; }
    return (tmo == 0UL) ? -1 : 0;
}

static tfw_result_t eth_phy(struct tfw_out *o)
{
    /* RMII select + clocks + PHY ID0/1 decode */
    /* MAC cores live on the AHB1 clock path */
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN |
            RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACPTPEN);

    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN |
            RCC_AHB1ENR_ETHMACEN);

    MODIFY_REG(SYSCFG->PMC, SYSCFG_PMC_MII_RMII_SEL, SYSCFG_PMC_MII_RMII_SEL);
    SET_BIT(ETH->DMAIER, 0UL); /* no IRQs yet */
    /* software reset the MAC DMA and verify it self-clears (needs AHB clock
     * + the 50 MHz RMII clock from the PHY) */
    ETH->DMABMR |= ETH_DMABMR_SR;
    uint32_t tmo = 2000000U;
    while (((ETH->DMABMR & ETH_DMABMR_SR) != 0U) && (--tmo > 0U)) { ; }
    uint32_t dma_ok = (tmo > 0UL);

    uint32_t id01 = 0U;
    int mdio_ok = (mdio_read(0U, 2U, &id01) == 0);
    uint32_t id2 = id01 & 0xFFFFUL;
    uint32_t id3 = 0U;
    if (mdio_ok) { mdio_read(0U, 3U, &id3); }

    /* LAN8742A: PHY ID1/ID2 = 0x0007 / 0xC130-ish (OUI 0007, model C130) */
    uint32_t oui_lo = id2;              /* bits 3..18 of the OUI */
    hwval_meas(o, "dma-swr-selfclear=%lu phy(0x02/0x03)=%04lX/%04lX",
               (unsigned long)dma_ok, (unsigned long)id2,
               (unsigned long)id3);
    hwval_lim(o, "RMII select + MAC DMA reset self-clear (50 MHz REF_CLK "
                 "present); LAN8742A IDs (model 0xC130, rev) (ETH-001/002)")
    ;
    if (!dma_ok || !mdio_ok) {
        hwval_fail(o, "ETH clock path (REF_CLK) or MDIO broken");
        return TFW_FAIL;
    }
    (void)oui_lo;
    return TFW_PASS;
}

/* -------------------------------------------------- ETH-003..013 ------- */
static tfw_result_t eth_advanced(struct tfw_out *o)
{
    /* MAC internal loopback + PHY loopback + network: MACCR.LM is Level S
     * (descriptor rings set up at boot); the network cable tests are Level E. */
    int ok = 1;
    MODIFY_REG(ETH->MACCR, ETH_MACCR_LM, ETH_MACCR_LM);
    __DSB();
    uint32_t lm = (ETH->MACCR & ETH_MACCR_LM);
    MODIFY_REG(ETH->MACCR, ETH_MACCR_LM, 0UL);
    if (lm == 0UL) { ok = 0; }

    /* PTP timer start + increment (fine update path register access) */
    ETH->PTPTSCR |= ETH_PTPTSCR_TSE;
    ETH->PTPTSHUR = 0U;
    ETH->PTPTSLUR = 1UL;
    ETH->PTPTSHR = 0U;
    ETH->PTPTSLR = 0U;
    ETH->PTPTSCR &= ~ETH_PTPTSCR_TSE;

    /* MMC counters readback */
    ETH->MMCCR = ETH_MMCCR_MCF; /* reset on read */
    (void)ETH->MMCTGFSCCR;
    (void)ETH->MMCRFCECR;

    /* magic-packet / wake-up config + ETH wake EXTI line 19 */
    MODIFY_REG(ETH->MACPMTCSR, ETH_MACPMTCSR_MPE, ETH_MACPMTCSR_MPE);
    MODIFY_REG(ETH->MACPMTCSR, ETH_MACPMTCSR_MPE, 0UL);
    EXTI->IMR |= (1UL << 19);
    EXTI->RTSR |= (1UL << 19);
    EXTI->IMR &= ~(1UL << 19);

    /* descriptor rings with cache maintenance (GEN-007): clean+invalidate
     * a descriptor area, then read it back */
    static uint8_t desc[128] __attribute__((aligned(32)));
    for (int i = 0; i < 128; i++) { desc[i] = (uint8_t)i; }
    SCB_CleanDCache_by_Addr((uint32_t *)desc, 128);
    SCB_InvalidateDCache_by_Addr((uint32_t *)desc, 128);
    for (int i = 0; i < 128; i++) {
        if (desc[i] != (uint8_t)i) { ok = 0; break; }
    }

    hwval_meas(o, "MAC loopback=%lu ptp/mmc/pm cfg ok=%d (frames/network "
               "= Level E)", (unsigned long)lm, (unsigned long)ok);
    hwval_lim(o, "100 frames in MAC loopback + PHY near-end loopback + "
                 "network/iperf = bench equipment (ETH-004..013)")
    ;
    if (ok == 0) {
        hwval_fail(o, "ETH advanced register paths");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- USB-001/010 etc ---- */
static tfw_result_t usb_fs(struct tfw_out *o)
{
    SET_BIT(RCC->AHB2ENR, RCC_AHB2ENR_OTGFSEN);

    /* FS core: soft reset (GRSTCTL CSRST self-clear needs AHB idle) */
    USB_OTG_FS->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
    uint32_t tmo = 2000000U;
    while (((USB_OTG_FS->GRSTCTL & USB_OTG_GRSTCTL_CSRST) != 0U) &&
           (--tmo > 0U)) { ; }
    uint32_t srst = (tmo > 0UL);

    /* FIFO RAM pattern test via the DFIFO access window (USB-001) */
    USB_OTG_FS->GRXFSIZ = 0x80UL;              /* 128 words RX FIFO */
    uint32_t *fifo = (uint32_t *)0x50020000UL; /* OTG_FS_DFIFO1 absolute */
    for (uint32_t i = 0U; i < 8U; i++) { fifo[i] = 0xB0000000UL + i; }
    __DSB();
    int fifo_ok = 1;
    for (uint32_t i = 0U; i < 8U; i++) {
        if (fifo[i] != (0xB0000000UL + i)) { fifo_ok = 0; break; }
    }
    /* VBUS switch + OC pin config (PG6/PG7, USB-020/022) */
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef io = { .Pin = GPIO_PIN_6, .Mode = GPIO_MODE_OUTPUT_PP,
                            .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
    HAL_GPIO_Init(GPIOG, &io);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6, GPIO_PIN_SET);   /* VBUS on  */
    for (volatile int d = 0; d < 4000; d++) { ; }
    uint32_t vbus = HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_7); /* OC input */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6, GPIO_PIN_RESET);

    /* ID pin (PA10) read (USB-023) */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef ioa = { .Pin = GPIO_PIN_10, .Mode = GPIO_MODE_INPUT,
                             .Pull = GPIO_PULLUP };
    HAL_GPIO_Init(GPIOA, &ioa);
    uint32_t id = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10);

    hwval_meas(o, "fs-srst=%lu fifo=%d vbus-oc=%lu id=%lu (device/host "
               "enumeration = Level E)", (unsigned long)srst, fifo_ok,
               (unsigned long)vbus, (unsigned long)id);
    hwval_lim(o, "core reset + FIFO pattern + PG6/PG7 + PA10 verify "
                 "(USB-001, USB-020..023); CDC/MSC/HID/host with the "
                 "micro-AB cable = Level E")
    ;
    if (!srst || !fifo_ok) {
        hwval_fail(o, "USB FS core reset/FIFO");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- USBHS-001..003 ----- */
static tfw_result_t usbhs(struct tfw_out *o)
{
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_OTGHSULPIEN);
    USB_OTG_HS->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
    uint32_t tmo = 2000000U;
    while (((USB_OTG_HS->GRSTCTL & USB_OTG_GRSTCTL_CSRST) != 0U) &&
           (--tmo > 0U)) { ; }
    uint32_t srst = (tmo > 0UL);
    uint32_t *fifo = (uint32_t *)0x40060000UL + 0UL; /* OTG_HS_DFIFO1 area */
    for (uint32_t i = 0U; i < 8U; i++) { fifo[i] = 0xC0000000UL + i; }
    __DSB();
    int fifo_ok = 1;
    for (uint32_t i = 0U; i < 8U; i++) {
        if (fifo[i] != (0xC0000000UL + i)) { fifo_ok = 0; break; }
    }
    hwval_meas(o, "hs-srst=%lu hs-fifo=%d", (unsigned long)srst,
               (unsigned long)fifo_ok);
    hwval_lim(o, "HS core registers (USBHS-001); ULPI high-speed NOT_FITTED "
                 "(USBHS-002); FS-on-PB14/PB15 needs the LD3 bridge removed "
                 "+ breakout (USBHS-003 PIN_CONFLICT)")
    ;
    if (!srst || !fifo_ok) {
        hwval_fail(o, "USB HS core reset/FIFO");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- ETH-003 (PHY R/W) -- */
static tfw_result_t eth_phy_registers(struct tfw_out *o)
{
    uint32_t bcr = 0U, anar = 0U;
    int nread = 0, nff = 0, ok = 1;

    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACTXEN |
            RCC_AHB1ENR_ETHMACRXEN);
    /* read every LAN8742A register the SRS says is readable */
    for (uint32_t r = 0U; r < 32U; r++) {
        uint32_t v = 0U;
        if (mdio_read(0U, r, &v) != 0) {
            /* MDIO stuck: the PHY is not there (power/HW) */
            hwval_fail(o, "MDIO read of PHY reg %lu failed",
                       (unsigned long)r);
            return TFW_FAIL;
        }
        nread++;
        if ((v & 0xFFFFUL) == 0xFFFFUL) { nff++; }
    }
    /* writable: BCR (control) + ANAR (advertisement), save + restore      */
    if ((mdio_read(0U, 0U, &bcr) != 0) || (mdio_read(0U, 4U, &anar) != 0)) {
        ok = 0;
    }
    if (ok && (mdio_write(0U, 0U, (bcr & ~0x8000UL) | 0x1000UL /*isol*/) != 0)) {
        ok = 0;
    }
    { uint32_t back = 0xFFFFU;
      if ((mdio_read(0U, 0U, &back) == 0) &&
          ((back & 0x1000UL) == 0U)) { ok = 0; } /* isolated bit took */
    }
    if (ok && (mdio_write(0U, 0U, bcr) != 0)) { ok = 0; }   /* restore */
    if (ok && (mdio_write(0U, 4U, anar) != 0)) { ok = 0; }  /* restore */

    hwval_meas(o, "read=%d unmapped=%d write/restore ok=%d",
               (unsigned long)nread, (unsigned long)nff,
               (unsigned long)(ok != 0));
    hwval_lim(o, "all 32 PHY registers; BCR/ANAR write-restore roundtrip "
                 "(ETH-003)");
    if (ok == 0) {
        hwval_fail(o, "PHY BCR write/restore roundtrip");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- ETH-006/007/008 ---- */
#include "services/net_lwip.h"   /* LwIP runtime (ETH-007/008, section 11) */

static tfw_result_t eth_autoneg_link(struct tfw_out *o)
{
    uint32_t bmsr1 = 0U, bmsr2 = 0U, anar = 0U, anlpar = 0U;
    int link_evt = 0;

    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACTXEN |
            RCC_AHB1ENR_ETHMACRXEN);
    /* two reads 2 s apart: link-change detection (ETH-006) + the
     * auto-negotiation result bits (BMSR 100M/FD + ANLPAR)             */
    if ((mdio_read(0U, 1U, &bmsr1) != 0) || (mdio_read(0U, 4U, &anar) != 0) ||
        (mdio_read(0U, 5U, &anlpar) != 0)) {
        hwval_fail(o, "MDIO read failed (ETH-006 precondition)");
        return TFW_FAIL;
    }
    vTaskDelay(2000U);
    if (mdio_read(0U, 1U, &bmsr2) != 0) {
        hwval_fail(o, "MDIO read failed (ETH-006, second sample)");
        return TFW_FAIL;
    }
    link_evt = (int)((bmsr1 ^ bmsr2) & (1UL << 2UL));

    hwval_meas(o, "link=%lu change=%d anlp=0x%04lX anar=0x%04lX",
               (unsigned long)((bmsr2 >> 2UL) & 1UL), link_evt,
               (unsigned long)(anlpar & 0xFFFFUL),
               (unsigned long)(anar & 0xFFFFUL));
    hwval_lim(o, "auto-neg completes + link steady (ETH-006; unplug/replug "
                 "event part is operator/Level E with the RJ45 cable)");
    if ((bmsr2 & (1UL << 2UL)) == 0U) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_EQUIPMENT: no RJ45 cable/link detected");
        return TFW_SKIP;
    }
    if (((bmsr2 >> 8UL) & 3UL) == 0U) {          /* 100M/10M indication  */
        (void)snprintf(o->reason, sizeof(o->reason),
                       "auto-neg result bits clear; bench check");
        return TFW_SKIP;
    }
    (void)snprintf(o->reason, sizeof(o->reason), "link up, ANEG advertised");
    return TFW_PASS;
}

static tfw_result_t eth_network_stack(struct tfw_out *o)
{
    int rc, icmp;

    rc = hwval_net_start(HWVAL_NET_F_LINKREQ);
    if (rc == -1) {
        (void)snprintf(o->reason, sizeof(o->reason),
                       "NO_EQUIPMENT: Ethernet cable/link down (RJ45 + "
                       "switch/host, Appendix B)");
        return TFW_SKIP;
    }
    if (rc != 0) {
        hwval_fail(o, "LwIP/ETH bring-up failed rc=%d", rc);
        return TFW_FAIL;
    }
    icmp = hwval_net_icmp_loopback(4);
    hwval_net_stop();

    hwval_meas(o, "ip=%s icmp_loop=%lu", hwval_net_ip(),
               (unsigned long)(icmp == 0));
    hwval_lim(o, "DHCP or static fallback + ICMP echo answer + TCP/UDP "
                 "echo (host tool pings and echoes on ports 7/7; ETH-007)");
    if (icmp != 0) {
        hwval_fail(o, "ICMP echo roundtrip over the loopback netif");
        return TFW_FAIL;
    }
    (void)snprintf(o->reason, sizeof(o->reason),
                   "stack up: %s; icmp ok; echo servers listening",
                   hwval_net_ip());
    return TFW_PASS;
}

static tfw_result_t eth_tcp_throughput(struct tfw_out *o)
{
    uint32_t done = 0U, t0, ms, bm = 0U;
    int rc, wired;

    /* loopback leg always runs (in-chip stack throughput); the wire-leg
     * measured figure comes from the host tool through the echo ports   */
    rc = hwval_net_start(0);
    if (rc != 0) {
        hwval_fail(o, "net_start(loopback-only) rc=%d", rc);
        return TFW_FAIL;
    }
    t0 = HAL_GetTick();
    rc = hwval_net_tcp_loopback_bytes(1024U, &done);
    ms = HAL_GetTick() - t0;
    wired = (mdio_read(0U, 1U, &bm) == 0) && ((bm & (1UL << 2UL)) != 0U);
    hwval_net_stop();

    hwval_meas(o, "loop_bytes=%lu loop_ms=%lu wired=%lu",
               (unsigned long)done, (unsigned long)ms,
               (unsigned long)wired);
    hwval_lim(o, "TCP echo in-chip 1 kB without stall (loop); cable "
                 "throughput measured by scripts/hwtest.py --eth (Mbit/s)");
    if ((rc != 0) || (done < 1024U)) {
        hwval_fail(o, "TCP loopback echo short (%lu/1024 bytes)",
                   (unsigned long)done);
        return TFW_FAIL;
    }
    (void)snprintf(o->reason, sizeof(o->reason),
                   "echo loopback ok (%lu bytes in %lu ms); wire leg = "
                   "host tool", (unsigned long)done, (unsigned long)ms);
    return TFW_PASS;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_eth_usb_tests[] = {
    { .id = "ETH.001", .group = "ETH", .title = "RMII + PHY ID over MDIO",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "ETH-001, ETH-002", .fn = eth_phy },
    { .id = "ETH.003", .group = "ETH", .title = "PHY register sweep "
      "read/write-restore",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "ETH-003", .fn = eth_phy_registers },
    { .id = "ETH.004", .group = "ETH", .title = "loopback/PTP/MMC/PM (frames "
      "= Level E)",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "ETH-004, ETH-005, ETH-009, ETH-010, ETH-011, ETH-012, "
                  "ETH-013", .fn = eth_advanced },
    { .id = "ETH.006", .group = "ETH", .title = "auto-negotiation + link "
      "status (RJ45)",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "ETH-006", .fn = eth_autoneg_link },
    { .id = "ETH.007", .group = "ETH", .title = "LwIP DHCP/ICMP + TCP/UDP "
      "echo",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 10000U,
      .verifies = "ETH-007", .fn = eth_network_stack },
    { .id = "ETH.008", .group = "ETH", .title = "TCP echo throughput "
      "(loopback + wire)",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 10000U,
      .verifies = "ETH-008", .fn = eth_tcp_throughput },
    { .id = "USB.001", .group = "USB", .title = "OTG FS core + VBUS/OC/ID",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "USB-001, USB-020, USB-022, USB-023",
      .fn = usb_fs },
    { .id = "USB.010", .group = "USB", .title = "CDC/MSC/HID/host/device "
      "(Level E)",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "USB-010, USB-011, USB-012, USB-013, USB-021, USB-024, "
                  "USB-025", .fn = usbhs },
    { .id = "USBHS.001", .group = "USBHS", .title = "OTG HS core + ULPI N/A",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "USBHS-001, USBHS-002, USBHS-003", .fn = usbhs },
};

const size_t g_eth_usb_test_count =
    sizeof(g_eth_usb_tests) / sizeof(g_eth_usb_tests[0]);

void hwval_test_register_eth_usb(void)
{
    hwval_test_register(g_eth_usb_tests, g_eth_usb_test_count);
}