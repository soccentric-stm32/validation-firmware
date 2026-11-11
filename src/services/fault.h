/*
 * fault.h - core fault reporting (GEN-008)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     GEN-008 (HardFault/MemManage/BusFault/UsageFault print CFSR,
 *          HFSR, MMFAR, BFAR, stacked PC/LR/xPSR and the current task name,
 *          then reset or halt per a CLI setting)
 */

#ifndef HWVAL_FAULT_H
#define HWVAL_FAULT_H

#include <stdint.h>

/* Stacked register frame at exception entry (Basic frame, 32 bytes). */
typedef struct {
    uint32_t r0, r1, r2, r3;
    uint32_t r12, lr, pc, xpsr;
} hwval_fault_frame_t;

/* Values captured by each handler. */
typedef struct {
    uint32_t cfsr, hfsr, mmfar, bfar, pc, lr, xpsr;
} hwval_fault_snapshot_t;

/* Last fault, kept in .noinit across the reset the handler performs. */
typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t cfsr, hfsr, mmfar, bfar, pc, lr;
} hwval_fault_record_t;

void hwval_fault_record_write(const hwval_fault_snapshot_t *s);
const hwval_fault_record_t *hwval_fault_record(void);

/* Optional hook returning the running task's name; installed by the
 * FreeRTOS layer in section 5 (FRT-003/FRT-011 also report through here). */
typedef const char *(*hwval_taskname_fn_t)(void);
void hwval_fault_set_taskname_hook(hwval_taskname_fn_t fn);

/* One of FAULT_ACTION_*, settable from the CLI (CLI-036 `set fault-action`). */
typedef enum { HWVAL_FAULT_HALT = 0, HWVAL_FAULT_RESET = 1 } hwval_fault_action_t;
void hwval_fault_set_action(hwval_fault_action_t action);
hwval_fault_action_t hwval_fault_get_action(void);

/* Fault-recovery for CORE/MPU tests: arm before triggering an expected
 * fault; the handler redirects the stacked PC to the label and returns. */
/* Place right after the access expected to fault.  Without an edge to the
 * recovery label GCC (-O2) treats it as unreachable and drops it, so
 * &&label no longer points at the recovery code (it resolved to the
 * function entry).  The empty asm goto keeps the label and its code. */
#define HWVAL_FAULT_RECOVER_POINT(label) \
    __asm__ goto ("" : : : "memory" : label)

extern volatile int      hwval_fault_recover;
extern void *volatile    hwval_fault_recover_pc;
extern volatile uint32_t hwval_fault_last_exc;   /* 3 hard 4 mem 5 bus 6 usage */
extern volatile uint32_t hwval_fault_cfsr_snap;
extern volatile int      g_assert_line;
extern volatile const char *g_assert_file;

/* configASSERT plumbing (FRT-003): print file/line on the console. */
void hwval_fault_assert(const char *file, int line);

#endif /* HWVAL_FAULT_H */