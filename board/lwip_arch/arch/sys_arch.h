/**
 * @file    sys_arch.h
 * @brief   Minimal LwIP sys_arch types for the NO_SYS build.
 * @details lwip/sys.h includes this header even when NO_SYS = 1; only the
 *          SYS_ARCH critical-section macros are used in that mode.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-03
 * @srs     ETH-007 (LwIP from the SDK)
 */

#ifndef __HWVAL_LWIP_SYS_ARCH_H__
#define __HWVAL_LWIP_SYS_ARCH_H__

#include "lwip/opt.h"

#if !NO_SYS
#error "HWVAL builds LwIP in polled NO_SYS mode only (ETH-007/008)"
#endif

#include <stdint.h>

typedef uint32_t sys_sem_t;      /**< Unused placeholder semaphore. */
typedef uint32_t sys_mutex_t;    /**< Unused placeholder mutex. */
typedef uint32_t sys_mbox_t;     /**< Unused placeholder mailbox. */
typedef uint32_t sys_thread_t;   /**< Unused placeholder thread. */
typedef void    *sys_mbox_msg_t; /**< Unused placeholder mailbox message. */
/** Saved interrupt state; sys_arch_protect/unprotect are in net_lwip.c. */
typedef uint32_t sys_prot_t;

#endif /* __HWVAL_LWIP_SYS_ARCH_H__ */
