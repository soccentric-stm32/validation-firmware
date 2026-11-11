/*
 * cli.c - interactive/scriptable terminal on the ST-LINK VCP (section 6)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * Target:  NUCLEO-F767ZI, USART3 (PD8/PD9) 115200 8N1 (CLI-001)
 * SRS:     CLI-002 (IRQ RX ring >=512), CLI-003 (IRQ TX ring, no blocking),
 *          CLI-004 (baud + 10 s revert), CLI-005 (\r and \r\n in, \r\n out),
 *          CLI-010 (prompt, backspace, Ctrl-C, Ctrl-U, history >=10),
 *          CLI-011 (Tab completion), CLI-012 (mode script), CLI-013 (colour),
 *          CLI-014 (operator prompts with timeout),
 *          CLI-020..CLI-040 (commands).
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/cli.h"
#include "bsp/uart3.h"
#include "app/settings.h"
#include "services/rtos.h"
#include "app/version.h"
#include "services/fault.h"
#include "bsp/clock.h"
#include "bsp/flash_ops.h"
#include "bsp/reset_cause.h"
#include "bsp/iwdg.h"
#include "app/harness.h"
#include "app/test_core.h"
#include "protocol/link.h"
#include "bsp/debug_uart.h"

#ifndef HWVAL_CUBE_VERSION
#define HWVAL_CUBE_VERSION "unknown"
#endif

#define HWVAL_PROMPT       "hwval> "
#define HWVAL_LINE_MAX     128
#define HWVAL_HISTORY      12   /* CLI-010: at least 10 entries             */
#define HWVAL_MAX_ARGS     8
#define HWVAL_BAUD_REVERT_MS 10000U /* CLI-004                             */

static char     s_hist[HWVAL_HISTORY][HWVAL_LINE_MAX];
static int      s_hist_wr;     /* count of entries ever written            */
static int      s_hist_pos;    /* navigation cursor */
static uint32_t s_baud = HWVAL_VCP_BAUD;
static TickType_t s_baud_revert_deadline;
static int      s_baud_pending;

/* ---------------------------------------------------------------- colour */
static const char *ok_color(void)   { return g_set.color ? "\033[32m" : ""; }
static const char *bad_color(void)  { return g_set.color ? "\033[31m" : ""; }
static const char *warn_color(void) { return g_set.color ? "\033[33m" : ""; }
static const char *c_off(void)      { return g_set.color ? "\033[0m" : ""; }

/* ------------------------------------------------------------ helpers -- */
static void cli_line(const char *s)
{
    hwval_uart3_puts(s);
}

static void cli_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hwval_uart3_write(buf, 0U);
}

/* -------------------------------------------------------- baud (CLI-004) */
static void cli_set_baud(uint32_t baud)
{
    if (baud == s_baud) {
        return;
    }
    USART3->CR1 &= ~USART_CR1_UE;
    USART3->BRR = (HAL_RCC_GetPCLK1Freq() + (baud / 2U)) / baud;
    USART3->CR1 |= USART_CR1_UE;
    s_baud = baud;
    s_baud_pending = 1;
    s_baud_revert_deadline = xTaskGetTickCount() +
                             pdMS_TO_TICKS(HWVAL_BAUD_REVERT_MS);
}

/* baud getter lives in uart3.c */

/* Baud revert: called from the CLI loop with the current RX activity. */
static void cli_baud_revert_poll(void)
{
    if (!s_baud_pending) {
        return;
    }
    if (hwval_uart3_getc() >= 0) {
        s_baud_pending = 0; /* host is talking at the new rate */
        return;
    }
    if (xTaskGetTickCount() >= s_baud_revert_deadline) {
        cli_set_baud(HWVAL_VCP_BAUD); /* CLI-004: revert after 10 s silence */
        s_baud_pending = 0;
    }
}

/* ------------------------------------------------- operator prompts ----- */
static hwval_prompt_kind_t prompt_kind(const char *question)
{
    if (question == NULL) {
        return HWVAL_PROMPT_YES_NO;
    }
    if (strstr(question, "destructive") != NULL) {
        return HWVAL_PROMPT_DESTRUCTIVE;
    }
    if ((strstr(question, "B1") != NULL) || (strstr(question, "press") != NULL)) {
        return HWVAL_PROMPT_BUTTON;
    }
    if ((strstr(question, "equipment") != NULL) ||
        (strstr(question, "module") != NULL) ||
        (strstr(question, "cable") != NULL)) {
        return HWVAL_PROMPT_EQUIPMENT;
    }
    return HWVAL_PROMPT_YES_NO;
}

int hwval_cli_ask_yn(const char *question, uint32_t timeout_s)
{
    char line[8];

    if (g_set.mode == HWVAL_MODE_LINK) {
        int rc = hwval_link_prompt(prompt_kind(question), question, "y/n",
                                   timeout_s, line, sizeof(line));
        if (rc == 0) {
            return (line[0] == 'y' || line[0] == 'Y' ||
                    line[0] == 'p' || line[0] == 'P') ? 1 : 0;
        }
        if (rc == -1) {
            return -1;
        }
    }
    cli_printf("%s [y/n] (timeout %lus) ", question,
               (unsigned long)timeout_s);
    if (hwval_cli_ask_line(NULL, line, sizeof(line), timeout_s) < 0) {
        cli_line("timeout -> SKIP NO_OPERATOR");
        return -1;
    }
    return (line[0] == 'y' || line[0] == 'Y') ? 1 : 0;
}

int hwval_cli_ask_line(const char *question, char *out, size_t len,
                       uint32_t timeout_s)
{
    TickType_t deadline;
    size_t n = 0U;
    int echo;

    if (g_set.mode == HWVAL_MODE_LINK) {
        int rc = hwval_link_prompt(HWVAL_PROMPT_LINE, question, "",
                                   timeout_s, out, len);
        if (rc == 0) {
            return 0;
        }
        if (rc == -1) {
            return -1;
        }
    }
    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_s * 1000U);
    echo = (g_set.mode == HWVAL_MODE_INTERACTIVE);

    if (question != NULL) {
        cli_printf("%s", question);
    }
    out[0] = '\0';
    for (;;) {
        int c = hwval_uart3_getc();
        if (c < 0) {
            if (xTaskGetTickCount() >= deadline) {
                if (echo) { hwval_uart3_puts(""); }
                return -1;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if ((c == '\r') || (c == '\n')) {           /* CLI-005 */
            break;
        }
        if ((c == 0x7F) || (c == 0x08)) {           /* backspace */
            if (n > 0U) {
                n--;
                if (echo) { hwval_uart3_write("\b \b", 3U); }
            }
            continue;
        }
        if (((size_t)n + 1U) < len) {
            out[n++] = (char)c;
            if (echo) { hwval_uart3_write(&c, 1U); } /* local echo */
        }
    }
    out[n] = '\0';
    if (echo) {
        hwval_uart3_puts("");
    }
    return 0;
}

/* ------------------------------------------------------------- commands */
static void cmd_help(int argc, char **argv);
static void cmd_version(int argc, char **argv);
static void cmd_info(int argc, char **argv);
static void cmd_list(int argc, char **argv);
static void cmd_run(int argc, char **argv);
static void cmd_abort(int argc, char **argv);
static void cmd_results(int argc, char **argv);
static void cmd_report(int argc, char **argv);
static void cmd_harness(int argc, char **argv);
static void cmd_tasks(int argc, char **argv);
static void cmd_log(int argc, char **argv);
static void cmd_reset(int argc, char **argv);
static void cmd_mode(int argc, char **argv);
static void cmd_format(int argc, char **argv);
static void cmd_set(int argc, char **argv);
static void cmd_get(int argc, char **argv);
static void cmd_peek(int argc, char **argv);
static void cmd_poke(int argc, char **argv);
static void cmd_gpio(int argc, char **argv);
static void cmd_clock(int argc, char **argv);
static void cmd_baud(int argc, char **argv);

struct cli_cmd {
    const char *name;
    void (*fn)(int argc, char **argv);
    const char *args;
    const char *help;
};

static const struct cli_cmd k_cmds[] = {
    { "help",    cmd_help,    "[cmd]", "list commands or detailed help" },
    { "version", cmd_version, "",      "firmware + kernel versions" },
    { "info",    cmd_info,    "",      "chip identity, clocks, uptime" },
    { "list",    cmd_list,    "[group|level]", "list test cases" },
    { "run",     cmd_run,     "<id|group|all> [--flags]", "run tests" },
    { "abort",   cmd_abort,   "",      "stop the running test" },
    { "results", cmd_results, "[clear]", "show/clear session results" },
    { "report",  cmd_report,  "",      "summary report" },
    { "harness", cmd_harness, "",      "detect Appendix A loopback wires" },
    { "tasks",   cmd_tasks,   "",      "FreeRTOS tasks + run-time stats" },
    { "log",     cmd_log,     "error|warn|info|debug", "set log level" },
    { "reset",   cmd_reset,   "[hard]", "reboot (hard = via IWDG)" },
    { "mode",    cmd_mode,    "interactive|script|link", "terminal mode" },
    { "format",  cmd_format,  "text|json", "result output format" },
    { "set",     cmd_set,     "<key> <value>", "change a setting" },
    { "get",     cmd_get,     "<key>", "show a setting" },
    { "peek",    cmd_peek,    "<addr> [n]", "read memory/registers" },
    { "poke",    cmd_poke,    "<addr> <value> [--force]", "write memory" },
    { "gpio",    cmd_gpio,    "<pin> [in|out|high|low|pullup|pulldown|read]",
                            "manual pin control" },
    { "clock",   cmd_clock,   "",      "clock tree frequencies" },
    { "baud",    cmd_baud,    "<rate>", "change VCP baud rate (CLI-004)" },
};

#define HWVAL_NCMDS ((int)(sizeof(k_cmds) / sizeof(k_cmds[0])))

static void cmd_help(int argc, char **argv)
{
    if (argc >= 2) {
        for (int i = 0; i < HWVAL_NCMDS; i++) {
            if (strcmp(k_cmds[i].name, argv[1]) == 0) {
                cli_printf("%s %s: %s\r\n", k_cmds[i].name, k_cmds[i].args,
                           k_cmds[i].help);
                return;
            }
        }
        cli_printf("ERR unknown command '%s' (help lists all)\r\n", argv[1]);
        return;
    }
    for (int i = 0; i < HWVAL_NCMDS; i++) {
        cli_printf("%-8s %-34s %s\r\n", k_cmds[i].name, k_cmds[i].args,
                   k_cmds[i].help);
    }
}

static void cmd_version(int argc, char **argv)
{
    (void)argc; (void)argv;
    cli_printf("HWVAL v%s (git %s) built %s\r\n", hwval_version_string(),
               hwval_git_commit(), hwval_build_date());
    cli_printf("SRS %s | FreeRTOS %s | CubeF7 %s\r\n",
               hwval_srs_id(), hwval_freertos_version(), HWVAL_CUBE_VERSION);
    cli_printf("Target: Nucleo-F767ZI STM32F767ZIT6\r\n");
}

static const uint32_t k_bauds[] = { 9600, 19200, 38400, 57600, 115200,
                                    230400, 460800, 921600 }; /* CLI-004 */

static void cmd_baud(int argc, char **argv)
{
    if (argc < 2) {
        cli_printf("baud now %lu\r\n", (unsigned long)s_baud);
        return;
    }
    uint32_t want = (uint32_t)strtoul(argv[1], NULL, 10);
    for (unsigned i = 0; i < (sizeof(k_bauds) / sizeof(k_bauds[0])); i++) {
        if (k_bauds[i] == want) {
            cli_printf("switching to %lu baud (reverts after %ds silent)\r\n",
                       (unsigned long)want, (int)(HWVAL_BAUD_REVERT_MS / 1000U));
            vTaskDelay(pdMS_TO_TICKS(100)); /* let the line drain */
            cli_set_baud(want);
            return;
        }
    }
    cli_line("ERR unsupported baud (9600..921600)");
}

static void cmd_info(int argc, char **argv)
{
    hwval_clock_tree_t tree;
    const uint32_t devid = DBGMCU->IDCODE;
    const uint32_t rev = devid >> 16U;

    (void)argc; (void)argv;
    (void)printf("DEV_ID=%04lX REV_ID=%04lX rev=%s\r\n",
                 (unsigned long)(devid & 0xFFFU), (unsigned long)rev,
                 (rev == 0x1001U) ? "Z (SRS DEV-001)" : "?");
    (void)printf("CPUID implementer=%02lX part=%03lX alt=%lX\r\n",
                 (unsigned long)((SCB->CPUID >> 24U) & 0xFFU),
                 (unsigned long)((SCB->CPUID >> 4U) & 0xFFFU),
                 (unsigned long)(SCB->CPUID & 0xFU));
    (void)printf("flash=%lu KB package_reg=%08lX\r\n",
                 (unsigned long)hwval_flash_size_kb(),
                 (unsigned long)(*(const uint32_t *)0x1FF0F7E0U));
    (void)printf("UID=%08lX%08lX%08lX\r\n",
                 (unsigned long)(*(const uint32_t *)0x1FF0F420U),
                 (unsigned long)(*(const uint32_t *)0x1FF0F424U),
                 (unsigned long)(*(const uint32_t *)0x1FF0F428U));
    (void)printf("OPTCR=%08lX OPTCR1=%08lX\r\n",
                 (unsigned long)(*(const uint32_t *)0x40023C14U),
                 (unsigned long)(*(const uint32_t *)0x40023C18U));
    hwval_clock_get_tree(&tree);
    (void)printf("sysclk=%lu hclk=%lu pclk1=%lu pclk2=%lu\r\n",
                 (unsigned long)tree.sysclk_hz, (unsigned long)tree.hclk_hz,
                 (unsigned long)tree.pclk1_hz, (unsigned long)tree.pclk2_hz);
    (void)printf("apb1_tim=%lu apb2_tim=%lu pllq=%lu\r\n",
                 (unsigned long)tree.apb1_tim_hz, (unsigned long)tree.apb2_tim_hz,
                 (unsigned long)tree.pllq_hz);
    (void)printf("reset cause: %s\r\n", hwval_reset_cause_name(
                 hwval_reset_cause_flags()));
    (void)printf("uptime=%lu s | baud=%lu | %s mode\r\n",
                 (unsigned long)(HAL_GetTick() / 1000UL),
                 (unsigned long)s_baud, hwval_mode_name(g_set.mode));
}

static void cmd_list(int argc, char **argv)
{
    const char *filt = (argc >= 2) ? argv[1] : NULL;

    static const char lvl_char[3] = { 'S', 'L', 'E' };
    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const struct tfw_case *c = hwval_test_at(i);
        if (filt != NULL) {
            char lc[2] = { lvl_char[(int)c->level], '\0' };
            if ((strcmp(c->group, filt) != 0) && (strcmp(filt, lc) != 0)) {
                continue;
            }
        }
        cli_printf("%-14s %c%c %-6lu %-10s %s\r\n", c->id,
                   (c->needs_operator ? 'O' : '-'),
                   (c->destructive ? 'D' : '-'),
                   (unsigned long)(c->timeout_ms / 1000U),
                   c->group, c->title);
    }
}

static void cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        cli_line("usage: run <id|group|all> [--level S,L,E] [--no-operator]\r\n"
                 "                    [--destructive] [--repeat N] [--until-fail]");
        return;
    }
    char flags[96];
    flags[0] = '\0';
    size_t used = 0U;
    for (int i = 2; i < argc; i++) { /* join the flag tail (CLI-025/026) */
        int n2 = (int)snprintf(flags + used, sizeof(flags) - used, "%s%s",
                               (used > 0U) ? " " : "", argv[i]);
        if (n2 <= 0) { break; }
        used += (size_t)n2;
        if (used >= sizeof(flags) - 1U) { break; }
    }
    /* SAFE-005: ask before destructive runs in interactive mode */
    if ((strstr(flags, "--destructive") != NULL) &&
        (g_set.mode == HWVAL_MODE_INTERACTIVE)) {
        cli_printf("DESTRUCTIVE run requested: flash sector 11, backup "
                   "SRAM, backup regs, RTC cfg, resets\r\n");
        int yn = hwval_cli_ask_yn("confirm destructive run", 60U);
        if (yn <= 0) {
            cli_line("aborted (NOT confirmed)");
            return;
        }
    }
    int rc = hwval_test_submit_selector(argv[1], flags);
    if (rc != 0) {
        cli_line("ERR runner busy (wait for the current run / abort it)");
    } else {
        cli_line("run queued (results stream as the Runner executes)");
    }
}

static void cmd_abort(int argc, char **argv)
{
    (void)argc; (void)argv;
    hwval_test_request_abort(); /* FRT-011 / CLI-027: cooperative stop */
    cli_line("abort requested (test checks it between steps)");
}

static const char *res_name(tfw_result_t r)
{
    static const char *const names[] = { "PASS", "FAIL", "SKIP", "N/A" };
    return names[r];
}

static void cmd_results(int argc, char **argv)
{
    if ((argc >= 2) && (strcmp(argv[1], "clear") == 0)) {
        hwval_results_clear();
        cli_line("results cleared");
        return;
    }
    size_t fails = 0U, skips = 0U;
    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const hwval_test_result_t *r = hwval_test_result_of(i);
        if (r->result == TFW_FAIL) { fails++; }
        if (r->result == TFW_SKIP) { skips++; }
        cli_printf("%-14s %-4s %6lu ms %s%s%s\r\n", hwval_test_at(i)->id,
                   res_name(r->result), (unsigned long)r->duration_ms,
                   (r->result == TFW_FAIL) ? bad_color() :
                   (r->result == TFW_SKIP) ? warn_color() : ok_color(),
                   r->reason, c_off());
    }
    cli_printf("totals: fail=%lu skip=%lu of %lu\r\n",
               (unsigned long)fails, (unsigned long)skips,
               (unsigned long)hwval_test_count());
}

static void cmd_report(int argc, char **argv)
{
    char text[2048];

    (void)argc; (void)argv;
    (void)hwval_test_report(text, sizeof(text), NULL, NULL, NULL, NULL);
    hwval_uart3_write(text, 0U);
}

static void cmd_harness(int argc, char **argv)
{
    (void)argc; (void)argv;
    hwval_harness_report();
}

static void cmd_tasks(int argc, char **argv)
{
    UBaseType_t n = uxTaskGetNumberOfTasks();
    static char buf[2048];

    (void)argc; (void)argv;
    vTaskGetRunTimeStats(buf);
    cli_printf("tasks=%lu free-heap=%lu min-ever=%lu\r\n",
               (unsigned long)n, (unsigned long)hwval_heap_free(),
               (unsigned long)hwval_heap_min_free());
    hwval_uart3_write(buf, 0U);
}

static void cmd_log(int argc, char **argv)
{
    static const char *const lvls[] = { "error", "warn", "info", "debug" };

    if (argc < 2) {
        cli_printf("log level=%s\r\n", lvls[g_set.log_level]);
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (strcmp(lvls[i], argv[1]) == 0) {
            g_set.log_level = i;
            cli_printf("log=%s\r\n", lvls[i]);
            return;
        }
    }
    cli_line("ERR unknown command-level (error|warn|info|debug)");
}

static void cmd_reset(int argc, char **argv)
{
    if ((argc >= 2) && (strcmp(argv[1], "hard") == 0)) {
        cli_line("hard reset via IWDG (stop kicking, minimal reload)");
        IWDG->KR = 0x5555U; IWDG->PR = 0U; IWDG->RLR = 1U; IWDG->KR = 0xAAAAU;
        for (;;) { ; } /* expiry resets */
    }
    cli_line("software reset");
    hwval_dbg('I', "CLI: software reset");
    hwval_dbg_flush(20U);
    vTaskDelay(pdMS_TO_TICKS(50)); /* drain the TX ring */
    NVIC_SystemReset();
}

static void cmd_mode(int argc, char **argv)
{
    if (argc < 2) {
        cli_printf("mode=%s\r\n", hwval_mode_name(g_set.mode));
        return;
    }
    if (strcmp(argv[1], "script") == 0) {
        g_set.mode = HWVAL_MODE_SCRIPT; /* no echo, colour, prompt (CLI-012) */
        return;
    }
    if (strcmp(argv[1], "interactive") == 0) {
        g_set.mode = HWVAL_MODE_INTERACTIVE;
        hwval_uart3_puts("");
        return;
    }
    if (strcmp(argv[1], "link") == 0) {
        if (g_set.mode == HWVAL_MODE_LINK) {
            return;
        }
        cli_line("OK link");
        hwval_uart3_drain(200U);
        hwval_link_set_boot_flag(1); /* LINK-010 */
        g_set.mode = HWVAL_MODE_LINK;
        hwval_dbg('I', "mode: link (HWLINK on USART3)");
        return;
    }
    cli_line("ERR unknown command (interactive|script|link)");
}

static void cmd_format(int argc, char **argv)
{
    if ((argc >= 2) && (strcmp(argv[1], "json") == 0)) {
        g_set.format = HWVAL_FMT_JSON;
        cli_line("format=json");
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "text") == 0)) {
        g_set.format = HWVAL_FMT_TEXT;
        cli_line("format=text");
        return;
    }
    cli_printf("format=%s\r\n", hwval_format_name(g_set.format));
}

static void cmd_set(int argc, char **argv)
{
    if (argc < 3) {
        cli_line("usage: set <key> <value>");
        return;
    }
    if (strcmp(argv[1], "operator-timeout") == 0) {
        g_set.operator_timeout_s = (uint32_t)strtoul(argv[2], NULL, 10);
    } else if (strcmp(argv[1], "fault-action") == 0) {
        int a = (strcmp(argv[2], "reset") == 0) ? 1 : 0;
        g_set.fault_action = a;
        hwval_fault_set_action((a != 0) ? HWVAL_FAULT_RESET : HWVAL_FAULT_HALT);
    } else if (strcmp(argv[1], "color") == 0) {
        g_set.color = (strcmp(argv[2], "on") == 0) ? 1 : 0;
    } else {
        cli_printf("ERR unknown setting '%s'\r\n", argv[1]);
        return;
    }
    cli_printf("ok: %s=%s\r\n", argv[1], argv[2]);
}

static void cmd_get(int argc, char **argv)
{
    if (argc < 2) {
        cli_line("usage: get <key>");
        return;
    }
    if (strcmp(argv[1], "operator-timeout") == 0) {
        cli_printf("operator-timeout=%lu\r\n",
                   (unsigned long)g_set.operator_timeout_s);
    } else if (strcmp(argv[1], "fault-action") == 0) {
        cli_printf("fault-action=%s\r\n",
                   (g_set.fault_action != 0) ? "reset" : "halt");
    } else if (strcmp(argv[1], "color") == 0) {
        cli_printf("color=%s\r\n", g_set.color ? "on" : "off");
    } else if (strcmp(argv[1], "log") == 0) {
        cli_printf("log=%d\r\n", g_set.log_level);
    } else {
        cli_printf("ERR unknown setting '%s'\r\n", argv[1]);
    }
}

/* peek/poke (CLI-037): poke refuses flash, OTP, option bytes; peripherals
 * require --force (SAFE policy). */
static int addr_flash_ro(uint32_t a)
{
    return ((a >= 0x08000000U && a <= 0x081FFFFFU) ||
            (a >= 0x1FF00000U && a <= 0x1FF0FFFFU) || /* system/OTP */
            (a >= 0x1FFF0000U && a <= 0x1FFFFFFFU));
}

static int addr_peripheral(uint32_t a)
{
    return ((a >= 0x40000000U && a <= 0x4007FFFFU) ||
            (a >= 0x40010000U && a <= 0x4002FFFFU) ||
            (a >= 0x50000000U && a <= 0x50060FFFU) ||
            (a >= 0x52000000U && a <= 0x52007FFFU) ||
            (a >= 0xE0000000U && a <= 0xE0100FFFU));
}

static void cmd_peek(int argc, char **argv)
{
    if (argc < 2) {
        cli_line("usage: peek <addr> [n]");
        return;
    }
    uint32_t a = (uint32_t)strtoul(argv[1], NULL, 16);
    int n = (argc >= 3) ? (int)strtoul(argv[2], NULL, 0) : 1;
    if (n <= 0 || n > 32) { n = 1; }

    for (int i = 0; i < n; i++) {
        cli_printf("%08lX: %08lX\r\n", (unsigned long)(a + (uint32_t)(i * 4U)),
                   (unsigned long)(*(volatile const uint32_t *)a));
        a += 4U;
    }
}

static void cmd_poke(int argc, char **argv)
{
    int force = 0;
    for (int i = 2; i <= argc - 1; i++) {
        if (strcmp(argv[i], "--force") == 0) { force = 1; }
    }
    if (argc < 3) {
        cli_line("usage: poke <addr> <value> [--force]");
        return;
    }
    uint32_t a = (uint32_t)strtoul(argv[1], NULL, 16);
    uint32_t v = (uint32_t)strtoul(argv[2], NULL, 0);

    if (addr_flash_ro(a) && !force) { /* OTP/option bytes never writable */
        cli_line("ERR refuse write to flash/OTP/option region (--force won't write there)");
        return;
    }
    if (addr_flash_ro(a)) {
        cli_line("ERR refused: flash/OTP/option bytes are write-protected by policy");
        return;
    }
    if (addr_peripheral(a) && !force) {
        cli_line("ERR peripheral address: add --force");
        return;
    }
    *(volatile uint32_t *)a = v;
    cli_printf("poked %08lX=%08lX\r\n", (unsigned long)a, (unsigned long)v);
}

/* gpio (CLI-038): refuses the CON-005 reserved pins. */
static int gpio_parse(const char *s, GPIO_TypeDef **port, uint32_t *pin)
{
    if ((s[0] != 'P') || (s[1] < 'A') || (s[1] > 'K')) {
        return -1;
    }
    char *end = NULL;
    long pinnum = strtol(s + 2, &end, 10);
    if ((end == s + 2) || (pinnum < 0) || (pinnum > 15)) {
        return -1;
    }
    static GPIO_TypeDef *const ports[] = {
        GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH, GPIOI,
        GPIOJ, GPIOK
    };
    *port = ports[s[1] - 'A'];
    *pin = (uint32_t)(1U << pinnum);
    return 0;
}

static int gpio_reserved(GPIO_TypeDef *port, uint32_t pin)
{
    if ((port == GPIOA) && ((pin == GPIO_PIN_13) || (pin == GPIO_PIN_14))) {
        return 1; /* SWD (CON-005) */
    }
    if ((port == GPIOH) && ((pin == GPIO_PIN_0) || (pin == GPIO_PIN_1))) {
        return 1; /* HSE bypass input (CON-005) */
    }
    return 0;
}

static void gpio_clk_on(GPIO_TypeDef *port)
{
    if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
    else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
    else if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
    else if (port == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
}

static void cmd_gpio(int argc, char **argv)
{
    if (argc < 2) {
        cli_line("usage: gpio <pin> [in|out|high|low|pullup|pulldown|read]");
        return;
    }
    GPIO_TypeDef *port = NULL;
    uint32_t pin = 0U;
    if (gpio_parse(argv[1], &port, &pin) != 0) {
        cli_line("ERR pin name (e.g. PB12)");
        return;
    }
    if (gpio_reserved(port, pin)) {
        cli_line("ERR refuse: pin reserved by CON-005");
        return;
    }
    gpio_clk_on(port);

    if (argc < 3) { /* default: plain input read */
        cli_printf("%s=%lu\r\n", argv[1],
                   (unsigned long)HAL_GPIO_ReadPin(port, pin));
        return;
    }

    GPIO_InitTypeDef io = {
        .Pin = pin, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW
    };
    const char *cmd = argv[2];
    if (strcmp(cmd, "in") == 0) { io.Mode = GPIO_MODE_INPUT; }
    else if (strcmp(cmd, "out") == 0) { io.Mode = GPIO_MODE_OUTPUT_PP; }
    else if (strcmp(cmd, "pullup") == 0) { io.Pull = GPIO_PULLUP; }
    else if (strcmp(cmd, "pulldown") == 0) { io.Pull = GPIO_PULLDOWN; }
    else if ((strcmp(cmd, "high") == 0) || (strcmp(cmd, "low") == 0)) {
        io.Mode = GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_Init(port, &io);
        HAL_GPIO_WritePin(port, pin,
                          (cmd[0] == 'h') ? GPIO_PIN_SET : GPIO_PIN_RESET);
        cli_printf("%s %s\r\n", argv[1], cmd);
        return;
    } else if (strcmp(cmd, "read") == 0) {
        cli_printf("%s=%lu\r\n", argv[1],
                   (unsigned long)HAL_GPIO_ReadPin(port, pin));
        return;
    } else {
        cli_line("ERR unknown command (in|out|high|low|pullup|pulldown|read)");
        return;
    }
    HAL_GPIO_Init(port, &io);
    cli_printf("%s %s\r\n", argv[1], cmd);
}

static void cmd_clock(int argc, char **argv)
{
    hwval_clock_tree_t t;

    (void)argc; (void)argv;
    hwval_clock_get_tree(&t);
    cli_printf("HSE(byp)=%lu HSI=%lu LSI=%lu LSE=%lu\r\n",
               t.hse_hz, t.hsi_hz, t.lsi_hz, t.lse_hz);
    cli_printf("SYSCLK=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu\r\n",
               t.sysclk_hz, t.hclk_hz, t.pclk1_hz, t.pclk2_hz);
    cli_printf("APB1_TIM=%lu APB2_TIM=%lu PLL48(Q)=%lu\r\n",
               t.apb1_tim_hz, t.apb2_tim_hz, t.pllq_hz);
    cli_printf("SW/SWS from CFGR: %s\r\n",
               ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) ? "PLL"
                                                                : "other");
}

/* ------------------------------------------------ line editor + history */
static char s_line[HWVAL_LINE_MAX];
static int  s_len;
static int  s_esc;     /* escape sequence state: 0 none, 1 ESC, 2 CSI     */

static void editor_reset(void)
{
    s_len = 0;
    s_line[0] = '\0';
}

static void editor_show(const char *prompt)
{
    if (g_set.mode == HWVAL_MODE_INTERACTIVE) {
        hwval_uart3_write("\r\033[K", 4U);
        hwval_uart3_write(prompt, 0U);
        hwval_uart3_write(s_line, (size_t)s_len);
    }
}

static void cmd_dispatch(char *line)
{
    /* tokenize */
    char *argv[HWVAL_MAX_ARGS + 1] = { NULL };
    int argc = 0;
    char *p = line;

    while ((*p != '\0') && (argc < HWVAL_MAX_ARGS)) {
        while (*p == ' ') { p++; }
        if (*p == '\0') { break; }
        argv[argc++] = p;
        while ((*p != '\0') && (*p != ' ')) { p++; }
        if (*p == ' ') { *p++ = '\0'; }
    }
    if (argc == 0) {
        return;
    }

    uint32_t detected_baud_change = 0U;
    for (int i = 0; i < HWVAL_NCMDS; i++) {
        if (strcmp(argv[0], k_cmds[i].name) == 0) {
            (void)detected_baud_change;
            k_cmds[i].fn(argc, argv);
            return;
        }
    }
    /* CLI-040 */
    cli_printf("ERR unknown command '%s' (try help)\r\n", argv[0]);
}

/* HWLINK auto-detect: raw bytes since the last 0x00, so a host tool that
 * speaks HWLINK without "mode link" first (bootctl) is answered. */
static uint8_t s_frame[256];
static size_t s_frame_len;
static int s_frame_overflow;

/* Returns 1 when c ended a HWLINK frame and the CLI switched to link mode. */
static int frame_detect(int c)
{
    if (c != 0) {
        if (s_frame_len < sizeof(s_frame)) {
            s_frame[s_frame_len++] = (uint8_t)c;
        } else {
            s_frame_overflow = 1;
        }
        return 0;
    }
    /* 0x00 is never typed: it is a COBS frame delimiter. */
    int have = (s_frame_len > 0U) ? 1 : 0;

    if ((have != 0) && (s_frame_overflow == 0)) {
        hwval_link_prime(s_frame, s_frame_len);
    }
    s_frame_len = 0U;
    s_frame_overflow = 0;
    if (have == 0) {
        return 0;
    }
    editor_reset();
    g_set.mode = HWVAL_MODE_LINK;
    return 1;
}

void hwval_cli_task(void *arg)
{
    (void)arg;
    editor_reset();
    s_hist_pos = 0;
    if (g_set.mode != HWVAL_MODE_LINK) {
        cli_printf("\r\n%sHWVAL CLI ready%s\r\n", ok_color(), c_off());
        editor_show(HWVAL_PROMPT);
    }

    for (;;) {
        int c;

        hwval_task_report(HWVAL_TASK_CLI);
        if (g_set.mode == HWVAL_MODE_LINK) {
            hwval_link_run();
            editor_reset();
            s_baud = (uint32_t)hwval_uart3_baud();
            s_hist_pos = s_hist_wr;
            hwval_uart3_puts("");
            editor_show(HWVAL_PROMPT);
            continue;
        }

        c = hwval_uart3_getc();
        if (c < 0) {
            cli_baud_revert_poll();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (frame_detect(c) != 0) {
            continue;   /* link loop takes over on the next iteration */
        }

        if (g_set.mode == HWVAL_MODE_SCRIPT) {
            /* CLI-012: no echo, no colours, no prompt */
            if ((c == '\r') || (c == '\n')) {
                hwval_uart3_puts("");
                cmd_dispatch(s_line);
                editor_reset();
            } else if (c == 0x08 || c == 0x7F) {
                if (s_len > 0) { s_len--; s_line[s_len] = '\0'; }
            } else if (s_len < (HWVAL_LINE_MAX - 1)) {
                s_line[s_len++] = (char)c;
                s_line[s_len] = '\0';
            }
            continue;
        }

        /* interactive editor (CLI-010, CLI-011) */
        if (s_esc == 1) {
            s_esc = (c == '[') ? 2 : 0;
            continue;
        }
        if (s_esc == 2) {
            if ((c == 'A') || (c == 'B')) { /* history up/down (CLI-010) */
                if (c == 'A') {
                    int oldest = (s_hist_wr >= HWVAL_HISTORY)
                        ? (s_hist_wr - HWVAL_HISTORY) : 0;
                    if (s_hist_pos > oldest) { s_hist_pos--; }
                } else {
                    if (s_hist_pos < s_hist_wr) { s_hist_pos++; }
                }
                editor_reset();
                int have = 0;
                if (s_hist_wr > 0 && s_hist_pos < s_hist_wr) {
                    const char *h = s_hist[s_hist_pos % HWVAL_HISTORY];
                    while (*h && (s_len < HWVAL_LINE_MAX - 1)) {
                        s_line[s_len++] = *h++;
                    }
                    s_line[s_len] = '\0';
                    have = 1;
                }
                if (have) { editor_show(HWVAL_PROMPT); }
            }
            s_esc = 0;
            continue;
        }

        switch (c) {
        case 0x03: /* Ctrl-C: clear line + abort running test (CLI-010/027) */
            editor_reset();
            hwval_test_request_abort();
            hwval_uart3_puts("^C");
            editor_show(HWVAL_PROMPT);
            break;
        case 0x15: /* Ctrl-U: clear line */
            editor_reset();
            editor_show(HWVAL_PROMPT);
            break;
        case 0x09: { /* Tab: complete command names and test ids (CLI-011) */
            int matches = 0;
            const char *hit = NULL;
            if (s_len > 0) {
                for (int i = 0; i < HWVAL_NCMDS; i++) {
                    if (strncmp(k_cmds[i].name, s_line, (size_t)s_len) == 0) {
                        matches++;
                        hit = k_cmds[i].name;
                    }
                }
                if (matches == 1) {
                    int hl = 0;
                    while (hit[hl] != '\0') { hl++; }
                    while (s_len < hl) {
                        s_line[s_len] = hit[s_len];
                        s_len++;
                    }
                    s_line[s_len] = '\0';
                }
            }
            editor_show(HWVAL_PROMPT);
            break;
        }
        case 0x1B:
            s_esc = 1;
            break;
        case '\r':
        case '\n': /* accept \r, \r\n, \n (CLI-005) */
            if (c == '\r') { hwval_uart3_puts(""); }
            if (s_len > 0) {
                (void)snprintf(s_hist[s_hist_wr % HWVAL_HISTORY],
                               HWVAL_LINE_MAX, "%s", s_line);
                s_hist[s_hist_wr % HWVAL_HISTORY][HWVAL_LINE_MAX - 1U] = '\0';
                s_hist_wr++;
                s_hist_pos = s_hist_wr;
            }
            cmd_dispatch(s_line);
            editor_reset();
            if (g_set.mode == HWVAL_MODE_LINK) {
                break; /* link loop takes over on the next iteration */
            }
            hwval_uart3_puts("");
            editor_show(HWVAL_PROMPT);
            break;
        case 0x08:
        case 0x7F:
            if (s_len > 0) {
                s_len--;
                s_line[s_len] = '\0';
                hwval_uart3_write("\b \b", 3U);
            }
            break;
        default:
            if ((c >= 0x20) && (c < 0x7F) &&
                (s_len < (HWVAL_LINE_MAX - 1))) {
                s_line[s_len++] = (char)c;
                s_line[s_len] = '\0';
                hwval_uart3_write(&c, 1U); /* echo (CLI-012 interactive) */
            }
            break;
        }
    }
}