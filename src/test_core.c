/*
 * test_core.c - HWVAL test framework (registration, runner, results)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     TFW-001..TFW-008, TFW-010..TFW-012, TFW-020..TFW-022,
 *          CLI-024..CLI-026, SAFE-005, FRT-011/013.
 */

#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "test_core.h"
#include "uart3.h"
#include "settings.h"
#include "hwval_sections.h"
#include "cli.h"
#include "version.h"
#include "debug_uart.h"

#define HWVAL_MAX_TESTS 256
#define PERSIST_MAGIC   0x48565202U /* "HVR" v2 */

/* run-order key (TFW-006): S, L, E, destructive, MCU-resetting last. */
static uint32_t order_key(const struct tfw_case *c)
{
    return (uint32_t)c->resets_mcu * 10U + (uint32_t)c->destructive * 5U +
           (uint32_t)c->level;
}

struct run_cfg {
    const struct tfw_run_req *req;   /* ids/groups/all + run filters       */
    uint8_t     level_ok[3];         /* S/L/E enable bits (CLI-025)        */
    uint8_t     operator_ok;         /* default 1                          */
    uint8_t     destructive_ok;      /* default 0 (SAFE-005)               */
    uint8_t     boot_mode;           /* auto suite skips operator+destruct */
    uint8_t     print_summary;       /* Runner job prints a text summary   */
    uint32_t    repeat;
    int         until_fail;
    uint32_t    start_index;         /* LINK-010 resume position           */
    uint8_t     resumed;
    uint32_t    executed;
};

static struct {
    struct tfw_case        order[HWVAL_MAX_TESTS]; /* fixed, sorted view  */
    size_t                 count;
    volatile int           running;
    volatile int           active;
    volatile int           failed;
    volatile uint32_t      last_progress_tick;
    volatile int           abort_requested;
    volatile uint32_t      current_timeout_ms;
    const char            *current_id;
    int                    suite_depth;
} s;

static const struct tfw_hooks *s_hooks;

/* Runner job mailbox: one request at a time, no allocation. */
static struct tfw_run_req s_job HWVAL_SRAM1;
static SemaphoreHandle_t  s_job_sem;
static volatile int       s_job_queued;
static volatile int       s_job_running;

/* TFW-022: results survive reset in .noinit, guarded by magic and CRC. */
static __attribute__((section(".noinit")))
struct {
    uint32_t magic;
    uint32_t crc;
    size_t   count;
    const struct tfw_case *owner[HWVAL_MAX_TESTS]; /* flash addresses    */
    hwval_test_result_t row[HWVAL_MAX_TESTS];      /* owner duplicated   */
} s_persist;

/* ------------------------------------------------------------ helpers -- */
static void result_fill(size_t i, tfw_result_t r, uint32_t ms,
                        const struct tfw_out *o)
{
    s_persist.owner[i] = &s.order[i];
    s_persist.row[i].owner = &s.order[i];
    s_persist.row[i].result = r;
    s_persist.row[i].duration_ms = ms;
    (void)snprintf(s_persist.row[i].reason, sizeof(s_persist.row[i].reason),
                   "%s", (o != NULL) ? o->reason : "");
    (void)snprintf(s_persist.row[i].measured,
                   sizeof(s_persist.row[i].measured), "%s",
                   (o != NULL) ? o->measured : "");
    (void)snprintf(s_persist.row[i].limits,
                   sizeof(s_persist.row[i].limits), "%s",
                   (o != NULL) ? o->limits : "");

    /* TFW-022: publish the table before a test can reset the MCU.  Without
     * the count and CRC the next boot rejects the whole table and every
     * result is lost. */
    s_persist.count = s.count;
    s_persist.crc = hwval_results_crc();
    __DSB();
}

static const char *res_name(tfw_result_t r)
{
    static const char *const names[] = { "PASS", "FAIL", "SKIP", "N/A" };
    return names[r];
}

/* TFW-013: pull a leading number and its unit out of the text fields so the
 * JSON line carries the structured value next to the text. */
static int text_first_number(const char *s, const char **end, double *value)
{
    const char *p = s;

    while ((*p != '\0') && (!isdigit((unsigned char)*p)) &&
           (*p != '-') && (*p != '.') && (*p != '+')) {
        p++;
    }
    if (*p == '\0') {
        return 0;
    }
    char *stop = NULL;
    double v = strtod(p, &stop);
    if (stop == p) {
        return 0;
    }
    *value = v;
    *end = stop;
    return 1;
}

static void text_unit(const char *p, char *unit, size_t cap)
{
    size_t n = 0U;

    while ((*p == ' ') || (*p == '\t')) { p++; }
    while ((*p != '\0') && (*p != ' ') && (*p != ',') && (*p != ';') &&
           (*p != ')') && (*p != '\r') && (*p != '\n') && (n + 1U < cap)) {
        unit[n++] = *p++;
    }
    unit[n] = '\0';
}

static void json_structured(const char *meas, const char *lim,
                            double *mv, char *mu, size_t muc, int *mok,
                            double *lmin, double *lmax, char *lu, size_t luc,
                            int *lok)
{
    const char *end = NULL;
    const char *scan = NULL;
    double a = 0.0, b = 0.0;

    *mv = 0.0; mu[0] = '\0'; *mok = 0;
    *lmin = 0.0; *lmax = 0.0; lu[0] = '\0'; *lok = 0;

    if (text_first_number(meas, &end, &a) != 0) {
        *mv = a;
        text_unit(end, mu, muc);
        *mok = 1;
    }
    if (text_first_number(lim, &end, &a) != 0) {
        scan = end;
        text_unit(end, lu, luc);
        if (strstr(lim, "<=") != NULL) {
            *lmax = a; *lok = 1;
        } else if (strstr(lim, ">=") != NULL) {
            *lmin = a; *lmax = 1.0e12; *lok = 1;
        } else if ((strstr(lim, "..") != NULL) ||
                   (strstr(lim, " to ") != NULL)) {
            if (text_first_number(scan, &end, &b) != 0) {
                *lmin = (a <= b) ? a : b;
                *lmax = (a <= b) ? b : a;
                *lok = 1;
                text_unit(end, lu, luc);
            }
        } else if (strstr(lim, "+-") != NULL) {
            if (text_first_number(scan, &end, &b) != 0) {
                if (strchr(lim, '%') != NULL) {
                    *lmin = a * (1.0 - (b / 100.0));
                    *lmax = a * (1.0 + (b / 100.0));
                } else {
                    *lmin = a - b;
                    *lmax = a + b;
                }
                *lok = 1;
                text_unit(end, lu, luc);
            }
        }
    }
}

/* TFW-010/011/012/013: one start-to-end line, atomic in the TX ring. */
static void result_emit_line(const struct tfw_case *c,
                             const hwval_test_result_t *r)
{
    char line[640];

    if (g_set.format == HWVAL_FMT_JSON) {
        double mv, lmin, lmax;
        char mu[12], lu[12];
        int mok, lok;
        json_structured(r->measured, r->limits, &mv, mu, sizeof(mu), &mok,
                        &lmin, &lmax, lu, sizeof(lu), &lok);
        (void)snprintf(line, sizeof(line),
            "{\"id\":\"%s\",\"group\":\"%s\",\"result\":\"%s\","
            "\"duration_ms\":%lu,\"reason\":\"%s\","
            "\"measured\":\"%s\",\"measured_value\":%.6g,"
            "\"measured_unit\":\"%s\",\"measured_valid\":%s,"
            "\"limits\":\"%s\",\"limits_min\":%.6g,\"limits_max\":%.6g,"
            "\"limits_unit\":\"%s\",\"limits_valid\":%s,"
            "\"reqs\":\"%s\"}\r\n",
            c->id, c->group, res_name(r->result),
            (unsigned long)r->duration_ms, r->reason, r->measured,
            mv, mu, mok ? "true" : "false", r->limits, lmin, lmax, lu,
            lok ? "true" : "false", c->verifies);
    } else {
        const char *col = (r->result == TFW_FAIL && g_set.color) ? "\033[31m" :
                          (r->result == TFW_SKIP && g_set.color) ? "\033[33m" :
                          (r->result == TFW_PASS && g_set.color) ? "\033[32m" : "";
        const char *off = (col[0] != '\0') ? "\033[0m" : "";
        (void)snprintf(line, sizeof(line), "RESULT %s %s %lu %s%s%s%s\r\n",
                       c->id, res_name(r->result),
                       (unsigned long)r->duration_ms, col, r->reason, off,
                       "");
    }
    hwval_uart3_write(line, 0U);
}

/* ------------------------------------------------------- printf helpers */
void hwval_fr(char *dst, size_t n, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(dst, n, fmt, ap);
    va_end(ap);
}

void hwval_fail(struct tfw_out *o, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(o->reason, sizeof(o->reason), fmt, ap);
    va_end(ap);
}

void hwval_meas(struct tfw_out *o, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(o->measured, sizeof(o->measured), fmt, ap);
    va_end(ap);
}

void hwval_lim(struct tfw_out *o, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(o->limits, sizeof(o->limits), fmt, ap);
    va_end(ap);
}

/* ---------------------------------------------------- CRC32 (TFW-022) -- */
uint32_t crc32_buf(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFU;

    while (n-- > 0U) {
        crc ^= (uint32_t)*p++;
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (uint32_t)(-(int32_t)(crc & 1U)));
        }
    }
    return ~crc;
}

uint32_t hwval_results_crc(void)
{
    /* CRC covers count + owners + rows (everything but magic and crc). */
    return crc32_buf((const uint8_t *)&s_persist.count,
                     sizeof(s_persist) - 2U * sizeof(uint32_t));
}

/* ------------------------------------------------- seeded PRNG (NFR-006) */
/* Every test that needs pseudo-random data uses this LCG; the seed is
 * printed in the result (NFR-006: the same result on every run on a
 * known-good board). */
static uint32_t s_prng_state;
static uint32_t s_prng_seed;

uint32_t hwval_prng_init(uint32_t seed)
{
    s_prng_seed = seed;
    s_prng_state = (seed != 0U) ? seed : 0xC0DE0000U;
    return s_prng_seed;
}

uint32_t hwval_prng_seed(void)
{
    return s_prng_seed;
}

uint32_t hwval_prng_next(void)
{
    s_prng_state = (s_prng_state * 1103515245U) + 12345U;
    return s_prng_state >> 16U;
}

void hwval_prng_fill(void *p, size_t n)
{
    uint8_t *b = (uint8_t *)p;

    while (n-- > 0U) {
        *b++ = (uint8_t)(hwval_prng_next() >> 8U);
    }
}

void hwval_results_clear(void)
{
    (void)memset(&s_persist, 0, sizeof(s_persist));
    s_persist.magic = PERSIST_MAGIC;
    s_persist.crc = hwval_results_crc();
    s.failed = 0;             /* FRT-012: LD3 off after results clear */
    s.abort_requested = 0;
}

void hwval_results_restore_or_zero(void)
{
    if ((s_persist.magic != PERSIST_MAGIC) ||
        (s_persist.crc != hwval_results_crc()) ||
        (s_persist.count != s.count)) {
        (void)memset(&s_persist, 0, sizeof(s_persist));
        s_persist.magic = PERSIST_MAGIC;
        s_persist.crc = hwval_results_crc();
        return;
    }
    /* Owners must still point at the same (rebuilt) table entries. */
    for (size_t i = 0U; i < s.count; i++) {
        if ((s_persist.owner[i] != &s.order[i]) ||
            (s_persist.row[i].owner != &s.order[i])) {
            (void)memset(&s_persist, 0, sizeof(s_persist));
            s_persist.magic = PERSIST_MAGIC;
            s_persist.crc = hwval_results_crc();
            return;
        }
    }
    hwval_uart3_puts("results: restored previous session table (TFW-022)");
}

/* ---------------------------------------------------- registration ----- */
void hwval_test_register(const struct tfw_case *table, size_t count)
{
    if ((table == NULL) || (count == 0U)) {
        return;
    }
    if ((s.count + count) > HWVAL_MAX_TESTS) {
        hwval_uart3_puts("test_core: registration table full, group ignored\r\n");
        return;
    }

    /* Append to the one table (TFW-002); stable insert, TFW-006. */
    for (size_t i = 0U; i < count; i++) {
        size_t j = s.count;
        while ((j > 0U) &&
               (order_key(&s.order[j - 1U]) > order_key(&table[i]))) {
            s.order[j] = s.order[j - 1U];
            j--;
        }
        s.order[j] = table[i];
        s.count++;
    }
}

void hwval_test_registration_done(void)
{
    hwval_results_restore_or_zero();
}

size_t hwval_test_count(void) { return s.count; }
const struct tfw_case *hwval_test_at(size_t i)
{ return (i < s.count) ? &s.order[i] : NULL; }
const hwval_test_result_t *hwval_test_result_of(size_t i)
{ return (i < s.count) ? &s_persist.row[i] : NULL; }

/* ------------------------------------------------------- state queries - */
int hwval_test_running(void) { return s.running; }
int hwval_test_failed(void)  { return s.failed; }
int hwval_test_abort_requested(void) { return s.abort_requested; }
void hwval_test_request_abort(void)  { s.abort_requested = 1; }

int hwval_test_runner_progress_expired(void) /* FRT-013 */
{
    /* An idle Runner waiting for a job is not hung. */
    if ((s.active == 0) && (s_job_running == 0)) {
        return 0;
    }
    uint32_t now    = (uint32_t)xTaskGetTickCount();
    uint32_t margin = s.running ? (s.current_timeout_ms + 5000U) : 5000U;

    return (int)((now - s.last_progress_tick) > margin);
}

/* ----------------------------------------------------------- the runner */
static int cfg_allows(const struct run_cfg *cfg, const struct tfw_case *c)
{
    const struct tfw_run_req *r = cfg->req;
    int selected = 0;

    if (r->all != 0U) {
        selected = 1;
    } else {
        for (size_t i = 0U; i < r->id_count; i++) {
            if (strcmp(c->id, r->ids[i]) == 0) {
                selected = 1;
                break;
            }
        }
        if (selected == 0) {
            for (size_t i = 0U; i < r->group_count; i++) {
                if (strcmp(c->group, r->groups[i]) == 0) {
                    selected = 1;
                    break;
                }
            }
        }
    }
    if (selected == 0) {
        return 0;
    }
    if (cfg->level_ok[(int)c->level] == 0U) {
        return 0;
    }
    if (cfg->boot_mode) { /* SAFE-005 boot defaults */
        /* MCU-resetting cases are left out too: every reset passes through
         * the bootloader (dropping a host session in its Hello window) and
         * the next boot would start the suite again.  Run them explicitly. */
        return ((c->needs_operator == 0U) && (c->destructive == 0U) &&
                (c->resets_mcu == 0U)) ? 1 : 0;
    }
    if ((c->needs_operator != 0U) && (cfg->operator_ok == 0U)) {
        return 0;
    }
    if ((c->destructive != 0U) && (cfg->destructive_ok == 0U)) {
        return 0;
    }
    return 1;
}

static void run_case(size_t i, struct run_cfg *cfg)
{
    const struct tfw_case *c = &s.order[i];
    struct tfw_out out;
    TickType_t t0;
    tfw_result_t r;

    s.current_timeout_ms  = (c->timeout_ms != 0U) ? c->timeout_ms : 2000U;
    s.last_progress_tick  = (uint32_t)xTaskGetTickCount();
    (void)memset(&out, 0, sizeof(out));

    /* Skip reasons first (ENV-002/003 checks live inside every test). */
    if (cfg->boot_mode) {
        if ((c->needs_operator != 0U) || (c->destructive != 0U)) {
            struct tfw_out so = { {0}, {0}, {0} };
            (void)snprintf(so.reason, sizeof(so.reason),
                           "boot suite excludes operator/destructive");
            result_fill(i, TFW_SKIP, 0U, &so);
            result_emit_line(c, &s_persist.row[i]);
            return;
        }
    } else {
        if ((c->needs_operator != 0U) && (cfg->operator_ok == 0U)) {
            struct tfw_out so = { {0}, {0}, {0} };
            (void)snprintf(so.reason, sizeof(so.reason),
                           "NO_OPERATOR (--no-operator filter)");
            result_fill(i, TFW_SKIP, 0U, &so);
            result_emit_line(c, &s_persist.row[i]);
            return;
        }
        if ((c->destructive != 0U) && (cfg->destructive_ok == 0U)) {
            struct tfw_out so = { {0}, {0}, {0} };
            (void)snprintf(so.reason, sizeof(so.reason),
                           "NO_DESTRUCTIVE_FLAG (SAFE-005)");
            result_fill(i, TFW_SKIP, 0U, &so);
            result_emit_line(c, &s_persist.row[i]);
            return;
        }
    }
    /* SAFE-012: link-mode destructive runs confirm every case, not only
     * the Run.allow_destructive option. */
    if ((c->destructive != 0U) && (g_set.mode == HWVAL_MODE_LINK)) {
        char question[160];
        (void)snprintf(question, sizeof(question),
                       "destructive test %s (%s): %s",
                       c->id, c->group, c->title);
        if (hwval_cli_ask_yn(question, g_set.operator_timeout_s) <= 0) {
            struct tfw_out so = { {0}, {0}, {0} };
            (void)snprintf(so.reason, sizeof(so.reason),
                           "NO_OPERATOR (destructive not confirmed)");
            result_fill(i, TFW_SKIP, 0U, &so);
            result_emit_line(c, &s_persist.row[i]);
            return;
        }
    }

    if (hwval_test_abort_requested()) {
        struct tfw_out ao = { {0}, {0}, {0} };
        (void)snprintf(ao.reason, sizeof(ao.reason), "abort");
        result_fill(i, TFW_SKIP, 0U, &ao);
        result_emit_line(c, &s_persist.row[i]);
        return;
    }

    t0 = xTaskGetTickCount();
    s.current_id = c->id;
    s.running = 1;
    cfg->executed++;
    r = c->fn(&out);
    s.running = 0;
    s.current_id = NULL;
    result_fill(i, r, (uint32_t)(xTaskGetTickCount() - t0), &out);
    if (r == TFW_FAIL) {
        s.failed++;
    }
    s.last_progress_tick = (uint32_t)xTaskGetTickCount();
    result_emit_line(c, &s_persist.row[i]);
}

static uint32_t count_allowed(const struct run_cfg *cfg)
{
    uint32_t n = 0U;

    for (size_t i = 0U; i < s.count; i++) {
        if ((i >= cfg->start_index) && (cfg_allows(cfg, &s.order[i]) != 0)) {
            n++;
        }
    }
    return n;
}

static void run_suite(struct run_cfg *cfg)
{
    uint32_t total = count_allowed(cfg);
    uint32_t index = 0U;
    uint32_t pass = 0U, fail = 0U, skip = 0U, na = 0U;
    int outer = (s.suite_depth == 0);

    s.failed = 0;
    s.abort_requested = 0;
    s.active = 1;
    s.last_progress_tick = (uint32_t)xTaskGetTickCount();
    s.suite_depth++;
    if (outer && (s_hooks != NULL) && (s_hooks->run_started != NULL)) {
        s_hooks->run_started(total, cfg->resumed);
    }
    if (outer) {
        hwval_dbg('I', "run: %lu test(s)%s", (unsigned long)total,
                  cfg->resumed ? ", resumed" : "");
    }

    for (size_t i = cfg->start_index; i < s.count; i++) {
        if (cfg_allows(cfg, &s.order[i]) == 0) {
            continue;
        }
        if (outer && (s_hooks != NULL) && (s_hooks->test_started != NULL)) {
            /* index is the position in this filtered run; i is the absolute
             * table position used for the resume record. */
            s_hooks->test_started(&s.order[i], index, total, (uint32_t)i);
        }
        index++;
        run_case(i, cfg);

        const hwval_test_result_t *r = &s_persist.row[i];
        switch (r->result) {
        case TFW_PASS: pass++; break;
        case TFW_FAIL: fail++; break;
        case TFW_SKIP: skip++; break;
        default:        na++;   break;
        }
        if (outer && (s_hooks != NULL) && (s_hooks->test_result != NULL)) {
            s_hooks->test_result(&s.order[i], r);
        }
        if (r->result == TFW_FAIL) {
            hwval_dbg('W', "FAIL %s", s.order[i].id);
        }
        if (hwval_test_abort_requested()) {
            break;
        }
        if ((cfg->until_fail != 0) && (s.failed > 0)) {
            break;
        }
    }
    s.running = 0;
    s.active = 0;
    if (outer) {
        hwval_dbg(fail != 0U ? 'W' : 'I',
                  "run: done, %lu pass, %lu fail, %lu skip, %lu n/a",
                  (unsigned long)pass, (unsigned long)fail,
                  (unsigned long)skip, (unsigned long)na);
    }
    if (outer && (s_hooks != NULL) && (s_hooks->run_finished != NULL)) {
        s_hooks->run_finished(index, pass, fail, skip, na,
                              hwval_test_abort_requested(), cfg->resumed);
    }
    s.suite_depth--;
    if (cfg->print_summary) {
        (void)printf("%lu test(s) executed, failures=%lu\r\n",
                     (unsigned long)cfg->executed, (unsigned long)s.failed);
    }
}

void hwval_test_all(void) /* boot suite (SAFE-005): no operator/destructive/reset */
{
    struct tfw_run_req req = { 0 };
    struct run_cfg cfg = { 0 };

    req.all = 1U;
    cfg.req            = &req;
    cfg.level_ok[0]    = 1U;
    cfg.level_ok[1]    = 1U;
    cfg.level_ok[2]    = 1U;
    cfg.operator_ok    = 0U;
    cfg.destructive_ok = 0U;
    cfg.boot_mode      = 1U;
    run_suite(&cfg);
}

/* ------------------------------------------- CLI run flags (CLI-025/026) */
static void parse_level_bits(const char *tok, uint8_t *ok3)
{
    for (; *tok != '\0'; tok++) {
        if ((*tok == 'S') || (*tok == 's')) { ok3[TFW_LEVEL_S] = 1U; }
        else if ((*tok == 'L') || (*tok == 'l')) { ok3[TFW_LEVEL_L] = 1U; }
        else if ((*tok == 'E') || (*tok == 'e')) { ok3[TFW_LEVEL_E] = 1U; }
    }
}

static void req_defaults(struct tfw_run_req *req)
{
    (void)memset(req, 0, sizeof(*req));
    req->level_ok[0]    = 1U;
    req->level_ok[1]    = 1U;
    req->level_ok[2]    = 1U;
    req->operator_ok    = 1U;
    req->destructive_ok = 0U; /* SAFE-005 */
    req->repeat         = 1U;
}

static void req_add_selector(struct tfw_run_req *req, const char *sel)
{
    if (sel == NULL) {
        return;
    }
    if (strcmp(sel, "all") == 0) {
        req->all = 1U;
        return;
    }
    /* A selector is an id when it has the TFW-001 dotted form, else a group. */
    if (strchr(sel, '.') != NULL) {
        if (req->id_count < HWVAL_RUN_MAX_SELECT) {
            (void)snprintf(req->ids[req->id_count], HWVAL_RUN_SELECT_LEN,
                           "%s", sel);
            req->id_count++;
        }
    } else if (req->group_count < HWVAL_RUN_MAX_SELECT) {
        (void)snprintf(req->groups[req->group_count], HWVAL_RUN_SELECT_LEN,
                       "%s", sel);
        req->group_count++;
    }
}

static void req_parse_flags(struct tfw_run_req *req, const char *flags)
{
    char flagbuf[128];
    char *tok;
    char *save = NULL;

    if (flags == NULL) {
        return;
    }
    (void)snprintf(flagbuf, sizeof(flagbuf), "%s", flags);
    tok = strtok_r(flagbuf, " \t", &save);
    while (tok != NULL) {
        /* strtok_r splits "--level S,L" into two tokens; take the value
         * from the next token instead of looking for a space in this one. */
        if (strcmp(tok, "--level") == 0) {
            tok = strtok_r(NULL, " \t", &save);
            if (tok == NULL) { break; }
            req->level_ok[0] = req->level_ok[1] = req->level_ok[2] = 0U;
            parse_level_bits(tok, req->level_ok);
        } else if (strcmp(tok, "--no-operator") == 0) {
            req->operator_ok = 0U;
        } else if (strcmp(tok, "--destructive") == 0) {
            req->destructive_ok = 1U; /* SAFE-005: only with flag */
        } else if (strcmp(tok, "--no-destructive") == 0) {
            req->destructive_ok = 0U;
        } else if (strcmp(tok, "--until-fail") == 0) {
            req->until_fail = 1U;
        } else if (strcmp(tok, "--repeat") == 0) {
            tok = strtok_r(NULL, " \t", &save);
            if (tok == NULL) { break; }
            req->repeat = (uint32_t)strtoul(tok, NULL, 10);
            if (req->repeat == 0U) { req->repeat = 1U; }
        }
        tok = strtok_r(NULL, " \t", &save);
    }
}

static int run_req_sync(const struct tfw_run_req *req, int print_summary)
{
    struct run_cfg cfg = { 0 };
    uint32_t total = 0U;

    if ((req->level_ok[0] | req->level_ok[1] | req->level_ok[2]) == 0U) {
        return 0; /* nonsensical filter */
    }
    cfg.req            = req;
    cfg.level_ok[0]    = req->level_ok[0];
    cfg.level_ok[1]    = req->level_ok[1];
    cfg.level_ok[2]    = req->level_ok[2];
    cfg.operator_ok    = req->operator_ok;
    cfg.destructive_ok = req->destructive_ok;
    cfg.repeat         = (req->repeat != 0U) ? req->repeat : 1U;
    cfg.until_fail     = req->until_fail;
    cfg.start_index    = req->start_index;
    cfg.resumed        = req->resumed;
    cfg.print_summary  = (uint8_t)print_summary;

    for (uint32_t rep = 0U; rep < cfg.repeat; rep++) {
        cfg.start_index = (rep == 0U) ? req->start_index : 0U;
        cfg.resumed = (uint8_t)((rep == 0U) ? req->resumed : 0U);
        run_suite(&cfg);
        total += cfg.executed;
        if (cfg.until_fail && (s.failed > 0)) {
            break;
        }
    }
    return (int)total;
}

int hwval_test_run_sel(const char *selector, const char *flags)
{
    struct tfw_run_req req;

    req_defaults(&req);
    req_add_selector(&req, selector);
    req_parse_flags(&req, flags);
    return run_req_sync(&req, 0);
}

/* ------------------------------------------------------------ job queue */
void hwval_test_runner_init(void)
{
    if (s_job_sem == NULL) {
        s_job_sem = xSemaphoreCreateBinary();
    }
}

int hwval_test_submit_selector(const char *selector, const char *flags)
{
    struct tfw_run_req req;

    req_defaults(&req);
    req_add_selector(&req, selector);
    req_parse_flags(&req, flags);
    if (hwval_test_active() != 0) {
        return -1;
    }
    return hwval_test_submit(&req);
}

int hwval_test_submit(const struct tfw_run_req *req)
{
    hwval_test_runner_init();
    if ((req == NULL) || (s_job_sem == NULL)) {
        return -1;
    }
    taskENTER_CRITICAL();
    if ((s_job_queued != 0) || (s.active != 0) || (s_job_running != 0)) {
        taskEXIT_CRITICAL();
        return -1; /* LINK-011: BUSY while a run is active */
    }
    s_job = *req;
    s_job_queued = 1;
    taskEXIT_CRITICAL();
    (void)xSemaphoreGive(s_job_sem);
    return 0;
}

int hwval_test_runner_poll(uint32_t timeout_ms)
{
    hwval_test_runner_init();
    if (s_job_sem == NULL) {
        vTaskDelay(pdMS_TO_TICKS(timeout_ms));
        return 0;
    }
    if (xSemaphoreTake(s_job_sem, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        return 0;
    }
    taskENTER_CRITICAL();
    s_job_queued = 0;
    s_job_running = 1;
    taskEXIT_CRITICAL();
    (void)run_req_sync(&s_job, 1);
    s_job_running = 0;
    return 1;
}

int hwval_test_active(void) { return (s.active != 0) || (s_job_running != 0); }
const char *hwval_test_current_id(void) { return s.current_id; }

void hwval_test_set_hooks(const struct tfw_hooks *hooks) { s_hooks = hooks; }

/* ---------------------------------------------------------- TFW-020/021 -- */
static size_t report_add(char *out, size_t cap, size_t used,
                         const char *fmt, ...)
{
    va_list ap;
    int n;

    if (used >= cap) {
        return cap;
    }
    va_start(ap, fmt);
    n = vsnprintf(out + used, cap - used, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return used;
    }
    if ((size_t)n >= (cap - used)) {
        return cap - 1U;
    }
    return used + (size_t)n;
}

static int bcd_value(uint32_t v)
{
    return (int)((v & 0xFU) + 10U * ((v >> 4U) & 0xFU));
}

size_t hwval_test_report(char *out, size_t cap, uint16_t *pass_out,
                         uint16_t *fail_out, uint16_t *skip_out,
                         uint16_t *na_out)
{
    uint32_t pass = 0U, fail = 0U, skip = 0U, na = 0U;
    size_t used = 0U;

    if ((out == NULL) || (cap == 0U)) {
        return 0U;
    }
    out[0] = '\0';

    for (size_t i = 0U; i < hwval_test_count(); i++) {
        switch (hwval_test_result_of(i)->result) {
        case TFW_FAIL: fail++; break;
        case TFW_SKIP: skip++; break;
        case TFW_NA:   na++;   break;
        default:       pass++; break;
        }
    }
    used = report_add(out, cap, used,
        "REPORT HWVAL v%s uid=%08lX%08lX%08lX\r\n",
        hwval_version_string(),
        (unsigned long)HAL_GetUIDw0(), (unsigned long)HAL_GetUIDw1(),
        (unsigned long)HAL_GetUIDw2());
    used = report_add(out, cap, used,
        "PASS=%lu FAIL=%lu SKIP=%lu N/A=%lu of %lu\r\n",
        (unsigned long)pass, (unsigned long)fail, (unsigned long)skip,
        (unsigned long)na, (unsigned long)hwval_test_count());
    if ((RCC->BDCR & RCC_BDCR_RTCEN) != 0U) {
        uint32_t tr = RTC->TR;
        uint32_t dr = RTC->DR;
        used = report_add(out, cap, used,
            "DATE 20%02d-%02d-%02d %02d:%02d:%02d\r\n",
            bcd_value((dr >> 16U) & 0xFFU), bcd_value((dr >> 8U) & 0x1FU),
            bcd_value(dr & 0x3FU), bcd_value((tr >> 16U) & 0x3FU),
            bcd_value((tr >> 8U) & 0x7FU), bcd_value(tr & 0x7FU));
    }

    /* per-group totals (TFW-020) */
    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const struct tfw_case *c = hwval_test_at(i);
        int seen = 0;
        uint32_t gp = 0U, gf = 0U, gs = 0U, gn = 0U;
        for (size_t j = 0U; j < i; j++) {
            if (strcmp(c->group, hwval_test_at(j)->group) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen != 0) {
            continue;
        }
        for (size_t j = 0U; j < hwval_test_count(); j++) {
            if (strcmp(c->group, hwval_test_at(j)->group) != 0) {
                continue;
            }
            switch (hwval_test_result_of(j)->result) {
            case TFW_FAIL: gf++; break;
            case TFW_SKIP: gs++; break;
            case TFW_NA:   gn++; break;
            default:       gp++; break;
            }
        }
        used = report_add(out, cap, used,
            "GROUP %s PASS=%lu FAIL=%lu SKIP=%lu N/A=%lu\r\n",
            c->group, (unsigned long)gp, (unsigned long)gf,
            (unsigned long)gs, (unsigned long)gn);
    }

    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const hwval_test_result_t *r = hwval_test_result_of(i);
        if (r->result == TFW_FAIL) {
            used = report_add(out, cap, used, "FAIL %s: %s\r\n",
                              hwval_test_at(i)->id, r->reason);
        }
    }
    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const hwval_test_result_t *r = hwval_test_result_of(i);
        if (r->result == TFW_SKIP) {
            used = report_add(out, cap, used, "SKIP %s: %s\r\n",
                              hwval_test_at(i)->id, r->reason);
        }
    }
    used = report_add(out, cap, used, "REPORT END %s\r\n",
                      (fail > 0U) ? "FAIL" : "PASS");
    if (pass_out != NULL) { *pass_out = (uint16_t)pass; }
    if (fail_out != NULL) { *fail_out = (uint16_t)fail; }
    if (skip_out != NULL) { *skip_out = (uint16_t)skip; }
    if (na_out != NULL)   { *na_out = (uint16_t)na; }
    return used;
}

/* ------------------------------------------------- TFW-007: bootcheck -- */
#define BOOTCHECK_MAGIC 0xB0C4U

static void bkp_unlock(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR1, PWR_CR1_DBP);
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_RTCEN);
    (void)RCC->APB1ENR;
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;
}

static void bkp_lock(void)
{
    RTC->WPR = 0xFFU;
}

void hwval_bootcheck_arm(uint32_t case_hash, tfw_result_t expect)
{
    bkp_unlock();
    HWVAL_BKP_CHECK_MAGIC = BOOTCHECK_MAGIC;
    HWVAL_BKP_CHECK_HASH = case_hash;
    HWVAL_BKP_CHECK_EXPECT = (uint32_t)expect;
    bkp_lock();
}

void hwval_bootcheck_evaluate(void)
{
    char line[128];

    if (!hwval_uart3_ready()) {
        return;
    }
    bkp_unlock();
    if (HWVAL_BKP_CHECK_MAGIC != BOOTCHECK_MAGIC) {
        bkp_lock();
        return;
    }
    uint32_t hash   = HWVAL_BKP_CHECK_HASH;
    tfw_result_t got = (tfw_result_t)HWVAL_BKP_CHECK_EXPECT;

    if (hash == 0x5C01U) { /* RST.002: software reset flag */
        got = ((HWVAL_BKP_RESET_FLAGS & RCC_CSR_SFTRSTF_Msk) != 0U) ? TFW_PASS
                                                         : TFW_FAIL;
        (void)snprintf(line, sizeof(line),
                       "BOOTCHECK RST.002 SFTRSTF=%lu -> %s\r\n",
                       (unsigned long)((HWVAL_BKP_RESET_FLAGS &
                         RCC_CSR_SFTRSTF_Msk) != 0U), res_name(got));
        HWVAL_BKP_CHECK_MAGIC = 0U; HWVAL_BKP_CHECK_HASH = 0U; HWVAL_BKP_CHECK_EXPECT = 0U;
        bkp_lock();
        hwval_uart3_write(line, 0U);
        return;
    }
    if (hash == 0x5D01U) { /* WDG.001: IWDG expiry timing vs RTC */
        uint32_t armed_tr = HWVAL_BKP_CHECK_ARG0;
        uint32_t expect_s = HWVAL_BKP_CHECK_ARG1;
        uint32_t flags    = HWVAL_BKP_RESET_FLAGS;
        uint32_t now      = RTC->TR;
        /* BCD seconds/min/hours delta over one day wrap */
        uint32_t as = ((now  >> 16U) & 3U) * 10U + ((now >> 20U) & 3U);
        uint32_t bs = ((armed_tr >> 16U) & 3U) * 10U + ((armed_tr >> 20U) & 3U);
        uint32_t am = (((now  >> 8U) & 7U) * 10U + ((now >> 12U) & 7U)) * 60U;
        uint32_t bm = (((armed_tr >> 8U) & 7U) * 10U +
                       ((armed_tr >> 12U) & 7U)) * 60U;
        uint32_t now_s = as + am;
        uint32_t arm_s = bs + bm;
        uint32_t delta = (now_s >= arm_s) ? (now_s - arm_s)
                          : (86400U - arm_s + now_s);
        got = ((flags & RCC_CSR_IWDGRSTF_Msk) != 0U) &&
              (((delta + (expect_s / 2U)) >= expect_s) ||
               ((delta - (expect_s / 2U)) <= expect_s)) ? TFW_PASS : TFW_FAIL;
        (void)snprintf(line, sizeof(line),
                       "BOOTCHECK WDG.001 iwdgrst=%lu elapsed=%lu s "
                       "(expect %lu) -> %s\r\n",
                       (unsigned long)((flags & RCC_CSR_IWDGRSTF_Msk) != 0U),
                       (unsigned long)delta, (unsigned long)expect_s,
                       res_name(got));
        HWVAL_BKP_CHECK_MAGIC = 0U; HWVAL_BKP_CHECK_HASH = 0U; HWVAL_BKP_CHECK_EXPECT = 0U;
        HWVAL_BKP_CHECK_ARG0 = 0U; HWVAL_BKP_CHECK_ARG1 = 0U;
        bkp_lock();
        hwval_uart3_write(line, 0U);
        return;
    }
    if (hash == 0x5D02U) { /* WDG.002: IWDG window violation */
        got = ((HWVAL_BKP_RESET_FLAGS & RCC_CSR_IWDGRSTF_Msk) != 0U) ? TFW_PASS
                                                         : TFW_FAIL;
        (void)snprintf(line, sizeof(line),
                       "BOOTCHECK WDG.002 iwdgrst=%lu (window violation) -> "
                       "%s\r\n",
                       (unsigned long)((HWVAL_BKP_RESET_FLAGS &
                         RCC_CSR_IWDGRSTF_Msk) != 0U), res_name(got));
        HWVAL_BKP_CHECK_MAGIC = 0U; HWVAL_BKP_CHECK_HASH = 0U; HWVAL_BKP_CHECK_EXPECT = 0U;
        bkp_lock();
        hwval_uart3_write(line, 0U);
        return;
    }
    if (hash == 0x5D03U) { /* WDG.003: WWDG out-of-window reset */
        got = ((HWVAL_BKP_RESET_FLAGS & RCC_CSR_WWDGRSTF_Msk) != 0U) ? TFW_PASS
                                                         : TFW_FAIL;
        (void)snprintf(line, sizeof(line),
                       "BOOTCHECK WDG.003 wwdgrst=%lu -> %s\r\n",
                       (unsigned long)((HWVAL_BKP_RESET_FLAGS &
                         RCC_CSR_WWDGRSTF_Msk) != 0U), res_name(got));
        HWVAL_BKP_CHECK_MAGIC = 0U; HWVAL_BKP_CHECK_HASH = 0U; HWVAL_BKP_CHECK_EXPECT = 0U;
        bkp_lock();
        hwval_uart3_write(line, 0U);
        return;
    }
    if (hash == 0x5B01U) { /* PWR.007: standby flag after wake */
        got = ((PWR->CSR1 & PWR_CSR1_SBF_Msk) != 0U) ? TFW_PASS : TFW_FAIL;
        (void)snprintf(line, sizeof(line),
                       "BOOTCHECK PWR.007 SBF=%lu -> %s\r\n",
                       (unsigned long)((PWR->CSR1 & PWR_CSR1_SBF_Msk) != 0U),
                       res_name(got));
        HWVAL_BKP_CHECK_MAGIC = 0U; HWVAL_BKP_CHECK_HASH = 0U; HWVAL_BKP_CHECK_EXPECT = 0U;
        bkp_lock();
        hwval_uart3_write(line, 0U);
        return;
    }
    if (hash == 0x5A00U) { /* RAM.009: backup-SRAM retention check */
        volatile const uint32_t *b = (const uint32_t *)0x40024000U;
        got = ((b[0] == HWVAL_BKP_CHECK_ARG0) && (b[1] == HWVAL_BKP_CHECK_ARG1)) ? TFW_PASS
                                                            : TFW_FAIL;
        (void)snprintf(line, sizeof(line),
                       "BOOTCHECK RAM.009 bkpsram=%08lX%08lX -> %s\r\n",
                       (unsigned long)b[1], (unsigned long)b[0],
                       res_name(got));
        HWVAL_BKP_CHECK_MAGIC = 0U; HWVAL_BKP_CHECK_HASH = 0U; HWVAL_BKP_CHECK_EXPECT = 0U;
        bkp_lock();
        hwval_uart3_write(line, 0U);
        return;
    }
    HWVAL_BKP_CHECK_MAGIC = 0U;
    HWVAL_BKP_CHECK_HASH = 0U;
    HWVAL_BKP_CHECK_EXPECT = 0U;
    bkp_lock();

    (void)snprintf(line, sizeof(line),
                   "BOOTCHECK hash=%04lX result=%s (TFW-007 post-reset)\r\n",
                   (unsigned long)(hash & 0xFFFFU), res_name(got));
    hwval_uart3_write(line, 0U);
}