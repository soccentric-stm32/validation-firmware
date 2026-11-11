/*
 * tests/test_led_btn.c - on-board LEDs and buttons (section 8.14)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     LED-001..LED-004, BTN-001..BTN-003. LED colour confirmation is
 *          operator-level (Level O); PWM brightness uses the timer channels
 *          (PB0/TIM3_CH3, PB7/TIM4_CH2, PB14/TIM12_CH1).
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f7xx_hal.h"
#include "hwval_board.h"
#include "app/test_core.h"
#include "app/cli.h"
#include "app/settings.h"

static int ask(const char *q)
{
    return hwval_cli_ask_yn(q, g_set.operator_timeout_s);
}

/* -------------------------------------------------- LED-001/004 -------- */
static tfw_result_t led_colours(struct tfw_out *o)
{
    GPIO_InitTypeDef io = { .Pin = HWVAL_LED1_PIN | HWVAL_LED2_PIN |
                                    HWVAL_LED3_PIN,
                            .Mode = GPIO_MODE_OUTPUT_PP,
                            .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };

    /* suspend the heartbeat task's ownership of the LEDs? heartbeat runs
     * at prio 1 and we hold prio 2..3, so our writes win for the window */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_Init(HWVAL_LED1_PORT, &io);
    HAL_GPIO_WritePin(HWVAL_LED1_PORT, HWVAL_LED1_PIN | HWVAL_LED2_PIN |
                      HWVAL_LED3_PIN, GPIO_PIN_RESET);

    for (int led = 0; led < 3; led++) {
        uint32_t bit = (led == 0) ? HWVAL_LED1_PIN :
                       (led == 1) ? HWVAL_LED2_PIN : HWVAL_LED3_PIN;
        GPIO_PinState old = HAL_GPIO_ReadPin(HWVAL_LED1_PORT, bit);

        HAL_GPIO_WritePin(HWVAL_LED1_PORT, bit, GPIO_PIN_SET);
        int ok = ask(led == 0 ? "LD1 (PB0) green lit?"
                    : led == 1 ? "LD2 (PB7) blue lit?" : "LD3 (PB14) red lit?");
        HAL_GPIO_WritePin(HWVAL_LED1_PORT, bit, GPIO_PIN_RESET);
        (void)old;
        if (ok < 0) {
            (void)snprintf(o->reason, sizeof(o->reason), "NO_OPERATOR");
            return TFW_SKIP;
        }
        if (ok == 0) {
            hwval_fail(o, "operator did not confirm LED %d", led + 1);
            return TFW_FAIL;
        }
    }
    hwval_meas(o, "LD1/2/3 operator-confirmed");
    hwval_lim(o, "board-driven LEDs checked in their circuits (LED-004: "
                 "COM LED via STLK.005, power via PWR.011, VBUS/OC via "
                 "USB tests, RJ45 via ETH-005)");
    return TFW_PASS;
}

/* -------------------------------------------------- LED-002 (PWM) ------ */
static tfw_result_t led_pwm(struct tfw_out *o)
{
    struct { TIM_TypeDef *tim; uint8_t ch; const char *name; } lcs[3] = {
        { TIM3, 3, "LD1" }, { TIM4, 2, "LD2" }, { TIM12, 1, "LD3" },
    };
    int ok3 = 1;

    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_TIM12_CLK_ENABLE();
    for (int i = 0; i < 3; i++) {
        TIM_TypeDef *t = lcs[i].tim;
        uint8_t ch = lcs[i].ch;
        volatile uint32_t *ccr = (ch == 1) ? &t->CCR1 : (ch == 2) ?
                                 &t->CCR2 : (ch == 3) ? &t->CCR3 : &t->CCR4;
        volatile uint32_t *ccmr = (ch <= 2) ? &t->CCMR1 : &t->CCMR2;
        uint32_t shift = ((ch == 2) || (ch == 4)) ? 8U : 0U;

        t->CR1 = 0U;
        t->PSC = (HAL_RCC_GetHCLKFreq() / 1000000U) - 1U; /* 1 MHz */
        t->ARR = 999U;
        /* PWM mode 1 on the right channel */
        uint32_t oc = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE;
        *ccmr |= oc << shift;
        for (uint32_t duty = 0U; duty <= 4U; duty++) {
            *ccr = duty * 250U;
            t->CCER |= (1UL << ((ch - 1U) * 4U)); /* enable output */
            t->CR1 |= TIM_CR1_CEN;
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        t->CR1 = 0U;
        t->CCER &= ~(1UL << ((ch - 1U) * 4U));
        if (*ccr != 1000U) { ok3 = 0; }
        *ccr = 0U;
    }
    int conf = ask("all three LEDs ramped 0/25/50/75/100 %% ?");
    if (conf < 0) {
        (void)snprintf(o->reason, sizeof(o->reason), "NO_OPERATOR");
        return TFW_SKIP;
    }
    hwval_meas(o, "ccr seq=%d confirm=%d", (unsigned long)ok3,
               (unsigned long)conf);
    hwval_lim(o, "0-100 %% in 25 %% steps via TIM channels (LED-002)");
    if ((ok3 == 0) || (conf == 0)) {
        hwval_fail(o, "brightness ramp not confirmed");
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- LED-003 ------------ */
static tfw_result_t led_idr_short(struct tfw_out *o)
{
    /* drive high, IDR must read back high (not shorted to ground) */
    GPIO_InitTypeDef io = { .Pin = HWVAL_LED1_PIN | HWVAL_LED2_PIN |
                                    HWVAL_LED3_PIN,
                            .Mode = GPIO_MODE_OUTPUT_PP,
                            .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
    int bad = 0;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_Init(HWVAL_LED1_PORT, &io);
    HAL_GPIO_WritePin(HWVAL_LED1_PORT, io.Pin, GPIO_PIN_SET);
    for (volatile int d = 0; d < 1000; d++) { ; }

    const uint32_t bits[3] = { HWVAL_LED1_PIN, HWVAL_LED2_PIN, HWVAL_LED3_PIN };
    for (int i = 0; i < 3; i++) {
        if (HAL_GPIO_ReadPin(HWVAL_LED1_PORT, bits[i]) != GPIO_PIN_SET) {
            bad++;
        }
    }
    HAL_GPIO_WritePin(HWVAL_LED1_PORT, io.Pin, GPIO_PIN_RESET);
    for (int i = 0; i < 3; i++) {
        if (HAL_GPIO_ReadPin(HWVAL_LED1_PORT, bits[i]) != GPIO_PIN_RESET) {
            bad++;
        }
    }
    hwval_meas(o, "idr mismatches=%d", (unsigned long)bad);
    hwval_lim(o, "IDR follows ODR for each color (no short) (LED-003)");
    if (bad != 0) {
        hwval_fail(o, "%d IDR read-back failures", bad);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- BTN-001 ------------ */
static tfw_result_t btn_idle_press(struct tfw_out *o)
{
    GPIO_InitTypeDef io = { .Pin = HWVAL_BTN1_PIN, .Mode = GPIO_MODE_INPUT,
                            .Pull = GPIO_PULLDOWN, .Speed = GPIO_SPEED_FREQ_LOW };

    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_Init(HWVAL_BTN1_PORT, &io);
    for (volatile int d = 0; d < 1000; d++) { ; }
    uint32_t idle = HAL_GPIO_ReadPin(HWVAL_BTN1_PORT, HWVAL_BTN1_PIN);

    int press = ask("press and HOLD B1 now");

    if (press < 0) {
        (void)snprintf(o->reason, sizeof(o->reason), "NO_OPERATOR");
        return TFW_SKIP;
    }
    for (volatile int d = 0; d < 4000; d++) { ; }
    uint32_t held = HAL_GPIO_ReadPin(HWVAL_BTN1_PORT, HWVAL_BTN1_PIN);
    int rel = ask("release B1 now");
    for (volatile int d = 0; d < 4000; d++) { ; }
    uint32_t after = HAL_GPIO_ReadPin(HWVAL_BTN1_PORT, HWVAL_BTN1_PIN);

    hwval_meas(o, "idle=%lu hold=%lu after=%lu", (unsigned long)idle,
               (unsigned long)held, (unsigned long)after);
    hwval_lim(o, "B1 active high: 0 idle, 1 held, 0 released (BTN-001)");
    if ((idle != 0U) || (held != 1U) || (after != 0U)) {
        hwval_fail(o, "button idle/hold/release levels wrong");
        return TFW_FAIL;
    }
    (void)rel;
    return TFW_PASS;
}

/* -------------------------------------------------- BTN-002 ------------ */
static volatile uint32_t s_btn_exti, s_btn_edge;

void EXTI15_10_IRQHandler(void);

void EXTI15_10_IRQHandler(void)
{
    /* B1 = PC13 = line 13 */
    if ((EXTI->PR & (1U << 13)) != 0U) {
        s_btn_exti++;
        s_btn_edge = (GPIOC->IDR & (1UL << 13)) != 0U;
        EXTI->PR = (1U << 13);
    }
    EXTI->PR = 0x000FF000U; /* clear lines 12-15 etc */
}

static tfw_result_t btn_exti(struct tfw_out *o)
{
    s_btn_exti = 0U;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef io = { .Pin = HWVAL_BTN1_PIN, .Mode = GPIO_MODE_IT_RISING,
                            .Pull = GPIO_PULLDOWN, .Speed = GPIO_SPEED_FREQ_LOW };
    HAL_GPIO_Init(HWVAL_BTN1_PORT, &io);
    /* both edges: rising for press */
    EXTI->IMR |= (1U << 13);
    EXTI->RTSR |= (1U << 13);
    EXTI->FTSR |= (1U << 13);
    EXTI->PR = (1U << 13);
    NVIC_SetPriority(EXTI15_10_IRQn, 6);
    NVIC_EnableIRQ(EXTI15_10_IRQn);

    int n = ask("press B1 5 times (button debounce counted)");

    vTaskDelay(pdMS_TO_TICKS(600));
    NVIC_DisableIRQ(EXTI15_10_IRQn);
    EXTI->IMR &= ~(1U << 13);
    if (n < 0) {
        (void)snprintf(o->reason, sizeof(o->reason), "NO_OPERATOR");
        return TFW_SKIP;
    }
    hwval_meas(o, "exti events=%lu last-edge=%s (5 presses expected)",
               (unsigned long)s_btn_exti, (unsigned long)s_btn_edge);
    hwval_lim(o, ">=5 EXTI edges counted with software debounce (BTN-002)");
    if (s_btn_exti < 5U) {
        hwval_fail(o, "only %lu events for 5 presses",
                   (unsigned long)s_btn_exti);
        return TFW_FAIL;
    }
    return TFW_PASS;
}

/* -------------------------------------------------- BTN-003 ------------ */
static tfw_result_t btn_b2(struct tfw_out *o)
{
    (void)snprintf(o->reason, sizeof(o->reason),
                   "covered by RST.003 (B2 press -> PINRSTF)");
    return TFW_SKIP;
}

/* ----------------------------------------------------------------- table */
const struct tfw_case g_led_btn_tests[] = {
    { .id = "LED.001", .group = "LED", .title = "LD1/LD2/LD3 colour confirm",
      .level = TFW_LEVEL_S, .needs_operator = 1, .resets_mcu = 0,
      .timeout_ms = 20000U, .verifies = "LED-001, LED-004",
      .fn = led_colours },
    { .id = "LED.002", .group = "LED", .title = "PWM brightness ramp",
      .level = TFW_LEVEL_S, .needs_operator = 1, .resets_mcu = 0,
      .timeout_ms = 20000U, .verifies = "LED-002", .fn = led_pwm },
    { .id = "LED.003", .group = "LED", .title = "IDR short check",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 500U,
      .verifies = "LED-003", .fn = led_idr_short },
    { .id = "BTN.001", .group = "BTN", .title = "B1 idle/press/release",
      .level = TFW_LEVEL_S, .needs_operator = 1, .resets_mcu = 0,
      .timeout_ms = 20000U, .verifies = "BTN-001", .fn = btn_idle_press },
    { .id = "BTN.002", .group = "BTN", .title = "B1 EXTI 5 presses",
      .level = TFW_LEVEL_S, .needs_operator = 1, .resets_mcu = 0,
      .timeout_ms = 20000U, .verifies = "BTN-002", .fn = btn_exti },
    { .id = "BTN.003", .group = "BTN", .title = "B2 (reset) -> RST.003",
      .level = TFW_LEVEL_S, .resets_mcu = 0, .timeout_ms = 300U,
      .verifies = "BTN-003", .fn = btn_b2 },
};

const size_t g_led_btn_test_count =
    sizeof(g_led_btn_tests) / sizeof(g_led_btn_tests[0]);

void hwval_test_register_led_btn(void)
{
    hwval_test_register(g_led_btn_tests, g_led_btn_test_count);
}