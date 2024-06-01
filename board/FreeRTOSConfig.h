/**
 * @file    FreeRTOSConfig.h
 * @brief   Project-owned FreeRTOS kernel configuration.
 * @author  Sandesh Ghimire <sandesh@soccentric.com>
 * @date    2026-11-02
 * @srs     FRT-001..FRT-009; FreeRTOS V10.2.x shipped in the image (CL-011)
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/**
 * FreeRTOS version that actually runs (CL-011).
 * @note The SRS says V10.2.1; CubeF7 v1.17.4 ships V10.2.0 and CON-002 forbids
 *       vendor-file changes.
 */
#define HWVAL_FREERTOS_VERSION "V10.2.0"

/* --- scheduler ----------------------------------------------------------- */
#define configUSE_PREEMPTION                    1 /**< Preemptive scheduler. */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1 /**< CLZ-based ready list. */
#define configUSE_TICKLESS_IDLE                 0 /**< Tickless idle off. */
#define configCPU_CLOCK_HZ                      216000000UL /**< Core clock, Hz (GEN-004). */
#define configTICK_RATE_HZ                      ((TickType_t)1000) /**< 1 ms tick (FRT-002). */
#define configMAX_PRIORITIES                    12 /**< Priorities 0..11. */
#define configMINIMAL_STACK_SIZE                ((uint16_t)256) /**< Idle stack, words. */
#define configMAX_TASK_NAME_LEN                 16 /**< Task name length incl. NUL. */
#define configUSE_16_BIT_TICKS                  0 /**< 32-bit TickType_t. */
#define configIDLE_SHOULD_YIELD                 1 /**< Idle yields to priority-0 tasks. */
#define configUSE_TIME_SLICING                  1 /**< Round-robin equal priorities. */

/* --- memory ------------------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION         0 /**< No static object API. */
#define configSUPPORT_DYNAMIC_ALLOCATION        1 /**< Heap-allocated objects. */
#define configTOTAL_HEAP_SIZE                   HWVAL_RTOS_HEAP_SIZE /**< From CMake (FRT-006). */
#define configAPPLICATION_ALLOCATED_HEAP        0 /**< Kernel owns the heap array. */

/* --- kernel objects (FRT-021) -------------------------------------------- */
#define configUSE_MUTEXES                       1 /**< Mutex API. */
#define configUSE_RECURSIVE_MUTEXES             1 /**< Recursive mutex API. */
#define configUSE_COUNTING_SEMAPHORES           1 /**< Counting semaphore API. */
#define configUSE_QUEUE_SETS                    0 /**< No queue sets. */
#define configQUEUE_REGISTRY_SIZE               0 /**< No queue registry. */
#define configUSE_TASK_NOTIFICATIONS            1 /**< Task notification API. */
#define configUSE_EVENT_GROUPS                  1 /**< Event group API. */

/* --- timers -------------------------------------------------------------- */
#define configUSE_TIMERS                        1 /**< Software timers. */
#define configTIMER_TASK_PRIORITY               2 /**< Timer service task priority. */
#define configTIMER_QUEUE_LENGTH                8 /**< Timer command queue depth. */
#define configTIMER_TASK_STACK_DEPTH            256 /**< Timer task stack, words. */

/* --- hooks (FRT-003..FRT-005) --------------------------------------------- */
#define configUSE_MALLOC_FAILED_HOOK            1 /**< Call vApplicationMallocFailedHook. */
#define configCHECK_FOR_STACK_OVERFLOW          2 /**< Pattern-based overflow check. */
#define configUSE_IDLE_HOOK                     0 /**< No idle hook. */
#define configUSE_TICK_HOOK                     0 /**< No tick hook. */

/* --- tracing/stats (FRT-007, FRT-025) ------------------------------------- */
#define configUSE_TRACE_FACILITY                1 /**< Trace fields and uxTaskGetSystemState. */
#define configGENERATE_RUN_TIME_STATS           1 /**< Per-task run-time counters. */
#define configUSE_STATS_FORMATTING_FUNCTIONS    1 /**< vTaskList/vTaskGetRunTimeStats. */

/** @brief Start the DWT cycle counter for run-time stats. */
extern void hwval_dwt_stats_init(void);

/**
 * @brief Read the run-time stats counter.
 * @return Current counter value.
 */
extern uint32_t hwval_dwt_now(void);

/** Configure the run-time stats time base (DWT). */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() hwval_dwt_stats_init()
/** Read the run-time stats time base. */
#define portGET_RUN_TIME_COUNTER_VALUE()         hwval_dwt_now()

/* --- stack validation ----------------------------------------------------- */
#define configRECORD_STACK_HIGH_ADDRESS         1 /**< Keep pxEndOfStack for checks. */
#define configSTACK_DEPTH_TYPE                  uint16_t /**< Stack depth type, words. */

/* --- interrupt priorities (FRT-008) ---------------------------------------- */
/* The F767 has 4 priority bits. The kernel and every ISR that calls a FromISR
 * API run at priority number >= 5 (less urgent): TIM6 = 15, USART3 = 6;
 * test ISRs are configured by their tests. */
#define HWVAL_NVIC_PRIO_BITS                    4U /**< NVIC priority bits on the F767. */
#define configPRIO_BITS                         HWVAL_NVIC_PRIO_BITS /**< Priority bits. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15 /**< Lowest priority number. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5 /**< Highest FromISR priority. */
/** Kernel (PendSV/SysTick) priority, shifted into the NVIC field. */
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
/** BASEPRI mask for kernel critical sections, shifted into the NVIC field. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* --- debug (FRT-003) -------------------------------------------------------- */
/**
 * @brief Record a failed configASSERT and stop.
 * @param[in] file Source file name.
 * @param[in] line Source line.
 */
extern void hwval_fault_assert(const char *file, int line);

/**
 * Kernel assertion; reports through @ref hwval_fault_assert.
 * @param x Condition expected to be non-zero.
 * @warning Expands to a bare @c if statement, not a do/while block.
 */
#define configASSERT(x) if ((x) == 0) { hwval_fault_assert(__FILE__, __LINE__); }

/* --- port mapping (CM7 GCC port, FRT-001) ----------------------------------- */
#define vPortSVCHandler    SVC_Handler     /**< Port SVC handler as the vector name. */
#define xPortPendSVHandler PendSV_Handler  /**< Port PendSV handler as the vector name. */
#define xPortSysTickHandler SysTick_Handler /**< Port SysTick handler as the vector name. */
/* FRT-009: Cortex-M7 with lazy FP stacking; the port saves the FPU context
 * for tasks that use it (verified by the RTOS.021.FPU test). */

/* --- optional API includes --------------------------------------------------- */
#define INCLUDE_vTaskPrioritySet            1 /**< Include vTaskPrioritySet. */
#define INCLUDE_uxTaskPriorityGet           1 /**< Include uxTaskPriorityGet. */
#define INCLUDE_vTaskDelete                 1 /**< Include vTaskDelete. */
#define INCLUDE_vTaskSuspend                1 /**< Include vTaskSuspend. */
#define INCLUDE_vTaskDelayUntil             1 /**< Include vTaskDelayUntil. */
#define INCLUDE_vTaskDelay                  1 /**< Include vTaskDelay. */
#define INCLUDE_xTaskGetSchedulerState      1 /**< Include xTaskGetSchedulerState. */
#define INCLUDE_xTaskGetCurrentTaskHandle   1 /**< Include xTaskGetCurrentTaskHandle. */
#define INCLUDE_xTaskGetIdleTaskHandle      1 /**< Include xTaskGetIdleTaskHandle. */
#define INCLUDE_uxTaskGetStackHighWaterMark 1 /**< Include uxTaskGetStackHighWaterMark. */
#define INCLUDE_uxTaskGetSystemState        1 /**< Include uxTaskGetSystemState. */
#define INCLUDE_xTimerPendFunctionCall      1 /**< Include xTimerPendFunctionCall. */
#define INCLUDE_eTaskGetState               1 /**< Include eTaskGetState. */

#endif /* FREERTOS_CONFIG_H */
