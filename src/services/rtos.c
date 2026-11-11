/*
 * rtos.c - FreeRTOS bring-up and hooks (FRT-003..FRT-010)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  STM32F767ZIT6, FreeRTOS ARM_CM7 GCC port (FRT-001)
 * SRS:     section 5. SysTick is the kernel tick (FRT-002); the HAL keeps
 *          TIM6 (GEN-012). Both handlers coexist: the port defines
 *          SysTick_Handler -> xPortSysTickHandler; TIM6 has its own vector.
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "services/rtos.h"
#include "app/test_core.h"
#include "app/cli.h"
#include "bsp/uart3.h"
#include "app/boot_slot.h"
#include "services/fault.h"
#include "app/version.h"
#include "bsp/iwdg.h"
#include "app/settings.h"
#include "bsp/debug_uart.h"

static void hwval_panic_report(void);

/* Panic marker for hooks that end in a reset; reported at the next boot
 * from .noinit. */
#define HWVAL_PANIC_MAGIC   0x50414E49U /* "PANI" */
#define HWVAL_PANIC_OVERFLOW 1U
#define HWVAL_PANIC_MALLOC    2U

__attribute__((section(".noinit")))
static struct {
    uint32_t magic;
    uint32_t code;
} s_panic;

void hwval_rtos_report_panic(void)
{
    hwval_panic_report();
}

void hwval_dwt_stats_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR   = 0xC5ACCE55U; /* CM7 r1p0 lock access register */
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t hwval_dwt_now(void) { return DWT->CYCCNT; }

const char *hwval_freertos_version(void) { return HWVAL_FREERTOS_VERSION; }
uint32_t hwval_heap_free(void)     { return (uint32_t)xPortGetFreeHeapSize(); }
uint32_t hwval_heap_min_free(void) { return (uint32_t)xPortGetMinimumEverFreeHeapSize(); }

const char *hwval_current_task_name(void)
{
    return (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        ? pcTaskGetName(NULL) : "(nosched)";
}

/* ---- hooks -------------------------------------------------------------- */

/* FRT-004: print the offending task name, record, reset (stack is suspect;
 * staying alive is not an option). */
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    char msg[80];

    s_panic.magic = HWVAL_PANIC_MAGIC;
    s_panic.code  = HWVAL_PANIC_OVERFLOW;
    (void)snprintf(msg, sizeof(msg),
                   "PANIC stack-overflow task=%s -> reset\r\n",
                   (task_name != NULL) ? task_name : "?");
    hwval_uart3_write(msg, 0U);
    NVIC_SystemReset();
}

/* FRT-005. FreeRTOS V10 hooks carry no requested size (CL-012); print the
 * free and minimum-ever-free heap instead. */
void vApplicationMallocFailedHook(void)
{
    char msg[96];

    s_panic.magic = HWVAL_PANIC_MAGIC;
    s_panic.code  = HWVAL_PANIC_MALLOC;
    (void)snprintf(msg, sizeof(msg),
                   "PANIC malloc-failed free=%lu min-ever=%lu -> reset\r\n",
                   (unsigned long)xPortGetFreeHeapSize(),
                   (unsigned long)xPortGetMinimumEverFreeHeapSize());
    hwval_uart3_write(msg, 0U);
    NVIC_SystemReset();
}

void hwval_panic_report(void)
{
    char msg[80];

    if (s_panic.magic != HWVAL_PANIC_MAGIC) {
        return;
    }
    (void)snprintf(msg, sizeof(msg),
                   "boot: previous panic code=%lu\r\n",
                   (unsigned long)s_panic.code);
    hwval_uart3_write(msg, 0U);
}

/* ---- tasks (FRT-010) ------------------------------------------------------ */

static void task_cli(void *arg);      /* in tasks.c */
static void task_runner(void *arg);
static void task_heartbeat(void *arg);
static void task_watchdog(void *arg);

void hwval_rtos_start(void)
{
    hwval_fault_set_taskname_hook(hwval_current_task_name);
    hwval_test_runner_init();

    if (xTaskCreate(task_cli, "cli", 1024, NULL, 3, NULL) != pdPASS) {
        configASSERT(!"create cli");
    }
    if (xTaskCreate(task_runner, "runner", 4096, NULL, 2, NULL) != pdPASS) {
        configASSERT(!"create runner");
    }
    if (xTaskCreate(task_heartbeat, "hb", 512, NULL, 1, NULL) != pdPASS) {
        configASSERT(!"create hb");
    }
    if (xTaskCreate(task_watchdog, "wdg", 512, NULL, 1, NULL) != pdPASS) {
        configASSERT(!"create wdg");
    }
    if (hwval_dbg_task_create() != 0) {   /* DUART-002 */
        configASSERT(!"create dbg");
    }
    hwval_dwt_stats_init();
    vTaskStartScheduler();

    /* No heap left? This must never return: */
    hwval_fault_assert(__FILE__, __LINE__);
}

/* Runner: executes the boot suite once, then jobs submitted by the CLI or
 * link mode (section 7 / LINK-005).  In link mode the boot suite is skipped
 * so the GUI owns the test schedule.  While BOOT runs this slot on trial no
 * test may run: several reset the MCU, which BOOT would count as a failed
 * trial.  Jobs submitted meanwhile wait until the slot is confirmed. */
static void task_runner(void *arg)
{
    (void)arg;
    while (hwval_boot_trial_pending()) {
        hwval_task_report(HWVAL_TASK_RUNNER);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (g_set.mode != HWVAL_MODE_LINK) {
        hwval_test_all();
    }
    for (;;) {
        hwval_task_report(HWVAL_TASK_RUNNER);
        (void)hwval_test_runner_poll(1000U);
    }
}

/* Heartbeat (FRT-012): LD1 1 Hz idle, 5 Hz while a test runs, LD3 after a
 * failure — the states arrive with the test framework; use them now. */
static void task_heartbeat(void *arg)
{
    (void)arg;
    GPIO_InitTypeDef led = {
        .Pin = HWVAL_LED1_PIN | HWVAL_LED3_PIN, .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW
    };
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_Init(HWVAL_LED1_PORT, &led);

    for (;;) {
        const uint32_t period = hwval_test_running() ? 100U : 500U;
        hwval_task_report(HWVAL_TASK_HB);
        HAL_GPIO_TogglePin(HWVAL_LED1_PORT, HWVAL_LED1_PIN);
        HAL_GPIO_WritePin(HWVAL_LED3_PORT, HWVAL_LED3_PIN,
                          hwval_test_failed() ? GPIO_PIN_SET : GPIO_PIN_RESET);
        vTaskDelay(period);
    }
}

/* Watchdog (FRT-013 + GEN-009): dedicated lowest-priority task refreshes
 * IWDG while the Runner shows progress. If the Runner hangs longer than
 * the current test's timeout + 5 s, stop kicking: IWDG resets the MCU and
 * the boot path reports it.  It also runs the BOOT trial health check; a
 * failed check stops the kicks so BOOT counts a failed trial. */
static void task_watchdog(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(500);

    for (;;) {
        hwval_task_report(HWVAL_TASK_WDG);
        hwval_boot_health_poll();
        if (!hwval_test_runner_progress_expired() && !hwval_boot_stop_kick()) {
            hwval_iwdg_kick();
        }
        vTaskDelay(period);
    }
}

/* CLI task (FRT-010, section 6): line editor + commands. */
static void task_cli(void *arg)
{
    hwval_cli_task(arg);
}