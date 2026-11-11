/*
 * fault.c - HardFault, MemManage, BusFault, UsageFault reporting (GEN-008)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  Cortex-M7 (Armv7-M), STM32F767ZIT6
 * SRS:     GEN-008, FRT-003, CORE-003..CORE-006, MPU-002 (fault recovery
 *          paths reuse these handlers), [PM0253] exception model.
 *
 * The naked wrappers load the correct stack frame pointer (MSP or PSP) and
 * branch into the shared reporter. The report goes to the polled VCP, so it
 * survives any state the fault was caused by.
 */

#include <stdio.h>
#include "stm32f7xx_hal.h"
#include "fault.h"
#include "uart3.h"

static hwval_taskname_fn_t s_taskname;
static volatile hwval_fault_action_t s_action = HWVAL_FAULT_HALT;

volatile int g_assert_line;
volatile const char *g_assert_file;

/* CORE-003..006/MPU-002: tests arm this to catch an expected fault: the
 * stacked PC is redirected to hwval_fault_recover_pc and the exception
 * returns instead of halting. */
volatile int          hwval_fault_recover;
/* The pointer itself is volatile: its store must not move past the access
 * that faults. */
void *volatile        hwval_fault_recover_pc;   /* &&label inside test   */
volatile uint32_t     hwval_fault_last_exc;     /* 3 hard, 4 mem, 5 bus, 6 usage */
volatile uint32_t     hwval_fault_cfsr_snap;

#define FAULT_RECORD_MAGIC 0x4856464CU /* "HVFL" */
__attribute__((section(".noinit")))
static hwval_fault_record_t s_record; /* survives the handler's reset */

void hwval_fault_record_write(const hwval_fault_snapshot_t *s)
{
    s_record.magic = FAULT_RECORD_MAGIC;
    s_record.count++;
    s_record.cfsr  = s->cfsr;
    s_record.hfsr  = s->hfsr;
    s_record.mmfar = s->mmfar;
    s_record.bfar  = s->bfar;
    s_record.pc    = s->pc;
    s_record.lr    = s->lr;
}

const hwval_fault_record_t *hwval_fault_record(void) { return &s_record; }

void hwval_fault_set_taskname_hook(hwval_taskname_fn_t fn) { s_taskname = fn; }
void hwval_fault_set_action(hwval_fault_action_t a)        { s_action = a; }
hwval_fault_action_t hwval_fault_get_action(void)          { return s_action; }

static void report(uint32_t exc, const hwval_fault_frame_t *f, uint32_t lr)
{
    hwval_fault_snapshot_t s;
    char line[224];

    s.cfsr  = SCB->CFSR;
    s.hfsr  = SCB->HFSR;
    s.mmfar = SCB->MMFAR;
    s.bfar  = SCB->BFAR;
    s.pc    = f->pc;
    s.lr    = f->lr;
    s.xpsr  = f->xpsr;
    hwval_fault_record_write(&s);

    const char *name = (s_taskname != NULL) ? s_taskname() : "(no rtos)";

    (void)snprintf(line, sizeof(line),
        "FAULT exc=%lu %s task=%s\r\n"
        "  CFSR=%08lX HFSR=%08lX MMFAR=%08lX BFAR=%08lX\r\n"
        "  PC=%08lX LR=%08lX XPSR=%08lX EXC_LR=%08lX\r\n",
        (unsigned long)exc,
        (exc == 3U) ? "HardFault" :
        (exc == 4U) ? "MemManage" :
        (exc == 5U) ? "BusFault"  : "UsageFault",
        name,
        (unsigned long)s.cfsr, (unsigned long)s.hfsr,
        (unsigned long)s.mmfar, (unsigned long)s.bfar,
        (unsigned long)s.pc,  (unsigned long)s.lr,
        (unsigned long)s.xpsr, (unsigned long)lr);
    hwval_uart3_write(line, 0U);
}

/* Report, then act per s_action (GEN-008): halt in WFI (bench debugging;
 * the IWDG (GEN-009) ends the hang if left), or reset. */
static void fault_common(uint32_t exc, const hwval_fault_frame_t *f, uint32_t lr)
{
    report(exc, f, lr);

    for (;;) {
        if (s_action == HWVAL_FAULT_RESET) {
            NVIC_SystemReset();
        }
        /* Halt path: deliberately do NOT refresh the IWDG — a fault halt
         * must end in a watchdog reset, whose boot path prints the fault
         * record kept in .noinit (GEN-010 + section 4 evidence). */
        __asm volatile ("wfi");
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "tst   lr, #4          \n"
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "mov   r1, lr          \n"
        "mov   r2, #3          \n"
        "bl    hwval_fault_enter \n"
        "bx    r0                \n");
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile (
        "tst   lr, #4          \n"
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "mov   r1, lr          \n"
        "mov   r2, #4          \n"
        "bl    hwval_fault_enter \n"
        "bx    r0                \n");
}

__attribute__((naked)) void BusFault_Handler(void)
{
    __asm volatile (
        "tst   lr, #4          \n"
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "mov   r1, lr          \n"
        "mov   r2, #5          \n"
        "bl    hwval_fault_enter \n"
        "bx    r0                \n");
}

__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile (
        "tst   lr, #4          \n"
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "mov   r1, lr          \n"
        "mov   r2, #6          \n"
        "bl    hwval_fault_enter \n"
        "bx    r0                \n");
}

/* NMI: used by CORE-007 (software-pended NMI) and CLK-011 (CSS path).
 * A return from NMI is legal, so record, report, return. */
volatile uint32_t hwval_nmi_count;

void NMI_Handler(void)
{
    hwval_nmi_count++;
    if (hwval_nmi_count <= 4U) {
        /* Report minimally; the frame is not available in NMI here. */
        hwval_uart3_puts("NMI (NMIPENDSET/CSS observer)");
    }
}

/* Entry point used by the naked wrappers above. */
uint32_t hwval_fault_enter(const hwval_fault_frame_t *frame, uint32_t lr,
                           uint32_t exc);

/* Returns the EXC_RETURN value (r0) so the naked wrapper can do 'bx r0';
 * on the recovery path the stacked PC was redirected and the exception
 * simply returns to the test. */
uint32_t hwval_fault_enter(const hwval_fault_frame_t *frame, uint32_t lr,
                           uint32_t exc)
{
    if (hwval_fault_recover != 0) {
        /* Expected fault under test: redirect the stacked PC to the
         * recovery point and return; exception return unwinds cleanly
         * ([PM0253] B1.5.8/9: stacked frame edit + EXC_RETURN). */
        hwval_fault_last_exc  = exc;
        hwval_fault_cfsr_snap = SCB->CFSR;
        ((hwval_fault_frame_t *)(uintptr_t)frame)->pc =
            (uint32_t)(uintptr_t)hwval_fault_recover_pc;
        hwval_fault_recover = 0;
        /* A test may fault from unprivileged thread mode (MPU-004).  Only
         * privileged code can clear nPRIV, so do it here, in the handler. */
        __set_CONTROL(__get_CONTROL() & ~CONTROL_nPRIV_Msk);
        __ISB();
        return lr;
    }
    fault_common(exc, frame, lr);
    return lr;
}

void hwval_fault_assert(const char *file, int line)
{
    char msg[128];

    /* Bench diagnostics (read with gdb/OpenOCD; also visible when the VCP
     * TX bridge is closed): */
    g_assert_line = line;
    g_assert_file = file;
    (void)snprintf(msg, sizeof(msg), "configASSERT %s:%d\r\n", file, line);
    hwval_uart3_write(msg, 0U);
    if (s_action == HWVAL_FAULT_RESET) {
        NVIC_SystemReset();
    }
    /* Halt: no IWDG refresh, so the watchdog resets and the boot path
     * reports the last fault record from .noinit (GEN-009 + GEN-008). */
    for (;;) {
        __asm volatile ("wfi");
    }
}