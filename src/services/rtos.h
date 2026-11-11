/*
 * rtos.h - FreeRTOS integration points (hooks, runtime stats, task names)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     FRT-003, FRT-004, FRT-005, FRT-007, FRT-009, FRT-010
 */

#ifndef HWVAL_RTOS_H
#define HWVAL_RTOS_H

#include <stdint.h>

/* Enable DWT cycle counter as the run-time statistics source (FRT-007).
 * The DWT also serves the cycle measurements of section 8. */
void hwval_dwt_stats_init(void);

/* Create the four fixed tasks and start the scheduler (FRT-010). */
void hwval_rtos_start(void);

/* Version string of the kernel actually linked (CL-011). */
const char *hwval_freertos_version(void);

/* Boot-time report of hook panics recorded before a reset (FRT-004/005). */
void hwval_rtos_report_panic(void);

/* Current task name for fault reports (GEN-008) and the CLI. */
const char *hwval_current_task_name(void);

/* Free heap / minimum ever free (FRT-005) for the tasks/report commands. */
uint32_t hwval_heap_free(void);
uint32_t hwval_heap_min_free(void);

#endif /* HWVAL_RTOS_H */