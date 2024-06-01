/**
 * @file    lwipopts.h
 * @brief   LwIP configuration for the HWVAL network stack.
 * @details NO_SYS = 1: LwIP is polled from the Runner task (the cases call
 *          net_lwip_poll()), so SYS_ARCH critical sections reduce to BASEPRI
 *          sections. ETH DMA buffers live in .sram1 (non-cacheable) and the
 *          descriptors in .noncacheable; LwIP's own pools stay in DTCM .bss,
 *          sized to keep ~2 KB margin above the NFR-004 DTCM floor.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-03
 * @srs     ETH-007 (LwIP from the SDK, DHCP + static fallback, ICMP, TCP/UDP
 *          echo), ETH-008 (TCP throughput echo target), NFR-004
 */

#ifndef HWVAL_LWIPOPTS_H
#define HWVAL_LWIPOPTS_H

/* ---------- OS ---------- */
#define NO_SYS                      1 /**< Polled, no RTOS integration. */
#define SYS_LIGHTWEIGHT_PROT        1 /**< Use sys_arch_protect/unprotect. */

/* ---------- memory ---------- */
#define MEM_ALIGNMENT               4    /**< Heap alignment, bytes. */
#define MEM_SIZE                    2048 /**< LwIP heap, bytes. */

#define MEMP_NUM_PBUF               4  /**< PBUF_ROM/REF pbufs. */
#define MEMP_NUM_UDP_PCB            2  /**< UDP PCBs. */
#define MEMP_NUM_TCP_PCB            4  /**< Active TCP PCBs. */
#define MEMP_NUM_TCP_PCB_LISTEN     2  /**< Listening TCP PCBs. */
#define MEMP_NUM_TCP_SEG            24 /**< Queued TCP segments. */
#define MEMP_NUM_SYS_TIMEOUT        6  /**< Concurrent sys timeouts. */

/* ---------- pbuf ---------- */
#define PBUF_POOL_SIZE              3    /**< Pool pbufs. */
#define PBUF_POOL_BUFSIZE           1524 /**< Pool pbuf payload, bytes. */
#define ETH_PAD_SIZE                2    /**< Pad so the IP header is 4-byte aligned. */

/* ---------- IPv4 / protocols ---------- */
#define LWIP_IPV4                   1 /**< IPv4 on. */
#define LWIP_IPV6                   0 /**< IPv6 off. */
#define LWIP_ARP                    1 /**< ARP on. */
#define LWIP_ETHERNET               1 /**< Ethernet support on. */
#define LWIP_ICMP                   1 /**< ICMP (ping) on. */
#define LWIP_IGMP                   0 /**< IGMP off. */
#define LWIP_AUTOIP                 0 /**< AutoIP off. */
#define LWIP_DHCP                   1 /**< DHCP client on. */
#define DHCP_DOES_ARP_CHECK         0 /**< No ARP probe before claiming. */
#define LWIP_TCP                    1 /**< TCP on. */
#define LWIP_UDP                    1 /**< UDP on. */
#define LWIP_RAW                    1 /**< Raw API on. */
#define LWIP_DNS                    0 /**< DNS off. */
#define LWIP_NETIF_LOOPBACK         1 /**< Loopback for the ETH-008 in-chip leg. */
#define LWIP_CHECKSUM_ON_COPY       1 /**< Checksum while copying. */

/* ---------- TCP ---------- */
#define TCP_TTL                     64            /**< TCP TTL. */
#define TCP_MSS                     (1500 - 40)   /**< MSS, bytes. */
#define TCP_SND_BUF                 (2 * TCP_MSS) /**< Send buffer, bytes. */
#define TCP_WND                     (1 * TCP_MSS) /**< Receive window, bytes. */
#define TCP_QUEUE_OOSEQ             0             /**< Drop out-of-order segments. */
#define TCP_MSL                     5000UL        /**< Maximum segment lifetime, ms. */
#define LWIP_TCP_KEEPALIVE          0             /**< No keepalive. */

/* ---------- misc ---------- */
#define LWIP_STATS                  0 /**< No statistics. */
#define LWIP_NETIF_STATUS_CALLBACK  0 /**< No status callback. */
#define LWIP_NETIF_LINK_CALLBACK    0 /**< No link callback. */
#define LWIP_NETCONN                0 /**< No netconn API. */
#define LWIP_SOCKET                 0 /**< No socket API. */
#define LWIP_PROVIDE_ERRNO          0 /**< Use the C library errno. */
#define LWIP_DEBUG                  0 /**< Debug output off. */

#endif /* HWVAL_LWIPOPTS_H */
