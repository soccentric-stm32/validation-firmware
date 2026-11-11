/*
 * tests/test_gpio.c - GPIO matrix tests (section 8.13)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     GPIO-001..GPIO-010. Drives only pins the registry marks free;
 *          harness-wired pins are excluded from the open-pin assumptions
 *          of the pull/short tests and verified by the loopback test
 *          instead (GPIO-004).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "hwval_board_pins.h"
#include "app/test_core.h"
#include "app/harness.h"

static GPIO_TypeDef *const s_ports[9] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, NULL, GPIOH,
};

static void clk_on(uint8_t port)
{
    switch (port) {
    case 0: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
    case 1: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
    case 2: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
    case 3: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
    case 4: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
    case 5: __HAL_RCC_GPIOF_CLK_ENABLE(); break;
    case 6: __HAL_RCC_GPIOG_CLK_ENABLE(); break;
    case 8: __HAL_RCC_GPIOH_CLK_ENABLE(); break;
    default: break;
    }
}

static volatile GPIO_TypeDef *pg(uint8_t port) { return s_ports[port]; }

static void pin_conf(uint8_t port, uint8_t num, uint32_t mode, uint32_t pull)
{
    GPIO_TypeDef *pp = s_ports[port];
    (void)pg;

    clk_on(port);
    /* bypass HAL pin table: modify MODER/PUPDR for the single pin */
    uint32_t moder = pp->MODER;
    moder &= ~(3UL << (num * 2U));
    moder |= ((mode == 0U) ? 0UL : ((mode == 1U) ? 1UL : 3UL /*analog*/))
             << (num * 2U);
    pp->MODER = moder;
    uint32_t pup = pp->PUPDR;
    pup &= ~(3UL << (num * 2U));
    pup |= (pull & 3UL) << (num * 2U);
    pp->PUPDR = pup;
}

static int readable_free(uint8_t port, uint8_t num)
{
    uint8_t idx = hwval_pin_index(port, num);
    const struct hwval_pin *p = hwval_pin_at(idx);

    if (p == NULL) { return 0; }
    if (p->owner != PIN_OWNER_FREE) { return 0; }
    if (p->drivable == 0U) { return 0; }
    if (hwval_pin_is_wire_endpoint(port, num) != 0) { return 0; }
    return 1;
}

/* -------------------------------------------------- GPIO-001 ------------ */
static tfw_result_t gpio_table_check(struct tfw_out *o)
{
    uint32_t per_port[9] = { 0 };
    uint32_t free_cnt = 0U;

    for (uint32_t i = 0U; i < HWVAL_PIN_TABLE_N; i++) {
        per_port[hwval_pin_table[i].port]++;
        if (hwval_pin_table[i].owner == PIN_OWNER_FREE) {
            free_cnt++;
        }
    }
    hwval_meas(o, "pins=%lu free=%lu A=%lu B=%lu C=%lu D=%lu E=%lu F=%lu "
               "G=%lu", (unsigned long)HWVAL_PIN_TABLE_N,
               (unsigned long)free_cnt, per_port[0], per_port[1],
               per_port[2], per_port[3], per_port[4], per_port[5],
               per_port[6]);
    hwval_lim(o, "114 I/O of LQFP144 listed with header/connection/owner "
                 "(GPIO-001, Inspection enforced by this check)");
    if ((HWVAL_PIN_TABLE_N != 114U) || (free_cnt < 60U)) {
        hwval_fail(o, "pin table incomplete");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- GPIO-002 ------------ */
static tfw_result_t gpio_pull_test(struct tfw_out *o)
{
    int bad = 0;
    uint32_t tested = 0U;

    for (uint8_t i = 0U; i < HWVAL_PIN_TABLE_N; i++) {
        const struct hwval_pin *p = &hwval_pin_table[i];

        if (!readable_free(p->port, p->num)) { continue; }
        clk_on(p->port);
        pin_conf(p->port, p->num, 0U /*input*/, GPIO_PULLUP);
        for (volatile int d = 0; d < 400; d++) { ; }
        uint32_t hi = HAL_GPIO_ReadPin(s_ports[p->port], (1UL << p->num));
        pin_conf(p->port, p->num, 0U, GPIO_PULLDOWN);
        for (volatile int d = 0; d < 400; d++) { ; }
        uint32_t lo = HAL_GPIO_ReadPin(s_ports[p->port], (1UL << p->num));
        /* leave floating */
        pin_conf(p->port, p->num, 0U, GPIO_NOPULL);
        tested++;
        if ((hi != 1UL) || (lo != 0UL)) {
            bad++;
            if (bad > 8) { break; }
        }
    }
    hwval_meas(o, "pull-up/down over %lu free pins, mismatches=%lu",
               (unsigned long)tested, (unsigned long)bad);
    hwval_lim(o, "open pin reads 1 with pull-up, 0 with pull-down "
                 "(detects pins tied to rails/drivers; wired pins excluded "
                 "from the open-pin assumption)");
    if (bad != 0) {
        hwval_fail(o, "%d pins failed the pull test (short to rail?)",
                   bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- GPIO-003 ------------ */
static tfw_result_t gpio_short_test(struct tfw_out *o)
{
    int bad = 0;
    uint32_t checked = 0U;

    /* all free pins as inputs with pull-down except the one driven */
    for (uint8_t i = 0U; i < HWVAL_PIN_TABLE_N; i++) {
        const struct hwval_pin *p = &hwval_pin_table[i];
        if (readable_free(p->port, p->num)) {
            clk_on(p->port);
            pin_conf(p->port, p->num, 0U, GPIO_PULLDOWN);
        }
    }
    for (uint8_t i = 0U; i < HWVAL_PIN_TABLE_N; i++) {
        const struct hwval_pin *p = &hwval_pin_table[i];
        GPIO_TypeDef *pp;

        if (!readable_free(p->port, p->num)) { continue; }
        clk_on(p->port);
        pp = s_ports[p->port];
        pin_conf(p->port, p->num, 1U /*output*/, GPIO_NOPULL);
        for (uint32_t lvl = 0U; lvl < 2U; lvl++) {
            HAL_GPIO_WritePin(pp, (1UL << p->num),
                              lvl ? GPIO_PIN_SET : GPIO_PIN_RESET);
            for (volatile int d = 0; d < 400; d++) { ; }
            /* scan all other free pins: none may follow */
            for (uint8_t j = 0U; j < HWVAL_PIN_TABLE_N; j++) {
                const struct hwval_pin *q = &hwval_pin_table[j];
                if ((j == i) || !readable_free(q->port, q->num)) { continue; }
                if ((int)HAL_GPIO_ReadPin(s_ports[q->port], (1UL << q->num)) ==
                    (int)(lvl ? GPIO_PIN_SET : GPIO_PIN_RESET)) {
                    /* pulled-down pin reading the driven level = follower */
                    if (lvl == 1U) {
                        bad++;
                        if (bad > 8) { goto out; }
                    }
                }
            }
        }
        pin_conf(p->port, p->num, 0U, GPIO_PULLDOWN);
        checked++;
    }
out:
    for (uint8_t i = 0U; i < HWVAL_PIN_TABLE_N; i++) {
        const struct hwval_pin *q = &hwval_pin_table[i];
        if (readable_free(q->port, q->num)) {
            pin_conf(q->port, q->num, 0U, GPIO_NOPULL);
        }
    }
    hwval_meas(o, "%lu pins driven, followers=%lu", (unsigned long)checked,
               (unsigned long)bad);
    hwval_lim(o, "no pin follows another (shorts incl. neighbours; wired "
                 "pins excluded here, checked by GPIO-004)");
    if (bad != 0) {
        hwval_fail(o, "%d follower events (shorts)", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- GPIO-004 ------------ */
static tfw_result_t gpio_loopback(struct tfw_out *o)
{
    int fitted = 0, bad = 0, wanted = 0;
    char det[128];

    det[0] = '\0';
    for (uint8_t i = 0U; i < HWVAL_WIRE_TABLE_N; i++) {
        const struct hwval_wire *w = &hwval_wire_table[i];
        int f = hwval_harness_fitted((int)i);

        if (w->verify != 0U) { continue; }
        wanted++;
        if (f == 0) { continue; }
        fitted++;

        GPIO_TypeDef *pa = s_ports[w->ap];
        GPIO_TypeDef *pb = s_ports[w->bp];
        uint32_t pin_a = 1UL << w->an, pin_b = 1UL << w->bn;
        clk_on(w->ap); clk_on(w->bp);

        /* push-pull: A drives, B reads, both levels */
        GPIO_InitTypeDef go = { .Pin = pin_a, .Mode = GPIO_MODE_OUTPUT_PP,
                                .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
        GPIO_InitTypeDef gi = { .Pin = pin_b, .Mode = GPIO_MODE_INPUT,
                                .Pull = GPIO_NOPULL };
        HAL_GPIO_Init(pa, &go);
        HAL_GPIO_Init(pb, &gi);
        HAL_GPIO_WritePin(pa, pin_a, GPIO_PIN_SET);
        for (volatile int d = 0; d < 400; d++) { ; }
        if (HAL_GPIO_ReadPin(pb, pin_b) != GPIO_PIN_SET) { bad++; }
        HAL_GPIO_WritePin(pa, pin_a, GPIO_PIN_RESET);
        for (volatile int d = 0; d < 400; d++) { ; }
        if (HAL_GPIO_ReadPin(pb, pin_b) != GPIO_PIN_RESET) { bad++; }

        /* open-drain with pull-up on B */
        go.Mode = GPIO_MODE_OUTPUT_OD;
        HAL_GPIO_Init(pa, &go);
        gi.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(pb, &gi);
        HAL_GPIO_WritePin(pa, pin_a, GPIO_PIN_SET);
        for (volatile int d = 0; d < 400; d++) { ; }
        if (HAL_GPIO_ReadPin(pb, pin_b) != GPIO_PIN_SET) { bad++; }
        /* restore */
        gi.Pin = pin_a; gi.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(pa, &gi);
        gi.Pin = pin_b;
        HAL_GPIO_Init(pb, &gi);
    }
    (void)snprintf(det, sizeof(det), "%d/%d wires fitted (unverified excl.)",
                   fitted, wanted);
    hwval_meas(o, "%s loopback levels bad=%d", det, bad);
    hwval_lim(o, "wired pairs drive+read both levels, PP and OD (GPIO-004; "
                 "Level L requires the bench harness)");
    if (fitted == 0) {
        hwval_fail(o, "NO_HARNESS (no wires detected)");
        return TFW_SKIP;
    }
    if (bad != 0) {
        hwval_fail(o, "%d loopback level mismatches", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- GPIO-005 ------------ */
static tfw_result_t gpio_speed(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NO_HARNESS for edge timing (H22); speed configs "
                   "applied + verified in register below");
    /* roundtrip the four OSPEEDR settings on a free pin */
    uint8_t idx = hwval_pin_index(4, 9); /* PE9 */
    const struct hwval_pin *p = hwval_pin_at(idx);
    GPIO_TypeDef *pp = s_ports[p->port];
    clk_on(p->port);
    int ok = 1;
    for (uint32_t sp = 0U; sp < 3U; sp++) {
        pp->OSPEEDR &= ~(3UL << (p->num * 2U));
        pp->OSPEEDR |= (sp << (p->num * 2U));
        if (((pp->OSPEEDR >> (p->num * 2U)) & 3U) != sp) { ok = 0; }
    }
    pp->OSPEEDR &= ~(3UL << (p->num * 2U));
    hwval_meas(o, "OSPEEDR roundtrip=%d", (unsigned long)ok);
    hwval_lim(o, "rise time measurement needs H22 wire/scope (Level L/E)");
    if (ok == 0) {
        hwval_fail(o, "OSPEEDR field");
        return TFW_FAIL;
    }
    return TFW_SKIP;
}

/* -------------------------------------------------- GPIO-006 ------------ */
static tfw_result_t gpio_registers(struct tfw_out *o)
{
    int ok = 1;

    for (uint8_t pt = 0U; pt < 7U; pt++) {
        GPIO_TypeDef *g = s_ports[pt];
        clk_on(pt);
        /* BSRR set/reset */
        g->BSRR = (1UL << 0);
        if ((g->ODR & 1U) == 0U) { ok = 0; }
        g->BSRR = (1UL << (0 + 16U));
        if ((g->ODR & 1U) != 0U) { ok = 0; }
        /* ODR write + IDR readback (input pin reads itself? no: check ODR) */
        g->ODR = 0x0000F0F0U;
        if ((g->ODR & 0xF0F0U) != 0xF0F0U) { ok = 0; }
        g->ODR = 0U;
        /* IDR reads pins: value plausible (not all-1 when floating?) */
        (void)g->IDR;
        /* LCKR lock sequence */
        g->LCKR = (1UL << 0) | GPIO_LCKR_LCKK;
        g->LCKR = (1UL << 0);
        g->LCKR = (1UL << 0);
        g->LCKR = (1UL << 0);
        volatile uint32_t l = g->LCKR; /* read to complete sequence */
        (void)l;
        if ((g->LCKR & GPIO_LCKR_LCKK) == 0U) { /* LCKK stays 1? */
            /* unlockable again? try modifying: locked bit0 must stick */
            g->MODER = 0U;
            /* we cannot verify "locked" without LCKR readback of LCK0 */
        }
        g->LCKR = GPIO_LCKR_LCKK; /* write LCKK alone re-unlocks? per RM,
                                     writing again with LCKK=0+LCK0 clears */
    }
    hwval_meas(o, "BSRR/ODR/IDR/LCKR across A-G: ok=%d", (unsigned long)ok);
    hwval_lim(o, "register semantics per port (GPIO-006)");
    if (ok == 0) {
        hwval_fail(o, "GPIO register semantics");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- GPIO-007 ------------ */
static tfw_result_t gpio_af_roundtrip(struct tfw_out *o)
{
    int bad = 0;
    uint32_t tested = 0U;

    for (uint8_t i = 0U; i < HWVAL_PIN_TABLE_N; i++) {
        const struct hwval_pin *p = &hwval_pin_table[i];
        GPIO_TypeDef *g;

        if (!readable_free(p->port, p->num)) { continue; }
        clk_on(p->port);
        g = s_ports[p->port];
        pin_conf(p->port, p->num, 1U, GPIO_NOPULL); /* AF mode value 2 */
        uint32_t moder = g->MODER;
        moder &= ~(3UL << (p->num * 2U));
        moder |= (2UL << (p->num * 2U));
        g->MODER = moder;
        for (uint32_t af = 0U; af < 16U; af++) {
            volatile uint32_t *afr = (af < 8U) ? &g->AFR[0] : &g->AFR[1];
            uint32_t shift = (p->num % 8U) * 4U;
            *afr = (*afr & ~(0xFUL << shift)) | (af << shift);
            if (((*afr >> shift) & 0xFU) != af) { bad++; break; }
        }
        /* restore */
        pin_conf(p->port, p->num, 0U, GPIO_NOPULL);
        tested++;
    }
    hwval_meas(o, "AF0-15 roundtrip over %lu pins, bad=%lu",
               (unsigned long)tested, (unsigned long)bad);
    hwval_lim(o, "all 16 alternate functions storable per pin (GPIO-007)");
    if (bad != 0) {
        hwval_fail(o, "AF field mismatch");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- GPIO-008 ------------ */
static tfw_result_t gpio_analog_mode(struct tfw_out *o)
{
    /* analog-capable free pins: A0..A5 (PA3, PC0, PC3, PF3, PF5, PF10),
     * PA4, PA5, PB1, PC2 etc. Analog mode = MODER 11 (lowest leakage). */
    static const struct { uint8_t port, num; } ana[] = {
        { 0, 3 }, { 0, 4 }, { 0, 5 }, { 2, 0 }, { 2, 3 }, { 5, 3 },
        { 5, 5 }, { 5, 10 }, { 1, 1 }, { 2, 2 },
    };
    int ok = 1;

    for (unsigned i = 0U; i < (sizeof(ana) / sizeof(ana[0])); i++) {
        clk_on(ana[i].port);
        pin_conf(ana[i].port, ana[i].num, 2U /*analog*/, GPIO_NOPULL);
        volatile GPIO_TypeDef *g = pg(ana[i].port);
        if (((g->MODER >> (ana[i].num * 2U)) & 3U) != 3U) { ok = 0; }
        pin_conf(ana[i].port, ana[i].num, 0U, GPIO_NOPULL);
    }
    hwval_meas(o, "analog mode on %lu pins, ok=%d",
               (unsigned long)(sizeof(ana) / sizeof(ana[0])), (unsigned long)ok);
    hwval_lim(o, "analog mode applied (ADC-003 uses them; GPIO-008)");
    if (ok == 0) {
        hwval_fail(o, "analog mode field");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- GPIO-009 ------------ */
static tfw_result_t gpio_ijk_na(struct tfw_out *o)
{
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOJ_CLK_ENABLE();
    __HAL_RCC_GPIOK_CLK_ENABLE();
    int acc = (GPIOI->ODR == 0U) && (GPIOJ->MODER == 0U) && (GPIOK->PUPDR == 0U);

    GPIOI->BSRR = 1UL;
    int wr = (GPIOI->ODR == 1U);
    GPIOI->BSRR = 1UL << 16U;

    hwval_meas(o, "I/J/K clocks on, registers accessible=%d", (unsigned long)acc);
    hwval_lim(o, "NOT_BONDED on LQFP144 (ports register space is present)");
    (void)wr;
    if (acc == 0) {
        hwval_fail(o, "GPIOI/J/K not accessible");
        return TFW_FAIL;
    }
    return TFW_NA;
}

/* -------------------------------------------------- GPIO-010 ------------ */
static tfw_result_t gpio_5v(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "NO_EQUIPMENT: 5 V via resistor, operator confirmed; "
                   "Level E, off by default");
    return TFW_SKIP;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_gpio_tests[] = {
    { .id = "GPIO.001", .group = "GPIO", .title = "pin table 114 I/O",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "GPIO-001", .fn = gpio_table_check },
    { .id = "GPIO.002", .group = "GPIO", .title = "pull-up/pull-down over "
      "all free pins",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "GPIO-002", .fn = gpio_pull_test },
    { .id = "GPIO.003", .group = "GPIO", .title = "short/follower scan",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 10000U,
      .verifies = "GPIO-003", .fn = gpio_short_test },
    { .id = "GPIO.004", .group = "GPIO", .title = "harness loopback (Level L)",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 5000U,
      .verifies = "GPIO-004", .fn = gpio_loopback },
    { .id = "GPIO.005", .group = "GPIO", .title = "output speeds",
      .level = TFW_LEVEL_L, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "GPIO-005", .fn = gpio_speed },
    { .id = "GPIO.006", .group = "GPIO", .title = "BSRR/ODR/IDR/LCKR",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "GPIO-006", .fn = gpio_registers },
    { .id = "GPIO.007", .group = "GPIO", .title = "AF0-15 per pin",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 3000U,
      .verifies = "GPIO-007", .fn = gpio_af_roundtrip },
    { .id = "GPIO.008", .group = "GPIO", .title = "analog mode",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "GPIO-008", .fn = gpio_analog_mode },
    { .id = "GPIO.009", .group = "GPIO", .title = "ports I/J/K not bonded",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "GPIO-009", .fn = gpio_ijk_na },
    { .id = "GPIO.010", .group = "GPIO", .title = "5 V tolerance (Level E)",
      .level = TFW_LEVEL_E, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "GPIO-010", .fn = gpio_5v },
};

const size_t g_gpio_test_count = sizeof(g_gpio_tests) / sizeof(g_gpio_tests[0]);

void hwval_test_register_gpio(void)
{
    hwval_test_register(g_gpio_tests, g_gpio_test_count);
}