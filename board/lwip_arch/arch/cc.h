/**
 * @file    cc.h
 * @brief   LwIP compiler/architecture layer for the NO_SYS build.
 * @details Required by lwip/arch.h. Diagnostics go to the VCP console;
 *          assertions print and spin so the IWDG (GEN-009) recovers the board.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-03
 * @srs     ETH-007, ETH-008 (LwIP from the SDK; CON-002 as-shipped)
 */

#ifndef __HWVAL_LWIP_CC_H__
#define __HWVAL_LWIP_CC_H__

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Cortex-M7 byte order (little endian, DS11532). */
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234 /**< Little-endian marker. */
#define BIG_ENDIAN    4321 /**< Big-endian marker. */
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN /**< Target byte order. */
#endif

#define PACK_STRUCT_BEGIN                              /**< No packed prefix (GCC). */
#define PACK_STRUCT_STRUCT __attribute__((__packed__)) /**< GCC packed attribute. */
#define PACK_STRUCT_END                                /**< No packed suffix (GCC). */
/** Packed struct field; no wrapper needed on GCC. @param x Field declaration. */
#define PACK_STRUCT_FIELD(x) x

#if LWIP_DEBUG
/** Print an LwIP diagnostic. @param x Parenthesised printf arguments. */
#define LWIP_PLATFORM_DIAG(x) printf x
#else
/** Discard an LwIP diagnostic. @param x Parenthesised printf arguments. */
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#endif

/**
 * Print an LwIP assertion and spin until the IWDG resets the board.
 * @param x Assertion message.
 */
#define LWIP_PLATFORM_ASSERT(x) do {                                     \
    printf("LWIPASSERT %s:%d: %s\r\n", __FILE__, (int)__LINE__, (char *)(x)); \
    for (;;) { ; }                                                        \
} while (0)

/**
 * Declare LwIP pool/heap memory; keeps .bss placement (CPU-only, DTCM).
 * @param variable_name Array name.
 * @param size          Requested size, bytes.
 */
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) \
    uint8_t variable_name[LWIP_MEM_ALIGN_BUFFER(size)]
/**
 * Round a size up to MEM_ALIGNMENT.
 * @param size Size, bytes.
 * @return Aligned size, bytes.
 */
#define LWIP_MEM_ALIGN_BUFFER(size) (((size) + MEM_ALIGNMENT - 1U) & \
                                     ~(MEM_ALIGNMENT - 1U))

/** Saved interrupt state for SYS_ARCH_PROTECT. */
typedef uint32_t sys_prot_t;

#endif /* __HWVAL_LWIP_CC_H__ */
