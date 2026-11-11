/*
 * src/net_lwip.h - LwIP wrapper for the Ethernet group (ETH-007/008)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * SRS:     ETH-007 (LwIP from the SDK with DHCP + static fallback;
 *          ICMP; TCP/UDP echo servers), ETH-008 (TCP echo throughput).
 */

#ifndef HWVAL_NET_LWIP_H
#define HWVAL_NET_LWIP_H

#include <stdint.h>

#define HWVAL_NET_TCP_PORT  7U      /* TCP echo (like iperf discard? echo) */
#define HWVAL_NET_UDP_PORT  7U      /* UDP echo */

/* start flags (ETH-007 needs the link; ETH-008's loopback leg does not) */
#define HWVAL_NET_F_LINKREQ    1U    /* require PHY link, else -1          */
#define HWVAL_NET_F_NO_MAC     2U    /* loopback-only: skip ETH/PHY entirely */

/* Bring up: RMII + PHY autoneg (LAN8742A @ addr 0), LwIP init, DHCP
 * (HWVAL_NET_DHCP_TIMEOUT_MS) with static-IP fallback. Returns 0 on
 * success, -1 on no link (only with F_LINKREQ), -2 on stack failure. */
int hwval_net_start(uint32_t flags);

/* Poll the stack for up to ms ms (RX path + timers + DHCP). */
void hwval_net_poll(uint32_t ms);

/* Teardown: netif down + ETH stop (tests that follow run clean). */
void hwval_net_stop(void);

/* State queries for the test cases and the host report. */
int         hwval_net_up(void);     /* MAC started + link seen            */
const char *hwval_net_ip(void);     /* dotted string ("" when not up)     */
int         hwval_net_tcp_echo_ready(void);
int         hwval_net_udp_echo_ready(void);

/* Firmware leg of ETH-008: N bytes TCP echo over the loopback netif. */
int hwval_net_tcp_loopback_bytes(uint32_t bytes, uint32_t *done);
/* ICMP echo-request round trip over the loopback netif (ETH-007 self-check). */
int hwval_net_icmp_loopback(uint32_t echoes);

#endif /* HWVAL_NET_LWIP_H */