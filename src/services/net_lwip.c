/*
 * src/net_lwip.c - LwIP port + raw echo servers (ETH-007 / ETH-008)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * SRS:     ETH-007, ETH-008, GEN-007 (coherent DMA buffers).
 *
 * NO_SYS polled LwIP from the CubeF7 v1.17.4 SDK (CON-002). The ETH DMA
 * buffers live in .sram1 (MPU region 3: non-cacheable, NFR-004 layout)
 * and the descriptor tables in .noncacheable (MPU region 0); LwIP's own
 * pools are CPU-side (DTCM .bss, see board/lwipopts.h).
 *
 * RX is copy-mode: HAL_ETH_RxAllocateCallback (overridden) hands the
 * DMA a .sram1 buffer per descriptor; HAL_ETH_RxLinkCallback chains the
 * per-frame ETH_BufferTypeDef nodes; the poll loop copies each frame
 * into a pbuf and feeds netif->input, then releases the buffers.
 */

#include <string.h>
#include "stm32f7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/ip.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"
#include "services/net_lwip.h"

#ifndef ETH_RX_BUF_SIZE
#define ETH_RX_BUF_SIZE 1524U                   /* one max frame per buf  */
#endif
#define HWVAL_ETH_RX_BUFFERS 4U                 /* >= ETH_RX_DESC_CNT     */

/* ---------------------------------------------------------------- buffers */
/* .sram1: non-cacheable (MPU region 3) so the ETH DMA masters see CPU
 * stores directly (GEN-007); same section as the other DMA test buffers
 * (NFR-004 keeps >= 256 KB of SRAM1 free above it). */
static uint8_t s_rxbuf[HWVAL_ETH_RX_BUFFERS][ETH_RX_BUF_SIZE]
    __attribute__((section(".sram1"), aligned(32)));
static uint8_t s_txbuf[ETH_RX_BUF_SIZE]          /* TX: copy of the pbuf  */
    __attribute__((section(".sram1"), aligned(32)));

/* descriptors: .noncacheable (MPU region 0, GEN-007) */
static ETH_DMADescTypeDef s_dmarx[ETH_RX_DESC_CNT]
    __attribute__((section(".noncacheable"), aligned(4)));
static ETH_DMADescTypeDef s_dmatx[ETH_TX_DESC_CNT]
    __attribute__((section(".noncacheable"), aligned(4)));

/* per-buffer bookkeeping: busy map (DMA) + parallel ETH_BufferTypeDef
 * chain nodes for HAL_ETH_RxLinkCallback. */
static uint8_t           s_rx_busy[HWVAL_ETH_RX_BUFFERS];
static uint32_t          s_rx_next;
static ETH_BufferTypeDef s_rxstruct[HWVAL_ETH_RX_BUFFERS];

/* ---------------------------------------------------------------- HAL ETH */
static ETH_HandleTypeDef heth;

#define HWVAL_PHY_ADDR        0U                /* RMII strap (ETH-002)   */
#define HWVAL_PHY_BCR         0U
#define HWVAL_PHY_BMSR        1U
#define HWVAL_PHY_BMSR_LINK   (1UL << 2UL)
#define HWVAL_PHY_ANEG_100FD  0x2100UL          /* ANEG enable, 100M/FD   */

#define HWVAL_NET_DHCP_TIMEOUT_MS 3000UL
#define HWVAL_NET_STATIC_IP    0xC0A8000AU      /* 192.168.0.10           */
#define HWVAL_NET_STATIC_NM    0xFFFFFF00U      /* 255.255.255.0          */
#define HWVAL_NET_STATIC_GW    0xC0A80001U      /* 192.168.0.1            */

static uint8_t s_mac[6] = { 0x00U, 0x80U, 0xE1U, 0xF7U, 0x76U, 0x7AU };

static int s_up;
static int s_tcp_echo_up;
static int s_udp_echo_up;
static int s_no_mac;               /* HWVAL_NET_F_NO_MAC: loopback-only */
static struct netif s_netif;
static char s_ip_str[16];

u32_t sys_now(void)                 /* LwIP system time (NO_SYS polling) */
{
    return HAL_GetTick();
}

/* SYS_LIGHTWEIGHT_PROT critical sections (lwip/sys.h routes the SYS_ARCH
 * macros here even with NO_SYS=1). Single-context stack: a plain BASEPRI
 * section is the cheapest correct primitive. */
sys_prot_t sys_arch_protect(void)
{
    sys_prot_t p = __get_BASEPRI();

    __set_BASEPRI(1UL << 4);
    return p;
}

void sys_arch_unprotect(sys_prot_t p)
{
    __set_BASEPRI(p);
}

static err_t linkoutput(struct netif *netif, struct pbuf *p);

/* --------------------------------------------------- Rx buffer plumbing --- */
/* HAL weak override: one .sram1 buffer per built descriptor. */
void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    for (uint32_t i = 0U; i < HWVAL_ETH_RX_BUFFERS; i++) {
        uint32_t idx = (s_rx_next + i) % HWVAL_ETH_RX_BUFFERS;
        if (s_rx_busy[idx] == 0U) {
            s_rx_busy[idx] = 1U;
            s_rx_next = (idx + 1U) % HWVAL_ETH_RX_BUFFERS;
            s_rxstruct[idx].next   = NULL;
            *buff = s_rxbuf[idx];
            return;
        }
    }
    *buff = NULL;                               /* pool exhausted         */
}

/* HAL weak override: chain this buffer into the frame descriptor list. */
void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff,
                            uint16_t Length)
{
    uint32_t ofs = (uint32_t)buff - (uint32_t)&s_rxbuf[0][0];

    if ((buff == NULL) ||
        (ofs > (HWVAL_ETH_RX_BUFFERS - 1U) * ETH_RX_BUF_SIZE)) {
        return;
    }
    ETH_BufferTypeDef *b = &s_rxstruct[ofs / ETH_RX_BUF_SIZE];
    b->buffer = buff;
    b->len    = Length;
    b->next   = NULL;
    if (((ETH_BufferTypeDef *)*pStart) == NULL) {
        *pStart = b;
    }
    if (((ETH_BufferTypeDef *)*pEnd) != NULL) {
        ((ETH_BufferTypeDef *)*pEnd)->next = b;
    }
    *pEnd = b;
}

/* ------------------------------------------------------------------ TCP/UDP */
static err_t tcp_recv_echo(void *arg, struct tcp_pcb *pcb, struct pbuf *p,
                           err_t err)
{
    (void)arg;
    if (p == NULL) {                            /* peer closed            */
        tcp_close(pcb);
        return ERR_OK;
    }
    if (err == ERR_OK) {
        err = tcp_write(pcb, p->payload, p->len, TCP_WRITE_FLAG_COPY);
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return err;
}

static err_t tcp_accept_echo(void *arg, struct tcp_pcb *npcb, err_t err)
{
    (void)arg;
    tcp_arg(npcb, NULL);
    tcp_recv(npcb, tcp_recv_echo);
    return err;
}

static void tcp_echo_init(void)
{
    struct tcp_pcb *l = tcp_new_ip_type(IPADDR_TYPE_V4);

    if (l == NULL) { return; }
    if (tcp_bind(l, IP_ANY_TYPE, HWVAL_NET_TCP_PORT) != ERR_OK) {
        tcp_close(l);
        return;
    }
    l = tcp_listen(l);
    if (l == NULL) { return; }
    tcp_accept(l, tcp_accept_echo);
    s_tcp_echo_up = 1;
}

static void udp_recv_echo(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    if (p != NULL) {
        udp_sendto(pcb, p, addr, port);
        pbuf_free(p);
    }
}

static void udp_echo_init(void)
{
    struct udp_pcb *u = udp_new_ip_type(IPADDR_TYPE_V4);

    if (u == NULL) { return; }
    if (udp_bind(u, IP_ANY_TYPE, HWVAL_NET_UDP_PORT) != ERR_OK) {
        udp_remove(u);
        return;
    }
    udp_recv(u, udp_recv_echo, NULL);
    s_udp_echo_up = 1;
}

/* ------------------------------------------------------------------ netif  */
static err_t linkoutput(struct netif *netif, struct pbuf *p);

static err_t linkoutput(struct netif *netif, struct pbuf *p);

static err_t low_level_init(struct netif *netif)
{
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, s_mac, 6U);
    netif->mtu = 1500;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

    heth.Instance = ETH;
    heth.Init.MACAddr        = s_mac;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.TxDesc         = s_dmatx;
    heth.Init.RxDesc         = s_dmarx;
    heth.Init.RxBuffLen      = ETH_RX_BUF_SIZE;
    if (HAL_ETH_Init(&heth) != HAL_OK) {
        return ERR_IF;
    }
    netif->linkoutput = linkoutput;
    if (s_no_mac != 0) {
        /* loopback-only (ETH-008 in-chip leg): no ETH MAC involved */
        netif_set_up(netif);
        netif_set_link_up(netif);
        return ERR_OK;
    }
    if (HAL_ETH_Start(&heth) != HAL_OK) { return ERR_IF; }
    netif_set_up(netif);
    netif_set_link_up(netif);
    return ERR_OK;
}

static err_t linkoutput(struct netif *netif, struct pbuf *p)
{
    ETH_TxPacketConfigTypeDef cfg;
    struct pbuf *q;
    uint32_t len = 0U;

    (void)netif;
    memset(s_txbuf, 0, sizeof(s_txbuf));
    for (q = p; (q != NULL) && (len < sizeof(s_txbuf)); q = q->next) {
        memcpy(&s_txbuf[len], q->payload, q->len);
        len += q->len;
    }
    memset(&cfg, 0, sizeof(cfg));
    cfg.TxBuffer[0].buffer = s_txbuf;
    cfg.TxBuffer[0].len    = len;
    cfg.TxBuffer[0].next   = NULL;
    cfg.Length             = len;
    cfg.CRCPadCtrl         = ETH_CRC_PAD_INSERT;
    cfg.ChecksumCtrl       = ETH_CHECKSUM_DISABLE;
    return (HAL_ETH_Transmit(&heth, &cfg, 20U) == HAL_OK) ?
           ERR_OK : ERR_IF;
}

/* --------------------------------------------------------------- bring-up  */
static int eth_phyneg_run(void)
{
    uint32_t val = 0U;
    uint32_t tmo = 3000U;

    /* LAN8742A auto-negotiation, advert 100M/FD (ETH-006 measures the
     * negotiated state live in the bench case)                          */
    HAL_ETH_WritePHYRegister(&heth, HWVAL_PHY_ADDR, HWVAL_PHY_BCR,
                             HWVAL_PHY_ANEG_100FD);
    while (tmo-- > 0U) {
        HAL_ETH_ReadPHYRegister(&heth, HWVAL_PHY_ADDR, HWVAL_PHY_BMSR, &val);
        if ((val & HWVAL_PHY_BMSR_LINK) != 0U) {
            ETH_MACConfigTypeDef m;
            HAL_ETH_GetMACConfig(&heth, &m);
            m.DuplexMode = ETH_FULLDUPLEX_MODE;
            m.Speed      = ETH_SPEED_100M;
            HAL_ETH_SetMACConfig(&heth, &m);
            return 0;                           /* link up                */
        }
        vTaskDelay(1);
    }
    return -1;                                  /* no cable/peer          */
}

int hwval_net_start(uint32_t flags)
{
    ip4_addr_t ip, nm, gw;

    s_up = s_tcp_echo_up = s_udp_echo_up = 0;
    for (uint32_t i = 0U; i < HWVAL_ETH_RX_BUFFERS; i++) { s_rx_busy[i] = 0U; }
    memset(&s_netif, 0, sizeof(s_netif));
    lwip_init();

    if ((flags & HWVAL_NET_F_NO_MAC) == 0U) {
        if (eth_phyneg_run() != 0) {
            (void)HAL_ETH_DeInit(&heth);
            return -1;                          /* no link (ETH-006 gate) */
        }
    }

    IP4_ADDR(&ip, 0U, 0U, 0U, 0U);
    IP4_ADDR(&nm, 0U, 0U, 0U, 0U);
    IP4_ADDR(&gw, 0U, 0U, 0U, 0U);
    if (netif_add(&s_netif, &ip, &nm, &gw, NULL, low_level_init,
                  ethernet_input) == NULL) {
        if ((flags & HWVAL_NET_F_NO_MAC) == 0U) { HAL_ETH_Stop(&heth); }
        return -2;
    }
    netif_set_default(&s_netif);
    dhcp_start(&s_netif);

    uint32_t tmo = HWVAL_NET_DHCP_TIMEOUT_MS;
    while ((!dhcp_supplied_address(&s_netif)) && (tmo != 0U)) {
        uint32_t chunk = (tmo > 10U) ? 10U : tmo;
        hwval_net_poll(chunk);
        tmo -= chunk;
    }
    if (!dhcp_supplied_address(&s_netif)) {
        /* static fallback (direct bench cable, no server) */
        dhcp_stop(&s_netif);
        ip4_addr_set_u32(&ip, HWVAL_NET_STATIC_IP);
        ip4_addr_set_u32(&nm, HWVAL_NET_STATIC_NM);
        ip4_addr_set_u32(&gw, HWVAL_NET_STATIC_GW);
        netif_set_addr(&s_netif, &ip, &nm, &gw);
    }
    (void)ip4addr_ntoa_r(netif_ip4_addr(&s_netif), s_ip_str,
                         sizeof(s_ip_str));

    tcp_echo_init();
    udp_echo_init();
    s_up = 1;
    return 0;
}

/* --------------------------------------------------------------- polling   */
void hwval_net_poll(uint32_t ms)
{
    if (heth.gState != HAL_ETH_STATE_STARTED) {
        sys_check_timeouts();
        if (s_up != 0) { netif_poll(&s_netif); }   /* loopback frames     */
        return;
    }
    /* drain one burst of pending frames, drive the timers, wait the
     * remainder of the budget (the echo servers need the timer drive) */
    for (;;) {
        ETH_BufferTypeDef *pfirst = NULL;

        if (HAL_ETH_ReadData(&heth, (void **)&pfirst) != HAL_OK) {
            break;
        }
        uint32_t len = heth.RxDescList.RxDataLength;
        struct pbuf *p = pbuf_alloc(PBUF_RAW,
                                    (u16_t)(len + ETH_PAD_SIZE),
                                    PBUF_POOL);
        if ((p != NULL) && (p->len == (len + ETH_PAD_SIZE))) {
            memcpy((uint8_t *)p->payload + ETH_PAD_SIZE,
                   pfirst->buffer, len);
            s_netif.input(p, &s_netif);         /* ethernet_input         */
        } else if (p != NULL) {
            pbuf_free(p);
        }
        for (ETH_BufferTypeDef *b = pfirst; b != NULL; ) {
            ETH_BufferTypeDef *n = b->next;
            uint32_t ofs = (uint32_t)b->buffer -
                           (uint32_t)&s_rxbuf[0][0];
            if (ofs <= (HWVAL_ETH_RX_BUFFERS - 1U) * ETH_RX_BUF_SIZE) {
                s_rx_busy[ofs / ETH_RX_BUF_SIZE] = 0U;
            }
            b = n;
        }
    }
    sys_check_timeouts();                       /* DHCP + TCP timers      */
    netif_poll(&s_netif);                       /* loopback frames        */
    uint32_t left = ms;
    while (left != 0U) {
        uint32_t chunk = (left > 10U) ? 10U : left;
        vTaskDelay(chunk);
        left -= chunk;
        sys_check_timeouts();
    }
}

void hwval_net_stop(void)
{
    if (heth.gState == HAL_ETH_STATE_STARTED) {
        HAL_ETH_Stop(&heth);
    }
    s_up = s_tcp_echo_up = s_udp_echo_up = 0;
}

int         hwval_net_up(void)             { return s_up; }
int         hwval_net_tcp_echo_ready(void) { return s_tcp_echo_up & s_up; }
int         hwval_net_udp_echo_ready(void) { return s_udp_echo_up & s_up; }
const char *hwval_net_ip(void)             { return s_ip_str; }

/* ------------------------------------------------ ETH-007 self-check ------ */
static u8_t raw_icmp_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                           const ip_addr_t *addr)
{
    (void)arg; (void)pcb; (void)addr;
    pbuf_free(p);
    return 1;                                   /* consumed               */
}

/* ICMP echo request -> our own address (loopback netif path). */
int hwval_net_icmp_loopback(uint32_t echoes)
{
    struct raw_pcb *r = raw_new_ip_type(IPADDR_TYPE_V4, IP_PROTO_ICMP);
    ip4_addr_t dst;
    int ok = 0;

    if (r == NULL) { return -1; }
    (void)raw_recv(r, raw_icmp_recv, NULL);
    ip4_addr_copy(dst, *netif_ip4_addr(&s_netif));
    for (uint32_t i = 0U; i < echoes; i++) {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 40U, PBUF_RAM);
        if (p == NULL) { break; }
        memset(p->payload, 0, 40U);
        u8_t *pl = (u8_t *)p->payload;
        pl[0] = 8U;                             /* type: echo request     */
        pl[1] = 0U;
        pl[2] = 0U; pl[3] = 0U;                 /* checksum (filled next) */
        pl[4] = (u8_t)(i & 0xFFU);              /* id                     */
        pl[5] = (u8_t)((i >> 8U) & 0xFFU);
        pl[6] = 0U; pl[7] = 0U;                 /* seq                    */
        { u16_t cs = inet_chksum(p->payload, 40U);
          pl[2] = (u8_t)(cs >> 8U); pl[3] = (u8_t)(cs & 0xFFU); }
        if (raw_sendto(r, p, &dst) == ERR_OK) {
            hwval_net_poll(2U);                 /* loops back via INPUT   */
            ok++;
        }
        pbuf_free(p);
    }
    raw_remove(r);
    return (ok > 0) ? 0 : -1;
}

/* ------------------------------------------------ ETH-008 self-check ------ */
/* N bytes through the raw TCP echo server over the loopback netif. */
int hwval_net_tcp_loopback_bytes(uint32_t bytes, uint32_t *done)
{
    struct tcp_pcb *c = tcp_new_ip_type(IPADDR_TYPE_V4);
    ip4_addr_t dst;
    uint32_t sent = 0U;
    u8_t chunk[16];

    if ((c == NULL) || (done == NULL)) {
        if (c != NULL) { tcp_close(c); }
        return -1;
    }
    ip4_addr_copy(dst, *netif_ip4_addr(&s_netif));
    if (tcp_connect(c, &dst, HWVAL_NET_TCP_PORT, NULL) != ERR_OK) {
        tcp_close(c);
        return -2;
    }
    memset(chunk, 0xA5U, sizeof(chunk));
    *done = 0U;
    while ((sent < bytes) && (sent < 100000U)) {
        err_t e = tcp_write(c, chunk, sizeof(chunk), TCP_WRITE_FLAG_COPY);
        if (e == ERR_OK) {
            sent += sizeof(chunk);
        } else if (e != ERR_MEM) {
            break;                              /* reset/refused          */
        }
        hwval_net_poll(1U);                     /* drive the stack        */
    }
    *done = sent;
    tcp_close(c);
    hwval_net_poll(10U);
    return 0;
}