/*
 * test_core.h - HWVAL test framework (sections 5+7)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     TFW-001..TFW-008, TFW-010..TFW-022, ENV-004, FRT-010..FRT-013,
 *          CLI-023..CLI-035, TFW-007 (bootcheck protocol).
 */

#ifndef HWVAL_TEST_CORE_H
#define HWVAL_TEST_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Result states (TFW-001). */
typedef enum {
    TFW_PASS = 0,
    TFW_FAIL = 1,
    TFW_SKIP = 2,
    TFW_NA   = 3
} tfw_result_t;

/* Test levels (section 3.2). */
typedef enum { TFW_LEVEL_S = 0, TFW_LEVEL_L = 1, TFW_LEVEL_E = 2 } tfw_level_t;

/* What a test writes: details for the FAIL/SKIP line (TFW-004) and the
 * machine-readable measured/limits fields (TFW-011). */
struct tfw_out {
    char reason[96];
    char measured[48];
    char limits[48];
};

#define HWVAL_REASON_BUF 96

/* printf-style helpers every test uses. */
void hwval_fr(char *dst, size_t n, const char *fmt, ...);           /* reason   */
void hwval_fail(struct tfw_out *o, const char *fmt, ...);           /* FAIL why */
void hwval_meas(struct tfw_out *o, const char *fmt, ...);           /* measured */
void hwval_lim(struct tfw_out *o, const char *fmt, ...);            /* limits   */

/* One test case (TFW-001). */
struct tfw_case {
    const char    *id;
    const char    *group;
    const char    *title;
    tfw_level_t    level;
    uint8_t        needs_operator;
    uint8_t        destructive;
    uint8_t        resets_mcu;   /* TFW-006/007: runs last, bootcheck arm  */
    uint32_t       timeout_ms;
    const char    *verifies;     /* requirement ids, e.g. "FRT-020"        */
    tfw_result_t (*fn)(struct tfw_out *o);
};

/* Registration: each test file calls hwval_test_register() once at boot
 * (TFW-002 single table). The framework sorts into the fixed order of
 * TFW-006: S, L, E, destructive, MCU-resetting last. */
void hwval_test_register(const struct tfw_case *table, size_t count);

/* Call once after the last group has registered: validates and restores the
 * results table kept in .noinit. Registering per group must not reset the
 * accumulated table or re-run the restore. */
void hwval_test_registration_done(void);

/* Registration entry points, one per test file (called from main). */
void hwval_test_register_rtos(void);
void hwval_test_register_dev(void);
void hwval_test_register_cm7(void);
void hwval_test_register_cache_mpu(void);
void hwval_test_register_ram(void);
void hwval_test_register_flash(void);
void hwval_test_register_clk(void);
void hwval_test_register_pwr(void);
void hwval_test_register_rst_wdg(void);
void hwval_test_register_rtc(void);
void hwval_test_register_int(void);
void hwval_test_register_dma(void);
void hwval_test_register_gpio(void);
void hwval_test_register_led_btn(void);
void hwval_test_register_adc(void);
void hwval_test_register_dac(void);
void hwval_test_register_tim(void);
void hwval_test_register_uart(void);
void hwval_test_register_spi(void);
void hwval_test_register_sai_spdif(void);
void hwval_test_register_i2c(void);
void hwval_test_register_can(void);
void hwval_test_register_mem(void);
void hwval_test_register_eth_usb(void);
void hwval_test_register_gfx_misc(void);
void hwval_test_register_dbg_brd(void);
void hwval_test_register_safe(void);

size_t hwval_test_count(void);
const struct tfw_case *hwval_test_at(size_t index);

/* Runner body executed by the Runner task: the fixed-order suite minus
 * operator/destructive cases (SAFE-005) evaluated preconditions of every
 * case itself (ENV-002/ENV-003). */
void hwval_test_all(void);

/* `run <id|group|all> [flags]` (CLI-024..CLI-026): flags string holds
 * space-separated options: --level S,L,E --no-operator --destructive
 * --repeat N --until-fail. Defaults: all levels, operator allowed,
 * destructive NOT allowed. Returns cases executed. */
int hwval_test_run_sel(const char *selector, const char *flags);

/* Progress/state queries for the CLI and Watchdog (FRT-012/013). */
int hwval_test_running(void);
int hwval_test_failed(void);
int hwval_test_runner_progress_expired(void);

/* Cooperative abort (FRT-011, CLI-027). Tests poll between steps; on
 * timeout the watchdog reset is the hard backstop (TFW-003). */
void hwval_test_request_abort(void);
int  hwval_test_abort_requested(void);

/* A test result row of the session (TFW-010..TFW-012, TFW-022). */
typedef struct {
    const struct tfw_case *owner;   /* id/group from the registered table */
    tfw_result_t result;
    uint32_t     duration_ms;
    char         reason[96];
    char         measured[48];
    char         limits[48];
} hwval_test_result_t;

/* Selection of a run requested by the CLI or HWLINK (LINK-005).  Fixed-size
 * storage keeps the request safe to queue without allocation. */
#define HWVAL_RUN_MAX_SELECT 16U
#define HWVAL_RUN_SELECT_LEN 24U

struct tfw_run_req {
    char     ids[HWVAL_RUN_MAX_SELECT][HWVAL_RUN_SELECT_LEN];
    size_t   id_count;
    char     groups[HWVAL_RUN_MAX_SELECT][HWVAL_RUN_SELECT_LEN];
    size_t   group_count;
    uint8_t  all;
    uint8_t  level_ok[3];
    uint8_t  operator_ok;
    uint8_t  destructive_ok;
    uint32_t repeat;
    uint8_t  until_fail;
    uint32_t start_index;              /* resume after reset (LINK-010)      */
    uint8_t  resumed;
};

/* Event hooks used by HWLINK link mode (LINK-003/006).  The hooks are called
 * from the Runner task with the fixed-order view of the test table. */
struct tfw_hooks {
    void (*run_started)(uint32_t total, int resumed);
    void (*test_started)(const struct tfw_case *c, uint32_t index,
                         uint32_t total, uint32_t absolute_index);
    void (*test_result)(const struct tfw_case *c,
                        const hwval_test_result_t *r);
    void (*run_finished)(uint32_t total, uint32_t pass, uint32_t fail,
                         uint32_t skip, uint32_t na, int aborted,
                         int resumed);
};

void hwval_test_set_hooks(const struct tfw_hooks *hooks);

/* Runner task support (FRT-010): create/queue jobs without allocation. */
void hwval_test_runner_init(void);
int  hwval_test_submit(const struct tfw_run_req *req); /* 0 ok, -1 busy */
int  hwval_test_submit_selector(const char *selector, const char *flags);
int  hwval_test_runner_poll(uint32_t timeout_ms);      /* 1 ran, 0 idle */
int  hwval_test_active(void);                          /* suite in progress */
const char *hwval_test_current_id(void);               /* prompt context   */

const hwval_test_result_t *hwval_test_result_of(size_t index);

/* Reset-surviving results (TFW-022): magic + CRC in .noinit; restored at
 * boot unless `results clear` was the last command. */
void hwval_results_clear(void);
uint32_t hwval_results_crc(void);

uint32_t crc32_buf(const uint8_t *p, size_t n);

/* TFW-020/021: build the report text (overall + per-group totals, FAIL and
 * SKIP reasons, REPORT END).  Returns the bytes used; counts are optional. */
size_t hwval_test_report(char *out, size_t cap, uint16_t *pass,
                         uint16_t *fail, uint16_t *skip, uint16_t *na);

/* Seeded PRNG (NFR-006): deterministic fill for every test that needs
 * pseudo-random data; the seed is printed in measured fields. */
uint32_t hwval_prng_init(uint32_t seed);
uint32_t hwval_prng_seed(void);
uint32_t hwval_prng_next(void);
void     hwval_prng_fill(void *p, size_t n);

/* TFW-007: tests that reset the MCU arm this before NVIC_SystemReset and
 * the boot path reports the outcome. */
void hwval_bootcheck_arm(uint32_t case_id_hash, tfw_result_t expect);
/* Boot-time evaluation; prints the verdict banner once and clears. */
void hwval_bootcheck_evaluate(void);

#endif /* HWVAL_TEST_CORE_H */