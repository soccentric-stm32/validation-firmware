/*
 * link.c - HWLINK link mode (SRS section 6.4)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-03
 * Target:  NSTM32F767ZIT6
 * SRS:     LINK-001..LINK-012 (GUI CR-01..CR-08); framing LNK-001..LNK-010
 *          (SRS-GUI-F767 Appendix A).
 *
 * The CLI task owns USART3.  `mode link` installs the COBS/FlatBuffers
 * server in place of the text editor; test execution stays in the Runner
 * task and reports through the framework hooks, so Abort, OperatorAnswer
 * and Ping are served while a run is active.
 */

#include <ctype.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "bsp/clock.h"
#include "services/fault.h"
#include "bsp/flash_ops.h"
#include "app/harness.h"
#include "protocol/hwlink.h"
#include "protocol/link.h"
#include "bsp/reset_cause.h"
#include "services/rtos.h"
#include "app/settings.h"
#include "app/test_core.h"
#include "bsp/uart3.h"
#include "app/version.h"
#include "app/boot_slot.h"
#include "flatcc/flatcc_builder.h"
#include "hwlink_builder.h"
#include "hwlink_verifier.h"
#include "bsp/debug_uart.h"

#ifndef HWVAL_FIRMWARE_NAME
#define HWVAL_FIRMWARE_NAME "HWVAL"
#endif

#define LINK_GROUP_PAGE         16U
#define LINK_TEST_PAGE          8U
#define LINK_RESULT_PAGE        4U
#define LINK_HARNESS_PAGE       8U
#define LINK_TX_TIMEOUT_MS      100U
#define LINK_BAUD_REVERT_MS     5000U
#define LINK_TEXT_PROBE_MS      5000U
#define LINK_ANSWER_QUEUE_LEN   4U
#define LINK_LOG_TEXT_MAX       160U
#define LINK_PROMPT_ANSWER_MAX  32U
#define LINK_PRIME_MAX          256U

/* NFR-004: the link buffers are bulk RAM, so they live in .sram1 (zeroed by
 * main before the scheduler starts) rather than consuming the DTCM reserve. */
#define LINK_BUF __attribute__((section(".sram1"), aligned(32)))

/* Reset-surviving link flag (LINK-010).  BKP7R is outside the bootcheck
 * registers BKP0..BKP5 used by test_core.c. */
#define LINK_BKP_MAGIC          0x4C4E4B31U /* "LNK1" */
#define LINK_JOB_MAGIC          0x4C4A4F42U /* "LJOB" */

struct link_persist {
    uint32_t            magic;
    uint32_t            crc;
    struct tfw_run_req  req;
    uint32_t            next_index;
    uint8_t             active;
    uint8_t             aborted;
    uint8_t             resumed;
    uint8_t             pad;
};

static __attribute__((section(".noinit"))) struct link_persist s_persist;

struct link_answer {
    uint32_t id;
    char     text[LINK_PROMPT_ANSWER_MAX];
};

static flatcc_builder_t s_builder;
static SemaphoreHandle_t s_tx_mutex;
static SemaphoreHandle_t s_log_mutex;
static QueueHandle_t     s_answer_q;
static volatile int      s_active;
static volatile int      s_quiet;
static volatile int      s_boot_active;
static volatile int      s_boot_report;
static volatile int      s_text_requested;
static volatile int      s_prompt_cancel;
static uint32_t          s_prompt_next = 1U;
static volatile uint32_t s_prompt_waiting;

/* LINK-003/LNK-009: progress for long runs. */
static volatile uint32_t s_run_total;
static volatile uint32_t s_run_done;
static uint32_t s_progress_tick;

static int      s_have_resume;
static struct tfw_run_req s_resume_req LINK_BUF;

/* Frame assembly. */
static uint8_t s_rx_enc[HWLINK_MAX_ENCODED] LINK_BUF;
static size_t  s_rx_len;
static int     s_rx_overflow;
static uint8_t s_rx_raw[HWLINK_MAX_FRAME] LINK_BUF;
static uint8_t s_rx_payload[HWLINK_MAX_FRAME] LINK_BUF;
static uint8_t s_build_payload[HWLINK_MAX_FRAME] LINK_BUF;
static uint8_t s_raw_tx[HWLINK_MAX_FRAME] LINK_BUF;
static uint8_t s_tx_frame[HWLINK_MAX_ENCODED] LINK_BUF;
static uint8_t s_last_response[HWLINK_MAX_FRAME] LINK_BUF;
static size_t   s_last_response_len;
static uint16_t s_last_seq;
static int      s_last_valid;

/* Text sink -> Log events (LINK-002). */
static char   s_log_buf[LINK_LOG_TEXT_MAX];
static size_t s_log_len;

/* Baud revert (LNK-001). */
static int      s_baud_pending;
static uint32_t s_baud_last_valid;

/* OPEN-011 text fallback. */
static int      s_text_probe;
static uint32_t s_text_probe_tick;

/* ------------------------------------------------------------- helpers -- */
static hwlink_TestVerdict_enum_t verdict_enum(tfw_result_t r)
{
    switch (r) {
    case TFW_FAIL: return hwlink_TestVerdict_FAIL;
    case TFW_SKIP: return hwlink_TestVerdict_SKIP;
    case TFW_NA:   return hwlink_TestVerdict_NA;
    default:       return hwlink_TestVerdict_PASS;
    }
}

static hwlink_TestLevel_enum_t level_enum(tfw_level_t l)
{
    switch (l) {
    case TFW_LEVEL_L: return hwlink_TestLevel_L;
    case TFW_LEVEL_E: return hwlink_TestLevel_E;
    default:          return hwlink_TestLevel_S;
    }
}

static hwlink_PromptKind_enum_t prompt_enum(hwval_prompt_kind_t k)
{
    switch (k) {
    case HWVAL_PROMPT_LINE:        return hwlink_PromptKind_LINE;
    case HWVAL_PROMPT_BUTTON:      return hwlink_PromptKind_BUTTON;
    case HWVAL_PROMPT_EQUIPMENT:   return hwlink_PromptKind_EQUIPMENT;
    case HWVAL_PROMPT_DESTRUCTIVE: return hwlink_PromptKind_DESTRUCTIVE;
    default:                       return hwlink_PromptKind_YES_NO;
    }
}

static const char *group_name(const char *code)
{
    static const struct { const char *code; const char *name; } k_names[] = {
        { "DEV", "Device identity" }, { "CORE", "Cortex-M7 core" },
        { "CACHE", "Caches" }, { "MPU", "MPU" }, { "RAM", "Internal RAM" },
        { "FLS", "Flash" }, { "OB", "Option bytes" }, { "OTP", "OTP" },
        { "CLK", "Clocks" }, { "PWR", "Power" }, { "RST", "Resets" },
        { "WDG", "Watchdogs" }, { "RTC", "RTC and backup" },
        { "INT", "Interrupts/EXTI" }, { "DMA", "DMA" }, { "GPIO", "GPIO" },
        { "LED", "LEDs" }, { "BTN", "Buttons" }, { "ADC", "ADC" },
        { "DAC", "DAC" }, { "TIM", "Timers" }, { "LPTIM", "Low-power timer" },
        { "UART", "USART/UART" }, { "SPI", "SPI" }, { "I2S", "I2S" },
        { "SAI", "SAI" }, { "SPDIF", "SPDIF-RX" }, { "I2C", "I2C" },
        { "CAN", "CAN" }, { "SD", "SDMMC" }, { "QSPI", "Quad-SPI" },
        { "FMC", "FMC" }, { "ETH", "Ethernet" }, { "USB", "USB OTG FS" },
        { "USBHS", "USB OTG HS" }, { "LTDC", "LCD-TFT" },
        { "DMA2D", "DMA2D" }, { "JPEG", "JPEG" }, { "DCMI", "DCMI" },
        { "CRC", "CRC" }, { "RNG", "RNG" }, { "CEC", "CEC" },
        { "DFSDM", "DFSDM" }, { "MDIOS", "MDIOS" }, { "DBG", "Debug" },
        { "STLK", "ST-LINK" }, { "BRD", "Board" }, { "SAFE", "Safety" },
        { "FRT", "RTOS" },
    };

    for (size_t i = 0U; i < (sizeof(k_names) / sizeof(k_names[0])); i++) {
        if (strcmp(code, k_names[i].code) == 0) {
            return k_names[i].name;
        }
    }
    return code;
}

static int link_lock(void)
{
    if (s_tx_mutex == NULL) {
        return 1;
    }
    return (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(LINK_TX_TIMEOUT_MS)) ==
            pdPASS) ? 1 : 0;
}

static void link_unlock(void)
{
    if (s_tx_mutex != NULL) {
        (void)xSemaphoreGive(s_tx_mutex);
    }
}

static void link_reset_parser(void)
{
    s_rx_len = 0U;
    s_rx_overflow = 0;
}

static int link_begin(void)
{
    if (s_active == 0) {
        return 0;
    }
    if (!link_lock()) {
        return 0;
    }
    flatcc_builder_reset(&s_builder);
    return 1;
}

static bool link_send_payload(const uint8_t *payload, size_t payload_len,
                              uint8_t flags, uint16_t seq, int store)
{
    size_t raw_len = 0U;
    size_t encoded_len = 0U;

    if (!hwlink_build_raw(payload, payload_len, flags, seq,
                          s_raw_tx, sizeof(s_raw_tx), &raw_len)) {
        return false;
    }
    if (store != 0) {
        if (raw_len <= sizeof(s_last_response)) {
            memcpy(s_last_response, s_raw_tx, raw_len);
            s_last_response_len = raw_len;
        }
    }
    if (!hwlink_cobs_encode(s_raw_tx, raw_len, s_tx_frame, sizeof(s_tx_frame),
                            &encoded_len)) {
        return false;
    }
    hwval_uart3_write_direct(s_tx_frame, encoded_len);
    return true;
}

/* Finish a builder transaction and send the frame (tx mutex already held). */
static bool link_end(hwlink_Body_union_ref_t body, uint16_t seq,
                     uint8_t flags, int store)
{
    bool ok = false;

    if ((body.type != 0U) && (body.value != 0U) &&
        (hwlink_Message_create_as_root(&s_builder, body) != 0U)) {
        size_t size = 0U;
        void *direct = flatcc_builder_get_direct_buffer(&s_builder, &size);

        if ((direct != NULL) && (size != 0U) && (size <= sizeof(s_build_payload))) {
            memcpy(s_build_payload, direct, size);
            ok = link_send_payload(s_build_payload, size, flags, seq, store);
        }
    }
    flatcc_builder_reset(&s_builder);
    link_unlock();
    return ok;
}

static bool link_send_ack(uint16_t seq)
{
    if (!link_begin()) { return false; }
    hwlink_Ack_ref_t ack = hwlink_Ack_create(&s_builder, seq, 0U);
    return link_end(hwlink_Body_as_Ack(ack), seq, HWLINK_FLAG_RESPONSE, 1);
}

static bool link_send_nak(uint16_t seq, hwlink_Error_enum_t error,
                          const char *detail)
{
    if (!link_begin()) { return false; }
    flatbuffers_string_ref_t text =
        flatbuffers_string_create_str(&s_builder, (detail != NULL) ? detail : "");
    hwlink_Nak_ref_t nak = hwlink_Nak_create(&s_builder, seq, error, text, 0U);
    return link_end(hwlink_Body_as_Nak(nak), seq, HWLINK_FLAG_RESPONSE, 1);
}

/* LINK-007: HelloReply carries role HWVAL, identity and protocol version. */
static bool link_send_hello(uint16_t seq)
{
    if (!link_begin()) { return false; }
    flatbuffers_string_ref_t name =
        flatbuffers_string_create_str(&s_builder, HWVAL_FIRMWARE_NAME);
    flatbuffers_string_ref_t version =
        flatbuffers_string_create_str(&s_builder, hwval_version_string());
    flatbuffers_string_ref_t commit =
        flatbuffers_string_create_str(&s_builder, hwval_git_commit());
    flatbuffers_string_ref_t build =
        flatbuffers_string_create_str(&s_builder, hwval_build_date());
    flatbuffers_string_ref_t detail =
        flatbuffers_string_create_str(&s_builder, hwval_srs_id());

    uint32_t devid = DBGMCU->IDCODE;
    uint64_t uid = ((uint64_t)HAL_GetUIDw0() << 32U) |
                   (uint64_t)HAL_GetUIDw1();
    hwlink_FlashMode_enum_t mode = hwval_flash_dual_bank() != 0
                                       ? hwlink_FlashMode_DUAL_BANK
                                       : hwlink_FlashMode_SINGLE_BANK;
    hwlink_HelloReply_ref_t reply = hwlink_HelloReply_create(
        &s_builder, hwlink_Role_HWVAL, name, version, commit, build, uid,
        devid & 0xFFFU, devid >> 16U, mode,
        (uint16_t)HWLINK_PROTOCOL_MAJOR, (uint16_t)HWLINK_PROTOCOL_MINOR,
        (uint16_t)HWLINK_MAX_FRAME, detail);
    return link_end(hwlink_Body_as_HelloReply(reply), seq,
                    HWLINK_FLAG_RESPONSE, 1);
}

static bool link_send_pong(uint16_t seq)
{
    if (!link_begin()) { return false; }
    hwlink_Pong_ref_t pong =
        hwlink_Pong_create(&s_builder, (uint64_t)HAL_GetTick());
    return link_end(hwlink_Body_as_Pong(pong), seq, HWLINK_FLAG_RESPONSE, 1);
}

/* Status: the BOOT slot and trial state that bootctl waits for after an
 * update, plus identity, uptime and heap. */
static bool link_send_status(uint16_t seq)
{
    if (!link_begin()) { return false; }
    uint32_t cause = hwval_reset_cause_flags();
    flatbuffers_string_ref_t name =
        flatbuffers_string_create_str(&s_builder, HWVAL_FIRMWARE_NAME);
    flatbuffers_string_ref_t version =
        flatbuffers_string_create_str(&s_builder, hwval_version_string());
    flatbuffers_string_ref_t commit =
        flatbuffers_string_create_str(&s_builder, hwval_git_commit());
    flatbuffers_string_ref_t build =
        flatbuffers_string_create_str(&s_builder, hwval_build_date());
    flatbuffers_string_ref_t cause_text =
        flatbuffers_string_create_str(&s_builder, hwval_reset_cause_name(cause));
    uint64_t uid = ((uint64_t)HAL_GetUIDw0() << 32U) |
                   (uint64_t)HAL_GetUIDw1();

    hwlink_Status_start(&s_builder);
    hwlink_Status_firmware_name_add(&s_builder, name);
    hwlink_Status_version_add(&s_builder, version);
    hwlink_Status_commit_add(&s_builder, commit);
    hwlink_Status_build_time_add(&s_builder, build);
    hwlink_Status_uid_add(&s_builder, uid);
    hwlink_Status_uptime_ms_add(&s_builder, (uint64_t)HAL_GetTick());
    hwlink_Status_slot_add(&s_builder, (hwlink_Slot_enum_t)hwval_boot_slot());
    hwlink_Status_trial_add(&s_builder,
                            (hwlink_TrialState_enum_t)hwval_boot_trial());
    hwlink_Status_trial_count_add(&s_builder, hwval_boot_trial_count());
    hwlink_Status_health_add(&s_builder,
                             (hwlink_HealthState_enum_t)hwval_boot_health());
    hwlink_Status_reset_cause_add(&s_builder, cause);
    hwlink_Status_reset_cause_text_add(&s_builder, cause_text);
    hwlink_Status_heap_free_add(&s_builder, (uint32_t)xPortGetFreeHeapSize());
    hwlink_Status_heap_min_free_add(&s_builder,
                                    (uint32_t)xPortGetMinimumEverFreeHeapSize());
    hwlink_Status_protocol_major_add(&s_builder, (uint16_t)HWLINK_PROTOCOL_MAJOR);
    hwlink_Status_protocol_minor_add(&s_builder, (uint16_t)HWLINK_PROTOCOL_MINOR);
    hwlink_Status_ref_t status = hwlink_Status_end(&s_builder);
    return link_end(hwlink_Body_as_Status(status), seq,
                    HWLINK_FLAG_RESPONSE, 1);
}

static bool link_send_log(hwlink_LogLevel_enum_t level,
                          const char *text, size_t len)
{
    if (!link_begin()) { return false; }
    flatbuffers_string_ref_t str =
        flatbuffers_string_create(&s_builder, text, len);
    hwlink_Log_ref_t log = hwlink_Log_create(&s_builder, level, str);
    return link_end(hwlink_Body_as_Log(log), 0U, HWLINK_FLAG_EVENT, 0);
}

/* --------------------------------------------------- text sink (LINK-002) */
static void link_log_flush(hwlink_LogLevel_enum_t level)
{
    if (s_quiet != 0) {
        s_log_len = 0U;
        return;
    }
    if (s_log_len > 0U) {
        (void)link_send_log(level, s_log_buf, s_log_len);
        s_log_len = 0U;
    }
}

static void link_text_sink(const char *data, size_t len)
{
    if ((s_log_mutex != NULL) &&
        (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(LINK_TX_TIMEOUT_MS)) != pdPASS)) {
        return;
    }
    for (size_t i = 0U; i < len; i++) {
        char c = data[i];

        if (c == '\r') {
            continue;
        }
        if (s_log_len < (LINK_LOG_TEXT_MAX - 1U)) {
            s_log_buf[s_log_len++] = c;
        }
        if ((c == '\n') || (s_log_len >= (LINK_LOG_TEXT_MAX - 1U))) {
            link_log_flush(hwlink_LogLevel_INFO);
        }
    }
    if (s_log_mutex != NULL) {
        (void)xSemaphoreGive(s_log_mutex);
    }
}

/* -------------------------------------------------------- reset flag ---- */
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

void hwval_link_set_boot_flag(int on)
{
    bkp_unlock();
    RTC->BKP7R = (on != 0) ? LINK_BKP_MAGIC : 0U;
    bkp_lock();
}

static uint32_t persist_crc(const struct link_persist *p)
{
    return crc32_buf((const uint8_t *)&p->req,
                     sizeof(*p) - 2U * sizeof(uint32_t));
}

static void persist_save(void)
{
    s_persist.magic = LINK_JOB_MAGIC;
    s_persist.crc = persist_crc(&s_persist);
}

static void persist_clear(void)
{
    (void)memset(&s_persist, 0, sizeof(s_persist));
}

static int persist_restore(struct tfw_run_req *req, uint32_t *next)
{
    if ((s_persist.magic != LINK_JOB_MAGIC) ||
        (s_persist.crc != persist_crc(&s_persist)) ||
        (s_persist.active == 0U)) {
        return 0;
    }
    *req = s_persist.req;
    *next = s_persist.next_index;
    return 1;
}

void hwval_link_boot_init(void)
{
    if (s_tx_mutex == NULL) {
        s_tx_mutex = xSemaphoreCreateMutex();
    }
    if (s_log_mutex == NULL) {
        s_log_mutex = xSemaphoreCreateMutex();
    }
    if (s_answer_q == NULL) {
        s_answer_q = xQueueCreate(LINK_ANSWER_QUEUE_LEN,
                                  sizeof(struct link_answer));
    }
    /* The builder's emitter and allocator are set here; without this the
     * first reply calls a NULL emit function and HardFaults. */
    if (flatcc_builder_init(&s_builder) != 0) {
        hwval_fault_assert(__FILE__, __LINE__);
    }

    bkp_unlock();
    s_boot_active = (RTC->BKP7R == LINK_BKP_MAGIC) ? 1 : 0;
    bkp_lock();
    if (s_boot_active != 0) {
        g_set.mode = HWVAL_MODE_LINK;
        s_boot_report = 1;
        if (persist_restore(&s_resume_req, &s_resume_req.start_index) != 0) {
            s_resume_req.resumed = 1U;
            s_have_resume = 1;
        }
    }
}

int hwval_link_boot_active(void) { return s_boot_active; }
int hwval_link_active(void) { return s_active; }

void hwval_link_set_quiet(int on) { s_quiet = (on != 0) ? 1 : 0; }

/* ------------------------------------------------------- boot report ---- */
static void link_boot_report(void)
{
    if (s_boot_report == 0) {
        return;
    }
    s_boot_report = 0;
    hwval_version_banner();
    hwval_reset_cause_report();
    hwval_flash_bank_report();
    hwval_bootcheck_evaluate();
    hwval_rtos_report_panic();
}

/* --------------------------------------------------------- prompt queue - */
static void answer_post(uint32_t id, const char *text)
{
    struct link_answer a;

    (void)memset(&a, 0, sizeof(a));
    a.id = id;
    (void)snprintf(a.text, sizeof(a.text), "%s", (text != NULL) ? text : "");
    if (s_answer_q != NULL) {
        (void)xQueueSend(s_answer_q, &a, 0U);
    }
}

static void link_send_prompt_done(uint32_t id, const char *answer)
{
    if (!link_begin()) { return; }
    hwlink_OperatorPromptDone_ref_t ev = hwlink_OperatorPromptDone_create(
        &s_builder, id, flatbuffers_string_create_str(&s_builder, answer));
    (void)link_end(hwlink_Body_as_OperatorPromptDone(ev), 0U,
                   HWLINK_FLAG_EVENT, 0);
}

static int b1_pressed(void)
{
    static int s_b1_ready;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    if (s_b1_ready == 0) {
        GPIO_InitTypeDef io = {
            .Pin = GPIO_PIN_13, .Mode = GPIO_MODE_INPUT,
            .Pull = GPIO_PULLDOWN, .Speed = GPIO_SPEED_FREQ_LOW
        };
        HAL_GPIO_Init(GPIOC, &io);
        s_b1_ready = 1;
    }
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) ? 1 : 0;
}

/* LINK-008/009: OperatorPrompt events, with B1 auto-completion. */
int hwval_link_prompt(hwval_prompt_kind_t kind, const char *question,
                      const char *answers, uint32_t timeout_s,
                      char *out, size_t out_len)
{
    uint32_t id;
    TickType_t deadline;

    if ((s_active == 0) || (g_set.mode != HWVAL_MODE_LINK)) {
        return -2;
    }
    if (out == NULL || out_len == 0U) {
        return -1;
    }
    out[0] = '\0';

    id = s_prompt_next++;
    if (s_prompt_next == 0U) {
        s_prompt_next = 1U;
    }
    s_prompt_waiting = id;
    s_prompt_cancel = 0;

    if (!link_begin()) {
        s_prompt_waiting = 0U;
        return -1;
    }
    flatbuffers_string_ref_t test_id =
        flatbuffers_string_create_str(&s_builder,
            (hwval_test_current_id() != NULL) ? hwval_test_current_id() : "");
    flatbuffers_string_ref_t text =
        flatbuffers_string_create_str(&s_builder, (question != NULL) ? question : "");
    flatbuffers_string_ref_t ans =
        flatbuffers_string_create_str(&s_builder, (answers != NULL) ? answers : "");
    hwlink_OperatorPrompt_ref_t prompt = hwlink_OperatorPrompt_create(
        &s_builder, id, test_id, prompt_enum(kind), text, ans, timeout_s);
    (void)link_end(hwlink_Body_as_OperatorPrompt(prompt), 0U,
                   HWLINK_FLAG_EVENT, 0);

    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_s * 1000U);
    for (;;) {
        struct link_answer a;

        if ((s_prompt_cancel != 0) || (s_text_requested != 0) ||
            (s_active == 0)) {
            s_prompt_waiting = 0U;
            link_send_prompt_done(id, "cancelled");
            return -1;
        }
        if ((s_answer_q != NULL) &&
            (xQueueReceive(s_answer_q, &a, pdMS_TO_TICKS(5)) == pdPASS)) {
            if (a.id == id) {
                (void)snprintf(out, out_len, "%s", a.text);
                s_prompt_waiting = 0U;
                return 0;
            }
        }
        if ((kind == HWVAL_PROMPT_BUTTON) && (b1_pressed() != 0)) {
            const char *done = "B1 press detected; prompt completed";
            (void)link_send_log(hwlink_LogLevel_INFO, done, strlen(done));
            link_send_prompt_done(id, "pressed");
            (void)snprintf(out, out_len, "pressed");
            s_prompt_waiting = 0U;
            return 0;
        }
        if (xTaskGetTickCount() >= deadline) {
            s_prompt_waiting = 0U;
            link_send_prompt_done(id, "timeout");
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ------------------------------------------------ structured result ---- */
static int first_number(const char *s, const char **end, double *value)
{
    const char *p = s;

    while ((*p != '\0') &&
           (!isdigit((unsigned char)*p)) && (*p != '-') && (*p != '.') &&
           (*p != '+')) {
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

static void unit_after(const char *p, char *unit, size_t cap)
{
    size_t n = 0U;

    while ((*p == ' ') || (*p == '\t')) { p++; }
    while ((*p != '\0') && (*p != ' ') && (*p != ',') && (*p != ';') &&
           (*p != ')') && (*p != '\r') && (*p != '\n') && (n + 1U < cap)) {
        unit[n++] = *p++;
    }
    unit[n] = '\0';
}

static void fill_measurement(hwlink_Measurement_ref_t *out, const char *text)
{
    const char *end = NULL;
    double value = 0.0;
    char unit[12] = "";

    if (first_number(text, &end, &value) != 0) {
        unit_after(end, unit, sizeof(unit));
        *out = hwlink_Measurement_create(&s_builder, value,
            flatbuffers_string_create_str(&s_builder, unit),
            flatbuffers_string_create_str(&s_builder, text), 1U);
    } else {
        *out = hwlink_Measurement_create(&s_builder, 0.0,
            flatbuffers_string_create_str(&s_builder, ""),
            flatbuffers_string_create_str(&s_builder, text), 0U);
    }
}

static void fill_limits(hwlink_Limits_ref_t *out, const char *text)
{
    const char *end = NULL;
    const char *scan = NULL;
    double a = 0.0;
    double b = 0.0;
    double min = 0.0;
    double max = 0.0;
    int valid = 0;
    char unit[12] = "";

    if (first_number(text, &end, &a) != 0) {
        scan = end;
        unit_after(end, unit, sizeof(unit));
        if (strstr(text, "<=") != NULL) {
            min = 0.0;
            max = a;
            valid = 1;
        } else if (strstr(text, ">=") != NULL) {
            min = a;
            max = 1.0e12;
            valid = 1;
        } else if ((strstr(text, "..") != NULL) ||
                   (strstr(text, " to ") != NULL)) {
            if (first_number(scan, &end, &b) != 0) {
                min = (a <= b) ? a : b;
                max = (a <= b) ? b : a;
                valid = 1;
                unit_after(end, unit, sizeof(unit));
            }
        } else if (strstr(text, "+-") != NULL) {
            if (first_number(scan, &end, &b) != 0) {
                if (strchr(text, '%') != NULL) {
                    min = a * (1.0 - (b / 100.0));
                    max = a * (1.0 + (b / 100.0));
                } else {
                    min = a - b;
                    max = a + b;
                }
                valid = 1;
                unit_after(end, unit, sizeof(unit));
            }
        }
    }
    *out = hwlink_Limits_create(&s_builder, min, max,
        flatbuffers_string_create_str(&s_builder, unit),
        flatbuffers_string_create_str(&s_builder, text), (flatbuffers_bool_t)valid);
}

/* ------------------------------------------------------ run persistence - */
static void persist_start(const struct tfw_run_req *req)
{
    (void)memset(&s_persist, 0, sizeof(s_persist));
    s_persist.req = *req;
    s_persist.next_index = 0U;
    s_persist.active = 1U;
    persist_save();
}

static void persist_progress(uint32_t next_index)
{
    if (s_persist.active == 0U) {
        return;
    }
    s_persist.next_index = next_index;
    persist_save();
}

/* ---------------------------------------------------------- event hooks - */
static void hook_run_started(uint32_t total, int resumed)
{
    s_run_total = total;
    s_run_done = 0U;
    s_progress_tick = (uint32_t)xTaskGetTickCount();
    if (!link_begin()) { return; }
    hwlink_RunStarted_ref_t ev = hwlink_RunStarted_create(
        &s_builder, total, (flatbuffers_bool_t)(resumed != 0));
    (void)link_end(hwlink_Body_as_RunStarted(ev), 0U, HWLINK_FLAG_EVENT, 0);
}

static void hook_test_started(const struct tfw_case *c, uint32_t index,
                              uint32_t total, uint32_t absolute_index)
{
    s_run_done = index;
    s_run_total = total;
    /* Resume position is an absolute index into the ordered table, not the
     * filtered position shown in TestStarted (LINK-010). */
    persist_progress(absolute_index + 1U);
    if (!link_begin()) { return; }
    flatbuffers_string_ref_t id =
        flatbuffers_string_create_str(&s_builder, c->id);
    hwlink_TestStarted_ref_t ev =
        hwlink_TestStarted_create(&s_builder, id, index, total);
    (void)link_end(hwlink_Body_as_TestStarted(ev), 0U, HWLINK_FLAG_EVENT, 0);
}

/* LINK-006 / TFW-013: every text/JSON field plus structured values. */
static void hook_test_result(const struct tfw_case *c,
                             const hwval_test_result_t *r)
{
    hwlink_Measurement_ref_t meas;
    hwlink_Limits_ref_t lim;
    hwlink_TestResult_ref_t ev;

    if (!link_begin()) { return; }
    fill_measurement(&meas, r->measured);
    fill_limits(&lim, r->limits);
    flatbuffers_string_ref_t id =
        flatbuffers_string_create_str(&s_builder, c->id);
    flatbuffers_string_ref_t group =
        flatbuffers_string_create_str(&s_builder, c->group);
    flatbuffers_string_ref_t reason =
        flatbuffers_string_create_str(&s_builder, r->reason);
    flatbuffers_string_ref_t reqs =
        flatbuffers_string_create_str(&s_builder, c->verifies);
    ev = hwlink_TestResult_create(&s_builder, id, group, verdict_enum(r->result),
                                  r->duration_ms, reason, meas, lim, reqs);
    (void)link_end(hwlink_Body_as_TestResult(ev), 0U, HWLINK_FLAG_EVENT, 0);
}

static void hook_run_finished(uint32_t total, uint32_t pass, uint32_t fail,
                              uint32_t skip, uint32_t na, int aborted,
                              int resumed)
{
    s_run_total = 0U;
    s_run_done = 0U;
    if (s_persist.active != 0U) {
        s_persist.aborted = (uint8_t)(aborted != 0);
        persist_save();
    }
    persist_clear();
    if (!link_begin()) { return; }
    hwlink_RunFinished_ref_t ev = hwlink_RunFinished_create(
        &s_builder, total, (uint16_t)pass, (uint16_t)fail, (uint16_t)skip,
        (uint16_t)na, (flatbuffers_bool_t)(aborted != 0),
        (flatbuffers_bool_t)(resumed != 0));
    (void)link_end(hwlink_Body_as_RunFinished(ev), 0U, HWLINK_FLAG_EVENT, 0);
}

static const struct tfw_hooks s_hooks = {
    .run_started  = hook_run_started,
    .test_started = hook_test_started,
    .test_result  = hook_test_result,
    .run_finished = hook_run_finished,
};

/* ------------------------------------------------------ list/query replies */
static size_t group_total(void)
{
    size_t n = 0U;

    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const struct tfw_case *c = hwval_test_at(i);
        int seen = 0;
        for (size_t j = 0U; j < i; j++) {
            if (strcmp(c->group, hwval_test_at(j)->group) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen == 0) {
            n++;
        }
    }
    return n;
}

static const char *group_at(size_t index)
{
    size_t n = 0U;

    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const struct tfw_case *c = hwval_test_at(i);
        int seen = 0;
        for (size_t j = 0U; j < i; j++) {
            if (strcmp(c->group, hwval_test_at(j)->group) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen == 0) {
            if (n == index) {
                return c->group;
            }
            n++;
        }
    }
    return NULL;
}

static uint16_t group_test_count(const char *code)
{
    uint16_t n = 0U;

    for (size_t i = 0U; i < hwval_test_count(); i++) {
        if (strcmp(hwval_test_at(i)->group, code) == 0) {
            n++;
        }
    }
    return n;
}

/* LINK-004: both lists are generated from the single registered test table. */
static bool link_send_group_list(uint16_t seq, uint16_t page)
{
    size_t total = group_total();
    size_t pages = (total + LINK_GROUP_PAGE - 1U) / LINK_GROUP_PAGE;
    size_t start = (size_t)page * LINK_GROUP_PAGE;
    hwlink_GroupInfo_ref_t refs[LINK_GROUP_PAGE];
    size_t n = 0U;

    if (start >= total) {
        start = 0U;
        n = 0U;
    } else {
        size_t left = total - start;
        n = (left > LINK_GROUP_PAGE) ? LINK_GROUP_PAGE : left;
    }

    if (!link_begin()) { return false; }
    for (size_t i = 0U; i < n; i++) {
        const char *code = group_at(start + i);
        refs[i] = hwlink_GroupInfo_create(&s_builder,
            flatbuffers_string_create_str(&s_builder, code),
            flatbuffers_string_create_str(&s_builder, group_name(code)),
            group_test_count(code));
    }
    hwlink_GroupList_start(&s_builder);
    hwlink_GroupList_page_add(&s_builder, page);
    hwlink_GroupList_page_count_add(&s_builder, (uint16_t)pages);
    hwlink_GroupList_total_add(&s_builder, (uint16_t)total);
    hwlink_GroupList_groups_start(&s_builder);
    for (size_t i = 0U; i < n; i++) {
        hwlink_GroupList_groups_push(&s_builder, refs[i]);
    }
    hwlink_GroupList_groups_end(&s_builder);
    return link_end(hwlink_Body_as_GroupList(hwlink_GroupList_end(&s_builder)),
                    seq, HWLINK_FLAG_RESPONSE, 1);
}

static bool link_send_test_list(uint16_t seq, const char *filter, uint16_t page)
{
    size_t total = 0U;
    size_t seen = 0U;
    size_t start = (size_t)page * LINK_TEST_PAGE;
    hwlink_TestInfo_ref_t refs[LINK_TEST_PAGE];
    size_t n = 0U;

    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const struct tfw_case *c = hwval_test_at(i);
        if ((filter == NULL) || (filter[0] == '\0') ||
            (strcmp(c->group, filter) == 0)) {
            total++;
        }
    }
    if (start >= total) {
        start = 0U;
    }

    if (!link_begin()) { return false; }
    for (size_t i = 0U; i < hwval_test_count(); i++) {
        const struct tfw_case *c = hwval_test_at(i);
        if ((filter != NULL) && (filter[0] != '\0') &&
            (strcmp(c->group, filter) != 0)) {
            continue;
        }
        if (seen++ < start) {
            continue;
        }
        if (n >= LINK_TEST_PAGE) {
            break;
        }
        char needs[32];
        (void)snprintf(needs, sizeof(needs), "%s%s%s",
                       (c->needs_operator != 0U) ? "operator" : "",
                       (c->needs_operator != 0U) && (c->destructive != 0U) ? "," : "",
                       (c->destructive != 0U) ? "destructive" : "");
        refs[n++] = hwlink_TestInfo_create(&s_builder,
            flatbuffers_string_create_str(&s_builder, c->id),
            flatbuffers_string_create_str(&s_builder, c->group),
            flatbuffers_string_create_str(&s_builder, c->title),
            level_enum(c->level), (flatbuffers_bool_t)(c->needs_operator != 0U),
            (flatbuffers_bool_t)(c->destructive != 0U), c->timeout_ms,
            flatbuffers_string_create_str(&s_builder, needs),
            flatbuffers_string_create_str(&s_builder, c->verifies));
    }
    hwlink_TestList_start(&s_builder);
    hwlink_TestList_page_add(&s_builder, page);
    hwlink_TestList_page_count_add(&s_builder,
        (uint16_t)((total + LINK_TEST_PAGE - 1U) / LINK_TEST_PAGE));
    hwlink_TestList_total_add(&s_builder, (uint16_t)total);
    hwlink_TestList_tests_start(&s_builder);
    for (size_t i = 0U; i < n; i++) {
        hwlink_TestList_tests_push(&s_builder, refs[i]);
    }
    hwlink_TestList_tests_end(&s_builder);
    return link_end(hwlink_Body_as_TestList(hwlink_TestList_end(&s_builder)),
                    seq, HWLINK_FLAG_RESPONSE, 1);
}

static bool link_send_harness(uint16_t seq, uint16_t page)
{
    uint64_t mask = hwval_harness_detected();
    size_t total = HWVAL_WIRE_TABLE_N;
    size_t start = (size_t)page * LINK_HARNESS_PAGE;
    hwlink_HarnessWire_ref_t refs[LINK_HARNESS_PAGE];
    size_t n = 0U;
    char pin_a[8];
    char pin_b[8];

    if (start >= total) { start = 0U; }
    if (!link_begin()) { return false; }
    for (size_t i = start; (i < total) && (n < LINK_HARNESS_PAGE); i++) {
        const struct hwval_wire *w = hwval_wire_at((int)i);
        (void)snprintf(pin_a, sizeof(pin_a), "P%c%u", 'A' + w->ap, w->an);
        (void)snprintf(pin_b, sizeof(pin_b), "P%c%u", 'A' + w->bp, w->bn);
        refs[n++] = hwlink_HarnessWire_create(&s_builder,
            flatbuffers_string_create_str(&s_builder, w->name),
            flatbuffers_string_create_str(&s_builder, pin_a),
            flatbuffers_string_create_str(&s_builder, pin_b),
            flatbuffers_string_create_str(&s_builder, w->used_by),
            (flatbuffers_bool_t)(((mask >> i) & 1ULL) != 0U),
            (flatbuffers_bool_t)(w->verify == 0U));
    }
    hwlink_HarnessInfo_start(&s_builder);
    hwlink_HarnessInfo_fitted_mask_add(&s_builder, mask);
    hwlink_HarnessInfo_count_add(&s_builder, (uint16_t)total);
    hwlink_HarnessInfo_page_add(&s_builder, page);
    hwlink_HarnessInfo_page_count_add(&s_builder,
        (uint16_t)((total + LINK_HARNESS_PAGE - 1U) / LINK_HARNESS_PAGE));
    hwlink_HarnessInfo_wires_start(&s_builder);
    for (size_t i = 0U; i < n; i++) {
        hwlink_HarnessInfo_wires_push(&s_builder, refs[i]);
    }
    hwlink_HarnessInfo_wires_end(&s_builder);
    return link_end(hwlink_Body_as_HarnessInfo(hwlink_HarnessInfo_end(&s_builder)),
                    seq, HWLINK_FLAG_RESPONSE, 1);
}

static bool link_send_device_info(uint16_t seq)
{
    hwval_clock_tree_t t;
    char uid[32];
    char pkg[16];
    uint32_t devid = DBGMCU->IDCODE;
    uint32_t reset = hwval_reset_flags_snapshot();
    const char *reset_text;

    hwval_clock_get_tree(&t);
    (void)snprintf(uid, sizeof(uid), "%08lX%08lX%08lX",
        (unsigned long)HAL_GetUIDw0(), (unsigned long)HAL_GetUIDw1(),
        (unsigned long)HAL_GetUIDw2());
    (void)snprintf(pkg, sizeof(pkg), "%08lX",
        (unsigned long)(*(const uint32_t *)0x1FF0F7E0U));
    reset_text = hwval_reset_cause_name((reset != 0U) ? reset : hwval_reset_cause_flags());

    if (!link_begin()) { return false; }
    hwlink_DeviceInfo_ref_t info = hwlink_DeviceInfo_create(&s_builder,
        flatbuffers_string_create_str(&s_builder, HWVAL_FIRMWARE_NAME),
        flatbuffers_string_create_str(&s_builder, hwval_version_string()),
        flatbuffers_string_create_str(&s_builder, hwval_git_commit()),
        flatbuffers_string_create_str(&s_builder, hwval_build_date()),
        flatbuffers_string_create_str(&s_builder, hwval_srs_id()),
        devid & 0xFFFU, devid >> 16U, SCB->CPUID,
        hwval_flash_size_kb(),
        flatbuffers_string_create_str(&s_builder, pkg),
        flatbuffers_string_create_str(&s_builder, uid),
        *(const uint32_t *)0x40023C14U, *(const uint32_t *)0x40023C18U,
        t.sysclk_hz, t.hclk_hz, t.pclk1_hz, t.pclk2_hz,
        t.apb1_tim_hz, t.apb2_tim_hz, t.pllq_hz,
        reset, flatbuffers_string_create_str(&s_builder, reset_text),
        (uint64_t)HAL_GetTick());
    return link_end(hwlink_Body_as_DeviceInfo(info), seq,
                    HWLINK_FLAG_RESPONSE, 1);
}

static bool link_send_results(uint16_t seq, uint16_t page)
{
    size_t total = hwval_test_count();
    size_t start = (size_t)page * LINK_RESULT_PAGE;
    hwlink_TestResult_ref_t refs[LINK_RESULT_PAGE];
    size_t n = 0U;

    if (start >= total) {
        start = 0U;
    }
    if (!link_begin()) { return false; }
    for (size_t i = start; (i < total) && (n < LINK_RESULT_PAGE); i++) {
        const struct tfw_case *c = hwval_test_at(i);
        const hwval_test_result_t *r = hwval_test_result_of(i);
        hwlink_Measurement_ref_t meas;
        hwlink_Limits_ref_t lim;
        fill_measurement(&meas, r->measured);
        fill_limits(&lim, r->limits);
        refs[n++] = hwlink_TestResult_create(&s_builder,
            flatbuffers_string_create_str(&s_builder, c->id),
            flatbuffers_string_create_str(&s_builder, c->group),
            verdict_enum(r->result), r->duration_ms,
            flatbuffers_string_create_str(&s_builder, r->reason),
            meas, lim,
            flatbuffers_string_create_str(&s_builder, c->verifies));
    }
    hwlink_ResultList_start(&s_builder);
    hwlink_ResultList_page_add(&s_builder, page);
    hwlink_ResultList_page_count_add(&s_builder,
        (uint16_t)((total + LINK_RESULT_PAGE - 1U) / LINK_RESULT_PAGE));
    hwlink_ResultList_total_add(&s_builder, (uint16_t)total);
    hwlink_ResultList_results_start(&s_builder);
    for (size_t i = 0U; i < n; i++) {
        hwlink_ResultList_results_push(&s_builder, refs[i]);
    }
    hwlink_ResultList_results_end(&s_builder);
    return link_end(hwlink_Body_as_ResultList(hwlink_ResultList_end(&s_builder)),
                    seq, HWLINK_FLAG_RESPONSE, 1);
}

/* TFW-020/021: report text ends with REPORT END <PASS|FAIL>. */
static bool link_send_report(uint16_t seq)
{
    char text[1500];
    uint16_t pass = 0U, fail = 0U, skip = 0U, na = 0U;
    size_t used = hwval_test_report(text, sizeof(text), &pass, &fail, &skip,
                                    &na);

    if (!link_begin()) { return false; }
    hwlink_Report_ref_t rep = hwlink_Report_create(&s_builder,
        flatbuffers_string_create(&s_builder, text, used),
        pass, fail, skip, na, (uint16_t)hwval_test_count(),
        (flatbuffers_bool_t)(fail == 0U));
    return link_end(hwlink_Body_as_Report(rep), seq, HWLINK_FLAG_RESPONSE, 1);
}

static int setting_get(const char *key, char *out, size_t n)
{
    if (key == NULL) {
        out[0] = '\0';
        return 0;
    }
    if (strcmp(key, "operator-timeout") == 0) {
        (void)snprintf(out, n, "%lu", (unsigned long)g_set.operator_timeout_s);
    } else if (strcmp(key, "fault-action") == 0) {
        (void)snprintf(out, n, "%s", (g_set.fault_action != 0) ? "reset" : "halt");
    } else if (strcmp(key, "color") == 0) {
        (void)snprintf(out, n, "%s", g_set.color ? "on" : "off");
    } else if (strcmp(key, "log") == 0) {
        (void)snprintf(out, n, "%d", g_set.log_level);
    } else if (strcmp(key, "format") == 0) {
        (void)snprintf(out, n, "%s", hwval_format_name(g_set.format));
    } else {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

static int setting_set(const char *key, const char *value)
{
    if (key == NULL) {
        return 0;
    }
    if (value == NULL) {
        value = "";
    }
    if (strcmp(key, "operator-timeout") == 0) {
        g_set.operator_timeout_s = (uint32_t)strtoul(value, NULL, 10);
    } else if (strcmp(key, "fault-action") == 0) {
        int a = (strcmp(value, "reset") == 0) ? 1 : 0;
        g_set.fault_action = a;
        hwval_fault_set_action((a != 0) ? HWVAL_FAULT_RESET : HWVAL_FAULT_HALT);
    } else if (strcmp(key, "color") == 0) {
        g_set.color = (strcmp(value, "on") == 0) ? 1 : 0;
    } else if (strcmp(key, "log") == 0) {
        g_set.log_level = (int)strtoul(value, NULL, 10);
    } else if (strcmp(key, "format") == 0) {
        g_set.format = (strcmp(value, "json") == 0) ? HWVAL_FMT_JSON : HWVAL_FMT_TEXT;
    } else {
        return 0;
    }
    return 1;
}

static bool link_send_setting(uint16_t seq, const char *key)
{
    char value[32];
    int known;

    if (key == NULL) {
        key = "";
    }
    known = setting_get(key, value, sizeof(value));
    if (!link_begin()) { return false; }
    hwlink_Setting_ref_t s = hwlink_Setting_create(&s_builder,
        flatbuffers_string_create_str(&s_builder, key),
        flatbuffers_string_create_str(&s_builder, value),
        (flatbuffers_bool_t)known);
    return link_end(hwlink_Body_as_Setting(s), seq, HWLINK_FLAG_RESPONSE, 1);
}

/* ------------------------------------------------------------ Run request */
static void parse_levels(const char *levels, uint8_t ok3[3])
{
    ok3[0] = ok3[1] = ok3[2] = 0U;
    if ((levels == NULL) || (levels[0] == '\0')) {
        ok3[0] = ok3[1] = ok3[2] = 1U;
        return;
    }
    for (const char *p = levels; *p != '\0'; p++) {
        if ((*p == 'S') || (*p == 's')) { ok3[0] = 1U; }
        else if ((*p == 'L') || (*p == 'l')) { ok3[1] = 1U; }
        else if ((*p == 'E') || (*p == 'e')) { ok3[2] = 1U; }
    }
    if ((ok3[0] | ok3[1] | ok3[2]) == 0U) {
        ok3[0] = ok3[1] = ok3[2] = 1U;
    }
}

static bool link_handle_run(uint16_t seq, hwlink_Run_table_t run)
{
    struct tfw_run_req req;
    flatbuffers_string_vec_t ids = hwlink_Run_ids(run);
    flatbuffers_string_vec_t groups = hwlink_Run_groups(run);
    const char *levels = hwlink_Run_levels(run);

    (void)memset(&req, 0, sizeof(req));
    req.operator_ok = (uint8_t)(hwlink_Run_allow_operator(run) != 0U);
    req.destructive_ok = (uint8_t)(hwlink_Run_allow_destructive(run) != 0U);
    req.all = (uint8_t)(hwlink_Run_all(run) != 0U);
    req.repeat = hwlink_Run_repeat(run);
    if (req.repeat == 0U) { req.repeat = 1U; }
    req.until_fail = (uint8_t)(hwlink_Run_until_fail(run) != 0U);
    parse_levels(levels, req.level_ok);

    for (size_t i = 0U; i < flatbuffers_string_vec_len(ids); i++) {
        if (req.id_count >= HWVAL_RUN_MAX_SELECT) { break; }
        (void)snprintf(req.ids[req.id_count], HWVAL_RUN_SELECT_LEN, "%s",
                       flatbuffers_string_vec_at(ids, i));
        req.id_count++;
    }
    for (size_t i = 0U; i < flatbuffers_string_vec_len(groups); i++) {
        if (req.group_count >= HWVAL_RUN_MAX_SELECT) { break; }
        (void)snprintf(req.groups[req.group_count], HWVAL_RUN_SELECT_LEN, "%s",
                       flatbuffers_string_vec_at(groups, i));
        req.group_count++;
    }
    if ((req.all == 0U) && (req.id_count == 0U) && (req.group_count == 0U)) {
        return link_send_nak(seq, hwlink_Error_BAD_ARGUMENT, "empty selection");
    }
    /* LINK-005 / error codes: reject unknown ids and groups instead of
     * silently running an empty selection. */
    for (size_t i = 0U; i < req.id_count; i++) {
        int found = 0;
        for (size_t j = 0U; j < hwval_test_count(); j++) {
            if (strcmp(hwval_test_at(j)->id, req.ids[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (found == 0) {
            return link_send_nak(seq, hwlink_Error_NOT_FOUND, "test id");
        }
    }
    for (size_t i = 0U; i < req.group_count; i++) {
        int found = 0;
        for (size_t j = 0U; j < hwval_test_count(); j++) {
            if (strcmp(hwval_test_at(j)->group, req.groups[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (found == 0) {
            return link_send_nak(seq, hwlink_Error_NOT_FOUND, "group");
        }
    }
    if (hwval_test_active() != 0) {
        return link_send_nak(seq, hwlink_Error_BUSY, "run active");
    }
    if (hwval_test_submit(&req) != 0) {
        return link_send_nak(seq, hwlink_Error_BUSY, "runner busy");
    }
    persist_start(&req);
    return link_send_ack(seq);
}

/* -------------------------------------------------------- request router - */
static void link_handle_payload(uint16_t seq, const uint8_t *payload, size_t len)
{
    if (hwlink_Message_verify_as_root(payload, len) != flatcc_verify_ok) {
        (void)link_send_nak(seq, hwlink_Error_MALFORMED, "flatbuffer verifier");
        return;
    }
    hwlink_Message_table_t msg = hwlink_Message_as_root(payload);
    hwlink_Body_union_t body = hwlink_Message_body_union(msg);

    switch (body.type) {
    case hwlink_Body_Hello:
        (void)link_send_hello(seq);
        break;
    case hwlink_Body_Ping:
        (void)link_send_pong(seq);
        break;
    case hwlink_Body_SetBaud: {
        uint32_t rate = hwlink_SetBaud_rate((hwlink_SetBaud_table_t)body.value);
        if ((rate != 115200U) && (rate != 921600U)) {
            (void)link_send_nak(seq, hwlink_Error_BAD_ARGUMENT, "baud rate");
            break;
        }
        if (!link_send_ack(seq)) { break; }
        hwval_uart3_drain(100U);
        link_reset_parser();
        if (hwval_uart3_set_baud(rate) == 0) {
            s_baud_pending = (rate != 115200U);
            s_baud_last_valid = (uint32_t)xTaskGetTickCount();
        }
        break;
    }
    case hwlink_Body_Reset:
        persist_clear();
        if (link_send_ack(seq)) {
            hwval_dbg('I', "HWLINK: Reset");
            hwval_dbg_flush(20U);
            hwval_uart3_drain(100U);
            NVIC_SystemReset();
        }
        break;
    case hwlink_Body_GetStatus:
        (void)link_send_status(seq);
        break;
    case hwlink_Body_EnterBootloader:
        /* bootctl update/info: reset into BOOT and stay there (ENT-002). */
        persist_clear();
        if (link_send_ack(seq)) {
            hwval_dbg('I', "HWLINK: EnterBootloader, resetting into BOOT");
            hwval_dbg_flush(20U);
            hwval_uart3_drain(100U);
            hwval_boot_request_bootloader();
            NVIC_SystemReset();
        }
        break;
    case hwlink_Body_GetDeviceInfo:
        (void)link_send_device_info(seq);
        break;
    case hwlink_Body_ListGroups:
        (void)link_send_group_list(seq,
            hwlink_ListGroups_page((hwlink_ListGroups_table_t)body.value));
        break;
    case hwlink_Body_ListTests: {
        hwlink_ListTests_table_t lt = (hwlink_ListTests_table_t)body.value;
        const char *filter = hwlink_ListTests_group(lt);
        (void)link_send_test_list(seq, filter, hwlink_ListTests_page(lt));
        break;
    }
    case hwlink_Body_GetHarness:
        (void)link_send_harness(seq,
            hwlink_GetHarness_page((hwlink_GetHarness_table_t)body.value));
        break;
    case hwlink_Body_Run:
        (void)link_handle_run(seq, (hwlink_Run_table_t)body.value);
        break;
    case hwlink_Body_OperatorAnswer: {
        hwlink_OperatorAnswer_table_t a =
            (hwlink_OperatorAnswer_table_t)body.value;
        uint32_t id = hwlink_OperatorAnswer_prompt_id(a);
        flatbuffers_string_t ans = hwlink_OperatorAnswer_answer(a);
        if (s_prompt_waiting == id) {
            answer_post(id, ans);
            (void)link_send_ack(seq);
        } else {
            (void)link_send_nak(seq, hwlink_Error_NOT_FOUND, "prompt id");
        }
        break;
    }
    case hwlink_Body_Abort:
        hwval_test_request_abort();
        s_prompt_cancel = 1;
        (void)link_send_ack(seq);
        break;
    case hwlink_Body_GetResults:
        (void)link_send_results(seq,
            hwlink_GetResults_page((hwlink_GetResults_table_t)body.value));
        break;
    case hwlink_Body_ClearResults:
        hwval_results_clear();
        (void)link_send_ack(seq);
        break;
    case hwlink_Body_GetReport:
        (void)link_send_report(seq);
        break;
    case hwlink_Body_GetSetting: {
        flatbuffers_string_t key =
            hwlink_GetSetting_key((hwlink_GetSetting_table_t)body.value);
        (void)link_send_setting(seq, key);
        break;
    }
    case hwlink_Body_SetSetting: {
        hwlink_SetSetting_table_t st = (hwlink_SetSetting_table_t)body.value;
        const char *key = hwlink_SetSetting_key(st);
        const char *value = hwlink_SetSetting_value(st);
        if (setting_set(key, value) != 0) {
            (void)link_send_ack(seq);
        } else {
            (void)link_send_nak(seq, hwlink_Error_NOT_FOUND, "setting");
        }
        break;
    }
    case hwlink_Body_EnterTextMode:
        if (link_send_ack(seq)) {
            s_prompt_cancel = 1;
            s_text_requested = 1;
        }
        break;
    default:
        (void)link_send_nak(seq, hwlink_Error_UNSUPPORTED, "message");
        break;
    }
}

static void link_handle_raw(const uint8_t *raw, size_t raw_len)
{
    size_t payload_len = 0U;
    uint8_t flags = 0U;
    uint16_t seq = 0U;

    if (!hwlink_decode_raw(raw, raw_len, s_rx_payload, sizeof(s_rx_payload),
                           &payload_len, &flags, &seq)) {
        return; /* LNK-004: bad CRC is dropped silently */
    }
    s_baud_last_valid = (uint32_t)xTaskGetTickCount();
    if ((flags & (HWLINK_FLAG_RESPONSE | HWLINK_FLAG_EVENT)) != 0U) {
        return;
    }
    if ((s_last_valid != 0) && (seq == s_last_seq) &&
        (s_last_response_len > 0U)) {
        size_t encoded_len = 0U;
        if (link_lock()) {
            if (hwlink_cobs_encode(s_last_response, s_last_response_len,
                                   s_tx_frame, sizeof(s_tx_frame),
                                   &encoded_len)) {
                hwval_uart3_write_direct(s_tx_frame, encoded_len);
            }
            link_unlock();
        }
        return;
    }
    s_last_valid = (seq != 0U) ? 1 : 0;
    s_last_seq = seq;
    s_last_response_len = 0U;
    link_handle_payload(seq, s_rx_payload, payload_len);
}

/* A frame the text CLI received before it switched to link mode. */
static uint8_t s_prime[LINK_PRIME_MAX];
static size_t s_prime_len;

void hwval_link_prime(const uint8_t *frame, size_t len)
{
    if (len > sizeof(s_prime)) {
        len = 0U;   /* too long to replay; the host retries */
    }
    memcpy(s_prime, frame, len);
    s_prime_len = len;
}

static void link_feed_byte(uint8_t byte)
{
    if (byte == 0U) {
        if ((s_rx_overflow == 0) && (s_rx_len > 0U)) {
            size_t raw_len = 0U;
            if (hwlink_cobs_decode(s_rx_enc, s_rx_len, s_rx_raw,
                                   sizeof(s_rx_raw), &raw_len) == 0) {
                link_handle_raw(s_rx_raw, raw_len);
            }
        }
        link_reset_parser();
    } else if (s_rx_len < sizeof(s_rx_enc)) {
        s_rx_enc[s_rx_len++] = byte;
    } else {
        s_rx_overflow = 1;
    }
}

static void baud_revert_poll(void)
{
    if (s_baud_pending == 0) {
        return;
    }
    if ((uint32_t)(xTaskGetTickCount() - s_baud_last_valid) >
        pdMS_TO_TICKS(LINK_BAUD_REVERT_MS)) {
        (void)hwval_uart3_set_baud(115200U);
        s_baud_pending = 0;
    }
}

static void progress_poll(void)
{
    if (s_quiet != 0) {
        return;   /* clock test in progress: the baud is not valid */
    }
    uint32_t now = (uint32_t)xTaskGetTickCount();

    if ((s_run_total == 0U) || (hwval_test_active() == 0)) {
        s_progress_tick = now;
        return;
    }
    if ((uint32_t)(now - s_progress_tick) < pdMS_TO_TICKS(500U)) {
        return;
    }
    s_progress_tick = now;
    if (!link_begin()) { return; }
    flatbuffers_string_ref_t op =
        flatbuffers_string_create_str(&s_builder, "run");
    hwlink_Progress_ref_t ev = hwlink_Progress_create(
        &s_builder, op, s_run_done, s_run_total);
    (void)link_end(hwlink_Body_as_Progress(ev), 0U, HWLINK_FLAG_EVENT, 0);
}

static void text_probe_poll(void)
{
    if ((s_text_probe != 0) &&
        ((uint32_t)(xTaskGetTickCount() - s_text_probe_tick) >
         pdMS_TO_TICKS(LINK_TEXT_PROBE_MS))) {
        s_text_requested = 1; /* OPEN-011: text CLI fallback */
    }
}

/* ------------------------------------------------------------ main loop -- */
void hwval_link_run(void)
{
    uint32_t last_rx;

    s_active = 1;
    s_text_requested = 0;
    s_prompt_cancel = 0;
    s_prompt_waiting = 0U;
    s_baud_pending = 0;
    s_text_probe = 0;
    s_last_valid = 0;
    s_last_response_len = 0U;
    s_run_total = 0U;
    s_run_done = 0U;
    s_progress_tick = (uint32_t)xTaskGetTickCount();
    link_reset_parser();
    hwval_uart3_set_sink(link_text_sink);
    hwval_test_set_hooks(&s_hooks);

    link_boot_report();
    if (s_prime_len != 0U) {
        /* The CLI echoed the frame's printable bytes: a delimiter closes
         * that noise on the host as one bad frame before the reply. */
        static const uint8_t delimiter = 0U;
        hwval_uart3_write_direct(&delimiter, 1U);
        for (size_t i = 0U; i < s_prime_len; i++) {
            link_feed_byte(s_prime[i]);
        }
        s_prime_len = 0U;
        link_feed_byte(0U);
    }
    if (s_have_resume != 0) {
        s_have_resume = 0;
        (void)hwval_test_submit(&s_resume_req);
    }

    last_rx = (uint32_t)xTaskGetTickCount();
    while (s_text_requested == 0) {
        int c = hwval_uart3_getc();

        hwval_task_report(HWVAL_TASK_CLI);

        if (c >= 0) {
            uint32_t now = (uint32_t)xTaskGetTickCount();
            last_rx = now;
            if (c == '\r') {
                s_text_probe = 1;
                s_text_probe_tick = now;
            } else if ((c < 0x20) && (c != '\n')) {
                s_text_probe = 0;
            }
            link_feed_byte((uint8_t)c);
        } else {
            baud_revert_poll();
            text_probe_poll();
            progress_poll();
            if ((uint32_t)(xTaskGetTickCount() - last_rx) > 1000U) {
                last_rx = (uint32_t)xTaskGetTickCount();
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    if ((s_log_mutex == NULL) ||
        (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(LINK_TX_TIMEOUT_MS)) == pdPASS)) {
        link_log_flush(hwlink_LogLevel_INFO);
        if (s_log_mutex != NULL) { (void)xSemaphoreGive(s_log_mutex); }
    }
    hwval_test_set_hooks(NULL);
    hwval_uart3_set_sink(NULL);
    s_active = 0;
    hwval_uart3_drain(100U);
    (void)hwval_uart3_set_baud(115200U); /* text CLI default (LNK-001) */
    hwval_link_set_boot_flag(0);
    g_set.mode = HWVAL_MODE_INTERACTIVE;
    s_text_requested = 0;
    s_boot_active = 0;
}
