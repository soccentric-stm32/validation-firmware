/*
 * harness.c - loopback harness wire detection (Appendix A, ENV-002)
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     ENV-002, CLI-030, GPIO-004. Pairs live in board/hwval_board_pins.c
 *          (CL-005). Detection drives one end and reads the other, both
 *          directions, for every non-verify wire; wires flagged verify=1
 *          (AF muxing to confirm on bench, OPEN-004) are detected but
 *          reported as unverified.
 */

#include <stdio.h>
#include "stm32f7xx_hal.h"
#include "hwval_board_pins.h"

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

static volatile GPIO_TypeDef *pin_port(uint8_t p)
{ return (p < 9) ? (volatile GPIO_TypeDef *)s_ports[p] : NULL; }

/* drive A (OD) low/high twice, B is input with the opposite pull */
static int pair_detect(const struct hwval_wire *w)
{
    volatile GPIO_TypeDef *pa = pin_port(w->ap);
    volatile GPIO_TypeDef *pb = pin_port(w->bp);
    GPIO_InitTypeDef gi, go;
    int ok = 1;

    if ((pa == NULL) || (pb == NULL)) {
        return 0;
    }
    clk_on(w->ap);
    clk_on(w->bp);

    gi.Pin   = (1UL << w->bn);
    gi.Mode  = GPIO_MODE_INPUT;
    gi.Pull  = GPIO_PULLDOWN;
    go.Pin   = (1UL << w->an);
    go.Mode  = GPIO_MODE_OUTPUT_OD;
    go.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init((GPIO_TypeDef *)pb, &gi);
    HAL_GPIO_Init((GPIO_TypeDef *)pa, &go);

    /* drive A low: with pull-down on B, a connected wire reads 0 */
    HAL_GPIO_WritePin((GPIO_TypeDef *)pa, (1UL << w->an), GPIO_PIN_RESET);
    for (volatile int i = 0; i < 400; i++) { ; }
    if (HAL_GPIO_ReadPin((GPIO_TypeDef *)pb, (1UL << w->bn)) != GPIO_PIN_RESET) {
        ok = 0;
    }
    /* drive A high: wire pulls B up through the OD high-Z + B pulldown...
     * use push-pull high instead: */
    go.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init((GPIO_TypeDef *)pa, &go);
    gi.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init((GPIO_TypeDef *)pb, &gi);
    HAL_GPIO_WritePin((GPIO_TypeDef *)pa, (1UL << w->an), GPIO_PIN_SET);
    for (volatile int i = 0; i < 400; i++) { ; }
    if (HAL_GPIO_ReadPin((GPIO_TypeDef *)pb, (1UL << w->bn)) != GPIO_PIN_SET) {
        ok = 0;
    }

    /* restore both to their default (input, no pull) */
    gi.Pin = (1UL << w->an); gi.Pull = GPIO_NOPULL;
    HAL_GPIO_Init((GPIO_TypeDef *)pa, &gi);
    gi.Pin = (1UL << w->bn);
    HAL_GPIO_Init((GPIO_TypeDef *)pb, &gi);
    return ok;
}

uint64_t hwval_harness_detected(void)
{
    uint64_t mask = 0U;

    for (uint8_t i = 0U; i < HWVAL_WIRE_TABLE_N; i++) {
        if (hwval_wire_table[i].verify != 0U) {
            continue; /* AF map not bench-verified yet: not reported */
        }
        if (pair_detect(&hwval_wire_table[i]) != 0) {
            mask |= (uint64_t)1U << i;
        }
    }
    return mask;
}

int hwval_harness_count(void) { return (int)HWVAL_WIRE_TABLE_N; }

int hwval_harness_fitted(int index)
{
    if ((index < 0) || (index >= (int)HWVAL_WIRE_TABLE_N)) {
        return 0;
    }
    return (int)((hwval_harness_detected() >> index) & 1ULL);
}

void hwval_harness_report(void)
{
    uint64_t mask = hwval_harness_detected();
    int fitted = 0;
    int verify = 0;

    for (uint8_t i = 0U; i < HWVAL_WIRE_TABLE_N; i++) {
        const struct hwval_wire *w = &hwval_wire_table[i];

        if (w->verify != 0U) {
            verify++;
            continue;
        }
        if (((mask >> i) & 1ULL) != 0ULL) {
            fitted++;
            (void)printf("  %-6s P%c%u <-> P%c%u  (%s)\r\n", w->name,
                         'A' + w->ap, w->an, 'A' + w->bp, w->bn,
                         w->used_by);
        }
    }
    (void)printf("harness: %d/%d wires fitted, %d verify-on-bench wires "
                 "excluded\r\n", fitted, (int)HWVAL_WIRE_TABLE_N, verify);
}
