/*
 * board/hwval_board_pins.c - 114-pin table, harness pairs, ownership
 *
 * Author:  Sandesh Ghimire <sandesh@soccentric.com>
 * Date:    2026-11-02
 * SRS:     GPIO-001, SAFE-006, Appendix A (CL-005); pin data from
 *          [Hardware.md 12/13] and [DS11532].
 *
 * Harness wire names follow SRS Appendix A. Pairs were chosen so that no
 * pin is a *driving* endpoint of two wires at once; input-side fan-outs
 * (e.g. MCO measured on an I2C wire endpoint) are marked in the notes and
 * short-test (GPIO-003) excludes every wired pin.
 */

#include <stddef.h>
#include "hwval_board_pins.h"

/* LQFP144 port map; conn uses Hardware.md section 12 (D/A names) or the
 * morpho position when the pin is Arduino-only by header position. */
const struct hwval_pin hwval_pin_table[HWVAL_PIN_TABLE_N] = {
    /* port A */
    { 0, 0,  "morpho",   "WKUP1/CAN? free",        PIN_OWNER_FREE,     1 },
    { 0, 1,  "morpho",   "ETH RMII (ref_clk?)",    PIN_OWNER_ETH,      0 },
    { 0, 2,  "morpho",   "ETH RMII MDIO / WKUP2",  PIN_OWNER_ETH,      0 },
    { 0, 3,  "A0",       "Arduino A0, ADC123_IN3", PIN_OWNER_FREE,     1 },
    { 0, 4,  "morpho",   "DAC_OUT1",               PIN_OWNER_FREE,     1 },
    { 0, 5,  "D13",      "DAC_OUT2 / SPI1_SCK",    PIN_OWNER_FREE,     1 },
    { 0, 6,  "D12",      "SPI1_MISO / TIM1_BKIN?", PIN_OWNER_FREE,     1 },
    { 0, 7,  "D11",      "SPI1_MOSI / ETH CRS_DV", PIN_OWNER_ETH,      0 },
    { 0, 8,  "morpho",   "MCO1 / I2C3_SCL",        PIN_OWNER_FREE,     1 },
    { 0, 9,  "morpho",   "USB VBUS / USART1_TX",   PIN_OWNER_USBFS,    1 },
    { 0, 10, "morpho",   "USB ID / USART1_RX",     PIN_OWNER_USBFS,    1 },
    { 0, 11, "morpho",   "USB D-/USART6 RTS",      PIN_OWNER_USBFS,    1 },
    { 0, 12, "morpho",   "USB D+ / USART6 CTS",    PIN_OWNER_USBFS,    1 },
    { 0, 13, "morpho",   "SWDIO",                  PIN_OWNER_SWDBG,    0 },
    { 0, 14, "morpho",   "SWCLK",                  PIN_OWNER_SWDBG,    0 },
    { 0, 15, "morpho",   "JTDI / SPI3_NSS",        PIN_OWNER_FREE,     1 },
    /* port B */
    { 1, 0,  "morpho",   "LD1 green (PB0)",        PIN_OWNER_LED,      1 },
    { 1, 1,  "morpho",   "free (ADC12_IN9)",       PIN_OWNER_FREE,     1 },
    { 1, 2,  "morpho",   "free / LSI clock out?",  PIN_OWNER_FREE,     1 },
    { 1, 3,  "morpho",   "SWO / SPI1_SCK",         PIN_OWNER_FREE,     1 },
    { 1, 4,  "morpho",   "SPI1_MISO / CAN2_RX",    PIN_OWNER_FREE,     1 },
    { 1, 5,  "morpho",   "SPI1_MOSI / CAN2_TX",    PIN_OWNER_FREE,     1 },
    { 1, 6,  "morpho",   "I2C1_SCL / TIM4_CH1 / CEC", PIN_OWNER_FREE,  1 },
    { 1, 7,  "morpho",   "LD2 blue",               PIN_OWNER_LED,      1 },
    { 1, 8,  "D15",      "I2C1_SCL / SDMMC1_D4? CEC", PIN_OWNER_FREE,  1 },
    { 1, 9,  "D14",      "I2C1_SDA",               PIN_OWNER_FREE,     1 },
    { 1, 10, "morpho",   "I2C2_SCL / SPI2_SCK",    PIN_OWNER_FREE,     1 },
    { 1, 11, "morpho",   "I2C2_SDA",               PIN_OWNER_FREE,     1 },
    { 1, 12, "morpho",   "TIM1_BKIN? / WKUP driver", PIN_OWNER_FREE,   1 },
    { 1, 13, "morpho",   "ETH RMII TXD1",          PIN_OWNER_ETH,      0 },
    { 1, 14, "morpho",   "LD3 red (PB14)",         PIN_OWNER_LED,      1 },
    { 1, 15, "morpho",   "SPI2_MOSI / OTG_HS_DP?", PIN_OWNER_FREE,     1 },
    /* port C */
    { 2, 0,  "A1",       "ADC123_IN10 / ETH?",     PIN_OWNER_FREE,     1 },
    { 2, 1,  "morpho",   "ETH RMII MDC / ADC12_IN11", PIN_OWNER_ETH,   0 },
    { 2, 2,  "morpho",   "SPI2_MISO / free",       PIN_OWNER_FREE,     1 },
    { 2, 3,  "A2",       "ADC123_IN13",            PIN_OWNER_FREE,     1 },
    { 2, 4,  "morpho",   "ETH RMII CRSDV? ADC12_IN14", PIN_OWNER_ETH,  0 },
    { 2, 5,  "morpho",   "ETH RMII RXD1? ADC12_IN15", PIN_OWNER_ETH,   0 },
    { 2, 6,  "morpho",   "DCMI_D0 / TIM3_CH1",     PIN_OWNER_FREE,     1 },
    { 2, 7,  "morpho",   "DCMI_D1 / TIM3_CH2",     PIN_OWNER_FREE,     1 },
    { 2, 8,  "morpho",   "SDMMC1_D2 / DCMI_D2",    PIN_OWNER_FREE,     1 },
    { 2, 9,  "morpho",   "SDMMC1_D3 / MCO2 / I2C3_SDA", PIN_OWNER_FREE, 1 },
    { 2, 10, "morpho",   "UART4_TX / SDMMC1_D3?",  PIN_OWNER_FREE,     1 },
    { 2, 11, "morpho",   "UART4_RX",               PIN_OWNER_FREE,     1 },
    { 2, 12, "morpho",   "UART5_TX / SDMMC1_CK",   PIN_OWNER_FREE,     1 },
    { 2, 13, "morpho",   "PC13 B1 (pull-down)",    PIN_OWNER_BTN,      0 },
    { 2, 14, "cn7",      "LSE OSC out (PC14)",     PIN_OWNER_LSE,      0 },
    { 2, 15, "cn7",      "LSE OSC in (PC15)",      PIN_OWNER_LSE,      0 },
    /* port D */
    { 3, 0,  "morpho",   "CAN1_RX / free",         PIN_OWNER_FREE,     1 },
    { 3, 1,  "morpho",   "CAN1_TX",                PIN_OWNER_FREE,     1 },
    { 3, 2,  "morpho",   "UART5_RX",               PIN_OWNER_FREE,     1 },
    { 3, 3,  "morpho",   "USART2_CTS",             PIN_OWNER_FREE,     1 },
    { 3, 4,  "morpho",   "USART2_RTS",             PIN_OWNER_FREE,     1 },
    { 3, 5,  "morpho",   "USART2_TX",              PIN_OWNER_FREE,     1 },
    { 3, 6,  "morpho",   "USART2_RX",              PIN_OWNER_FREE,     1 },
    { 3, 7,  "morpho",   "SDMMC1_CMD / free",      PIN_OWNER_FREE,     1 },
    /* PD8/PD9 are USART3 TX/RX on the ST-LINK VCP (CON-005).  The GPIO
     * test must never drive them, even though the board also routes them
     * to the SDMMC1 connector. */
    { 3, 8,  "morpho",   "USART3_TX (VCP)",        PIN_OWNER_CONSOLE,  0 },
    { 3, 9,  "morpho",   "USART3_RX (VCP)",        PIN_OWNER_CONSOLE,  0 },
    { 3, 10, "CN12",     "USART3_TX (ST-LINK VCP)", PIN_OWNER_CONSOLE, 0 },
    { 3, 11, "CN12",     "USART3_RX (ST-LINK VCP)", PIN_OWNER_CONSOLE, 0 },
    { 3, 12, "morpho",   "SAI1_FS_A / TIM4_CH1",   PIN_OWNER_PERIPH,   1 },
    { 3, 13, "morpho",   "I2C4_SCL",               PIN_OWNER_FREE,     1 },
    { 3, 14, "morpho",   "I2C4_SDA",               PIN_OWNER_FREE,     1 },
    { 3, 15, "morpho",   "TIM4_CH2 / SAI2_SD_B",   PIN_OWNER_FREE,     1 },
    /* port E */
    { 4, 0,  "morpho",   "UART8_TX / DCMI_D4",     PIN_OWNER_FREE,     1 },
    { 4, 1,  "morpho",   "UART8_RX / DCMI_D5",     PIN_OWNER_FREE,     1 },
    { 4, 2,  "morpho",   "TRACECLK / SAI MCLK / DCMI_PIXCLK", PIN_OWNER_FREE, 1 },
    { 4, 3,  "FMC/A3?",  "TRACE D0 / SAI1_SD_B?",  PIN_OWNER_PERIPH,   1 },
    { 4, 4,  "morpho",   "TRACE D1 / SAI1_FS_A",   PIN_OWNER_PERIPH,   1 },
    { 4, 5,  "morpho",   "TRACE D2 / SAI1_SD_A / TIM9_CH1", PIN_OWNER_PERIPH, 1 },
    { 4, 6,  "morpho",   "TRACE D3 / SAI1_MCLK_B / TIM9_CH2", PIN_OWNER_PERIPH, 1 },
    { 4, 7,  "morpho",   "UART7_TX",               PIN_OWNER_FREE,     1 },
    { 4, 8,  "morpho",   "UART7_RX",               PIN_OWNER_FREE,     1 },
    { 4, 9,  "D6",       "TIM1_CH1",               PIN_OWNER_FREE,     1 },
    { 4, 10, "morpho",   "TIM1_CH2N",              PIN_OWNER_FREE,     1 },
    { 4, 11, "D5",       "TIM1_CH2",               PIN_OWNER_FREE,     1 },
    { 4, 12, "morpho",   "SPI4_SCK",               PIN_OWNER_FREE,     1 },
    { 4, 13, "D3",       "SPI4_MISO / TIM1_CH3",   PIN_OWNER_FREE,     1 },
    { 4, 14, "D4?",      "SPI4_MOSI",              PIN_OWNER_FREE,     1 },
    { 4, 15, "D?/A4",    "free",                   PIN_OWNER_FREE,     1 },
    /* port F */
    { 5, 0,  "morpho",   "I2C2_SDA",               PIN_OWNER_FREE,     1 },
    { 5, 1,  "morpho",   "I2C2_SCL",               PIN_OWNER_FREE,     1 },
    { 5, 2,  "morpho",   "free (SDMMC2_CK?)",      PIN_OWNER_FREE,     1 },
    { 5, 3,  "A3",       "ADC3_IN9",               PIN_OWNER_FREE,     1 },
    { 5, 4,  "morpho",   "SDMMC2_D3 or free",       PIN_OWNER_FREE,     1 },
    { 5, 5,  "A4",       "ADC3_IN15",              PIN_OWNER_FREE,     1 },
    { 5, 6,  "morpho",   "UART7_RX? / TIM16_CH1",  PIN_OWNER_FREE,     1 },
    { 5, 7,  "morpho",   "UART7_TX? / SPI5_SCK",   PIN_OWNER_FREE,     1 },
    { 5, 8,  "morpho",   "TIM13_CH1 (MCO capture)", PIN_OWNER_FREE,    1 },
    { 5, 9,  "morpho",   "SPI5_MISO / TIM14_CH1",  PIN_OWNER_FREE,     1 },
    { 5, 10, "A5",       "ADC3_IN8",               PIN_OWNER_FREE,     1 },
    { 5, 11, "morpho",   "SPI5_MOSI",              PIN_OWNER_FREE,     1 },
    { 5, 12, "morpho",   "SPI5_NSS / SDMMC2_D0",   PIN_OWNER_FREE,     1 },
    { 5, 13, "D7",       "TIM1_ETR? / DCMI_HSYNC?", PIN_OWNER_FREE,    1 },
    { 5, 14, "D10?",     "free",                   PIN_OWNER_FREE,     1 },
    { 5, 15, "D9?",      "free",                   PIN_OWNER_FREE,     1 },
    /* port G */
    { 6, 0,  "morpho",   "USB OTG HS ID or free",  PIN_OWNER_FREE,     1 },
    { 6, 1,  "morpho",   "USB OTG HS D0 or free",  PIN_OWNER_FREE,     1 },
    { 6, 2,  "morpho",   "LCD? free",              PIN_OWNER_FREE,     1 },
    { 6, 3,  "morpho",   "LCD? free",              PIN_OWNER_FREE,     1 },
    { 6, 4,  "morpho",   "free",                   PIN_OWNER_FREE,     1 },
    { 6, 5,  "morpho",   "LCD B0?",                PIN_OWNER_FREE,     1 },
    { 6, 6,  "morpho",   "USB VBUS_ON (PG6)",      PIN_OWNER_USBFS,    1 },
    { 6, 7,  "morpho",   "USB OC (PG7)",           PIN_OWNER_USBFS,    0 },
    { 6, 8,  "morpho",   "LCD? free",              PIN_OWNER_FREE,     1 },
    { 6, 9,  "D0",       "USART6_RX (debug)",      PIN_OWNER_CONSOLE,  0 },
    { 6, 10, "morpho",   "LED2 alt? / LCD?",       PIN_OWNER_FREE,     1 },
    { 6, 11, "morpho",   "ETH RMII TX_EN",         PIN_OWNER_ETH,      0 },
    { 6, 12, "morpho",   "USART6 RTS (alt)",       PIN_OWNER_FREE,     1 },
    { 6, 13, "morpho",   "ETH RMII (PG13)",        PIN_OWNER_ETH,      0 },
    { 6, 14, "D1",       "USART6_TX (debug)",      PIN_OWNER_CONSOLE,  0 },
    { 6, 15, "morpho",   "USART6_CTS (alt)",       PIN_OWNER_FREE,     1 },
    /* port H */
    { 8, 0,  "morpho",   "HSE in (MCO from ST-LINK)", PIN_OWNER_SWDBG, 0 },
    { 8, 1,  "morpho",   "HSE out (X3 footp.)",    PIN_OWNER_SWDBG,    0 },
};

/* -------- Appendix A harness (CL-005): concrete pairs -------------------- */
/* verify=1: AF map assumed from [DS11532], re-check on the bench (OPEN-004). */
const struct hwval_wire hwval_wire_table[HWVAL_WIRE_TABLE_N] = {
    { "H01", 0, 4, 5, 3,  "ADC-004, DAC-*", 0 },        /* PA4->PF3  */
    { "H02", 0, 5, 5, 5,  "ADC-004, DAC-*", 0 },        /* PA5->PF5  */
    { "H03", 0, 9, 0, 10, "UART-001 USART1", 0 },
    { "H04", 3, 5, 3, 6,  "UART-001 USART2", 0 },
    { "H05", 6, 14, 6, 9,  "UART-001 USART6", 0 },
    { "H06", 2, 10, 2, 11, "UART-001 UART4",  0 },
    { "H07", 2, 12, 3, 2,  "UART-001 UART5",  0 },
    { "H08", 4, 7, 4, 8,  "UART-001 UART7",  0 },
    { "H09", 4, 0, 4, 1,  "UART-001 UART8",  0 },
    { "H10", 3, 4, 3, 3,  "UART-005 USART2 RTS->CTS", 0 },
    { "H11", 6, 12, 6, 13, "UART-005 USART6 RTS->CTS", 0 },
    { "H12", 3, 5, 6, 9,  "UART-006/009 cross inj. (PD5->PG9)", 0 },
    { "H12b", 6, 14, 3, 6, "UART-006/009 cross inj. (PG14->PD6)", 0 },
    { "H13", 1, 5, 1, 4,  "SPI-001 SPI1 MOSI->MISO (PB5->PB4)", 0 },
    { "H14", 1, 15, 2, 2,  "SPI-001 SPI2 MOSI->MISO (PB15->PC2)", 0 },
    { "H15", 4, 14, 4, 13, "SPI-001 SPI4 MOSI->MISO (PE14->PE13)", 0 },
    { "H16", 5, 11, 5, 9,  "SPI-001 SPI5 MOSI->MISO (PF11->PF9)", 0 },
    { "H17", 5, 7, 5, 1,  "I2C2 SCK/SDA path PF7(CK) note", 1 },
    /* H17-18 unused for SPI3/SPI6 (no loopback pins): register-only tests */
    { "H19", 4, 12, 5, 7,  "SPI-002 SPI4_SCK->SPI5_SCK", 0 },
    { "H19b", 4, 14, 5, 11, "SPI-002 SPI4_MOSI->SPI5_MOSI(slave in)", 0 },
    /* H20 I2C: I2C1<->I2C3 & I2C2<->I2C4, SCL and SDA each */
    { "H20", 1, 8, 0, 8,  "I2C SCL PB8->PA8 (I2C1->I2C3)", 0 },
    { "H20b", 1, 9, 2, 9,  "I2C SDA PB9->PC9 (I2C1->I2C3)", 0 },
    { "H20c", 5, 1, 3, 13, "I2C SCL PF1->PD13 (I2C2->I2C4)", 0 },
    { "H20d", 5, 0, 3, 14, "I2C SDA PF0->PD14 (I2C2->I2C4)", 0 },
    { "H21", 3, 1, 3, 0,  "CAN TX->RX/monitor PD1->PD0", 0 },
    { "H22", 4, 9, 2, 6,  "TIM PE9(TIM1_CH1)->PC6(TIM8_CH1/3?)", 0 },
    { "H23", 4, 11, 1, 6,  "encoder PE11(TIM1_CH2)->PB6(TIM4_CH1)", 0 },
    { "H23b", 4, 13, 1, 7, "encoder PE13(TIM1_CH3)->PB7(TIM4_CH2)", 0 },
    { "H24", 6, 10, 1, 12, "break/ETR PG10->PB12(TIM1_BKIN2? verify)", 1 },
    { "H25", 5, 12, 4, 9,  "hall PF12->PE9 (input-side fan-out)", 0 },
    { "H25b", 5, 13, 4, 11, "hall PF13->PE11 (input fan-out)", 0 },
    { "H25c", 5, 15, 4, 13, "hall PF15->PE13 (input fan-out)", 0 },
    { "H26", 0, 8, 5, 8,  "MCO1 PA8->PF8 (TIM13_CH1)", 0 },
    { "H26b", 2, 9, 1, 8,  "MCO2 PC9->PB8 (TIM10_CH1, fan-out)", 0 },
    { "H27", 4, 5, 4, 3,  "SAI SD_A(PE5)->SD_B(PE3, verify)", 1 },
    { "H27b", 4, 5, 4, 9,  "SAI SD_A(PE5)->SPDIFRX_IN1(PE9 verify)", 1 },
    { "H28", 5, 7, 5, 10, "DFSDM CK PF7->PF10? verify AF (OPEN-004)", 1 },
    { "H31", 2, 6, 2, 6,  "DCMI D0-D7 self-wired (see 8.30)", 1 },
    { "H33", 1, 12, 0, 0,  "WKUP1 PB10->PA0", 0 },
    { "H33b", 1, 11, 0, 2, "WKUP2 PB11->PA2 (PA2 also ETH MDIO)", 1 },
    { "H33c", 1, 12, 2, 1, "WKUP3 PB12->PC1 (fan-out w/H24)", 1 },
    { "H34", 0, 3, 2, 3,  "EXTI lines without dedicated wire (PA3->PC3)", 0 },
    { "H35", 4, 10, 0, 0,  "LPTIM OUT->IN1/ETR (AF verify)", 1 },
};

/* ------------------------------------------------- ownership registry --- */
static uint8_t s_owner[HWVAL_PIN_TABLE_N];

uint8_t hwval_pin_index(uint8_t port, uint8_t num)
{
    for (uint8_t i = 0U; i < HWVAL_PIN_TABLE_N; i++) {
        if ((hwval_pin_table[i].port == port) &&
            (hwval_pin_table[i].num == num)) {
            return i;
        }
    }
    return 0xFFU;
}

const struct hwval_pin *hwval_pin_at(uint8_t index)
{
    return (index < HWVAL_PIN_TABLE_N) ? &hwval_pin_table[index] : NULL;
}

int hwval_pin_claim(uint8_t port, uint8_t num, hwval_pin_owner_t who)
{
    uint8_t idx = hwval_pin_index(port, num);

    if (idx == 0xFFU) {
        return -1;
    }
    if (s_owner[idx] == (uint8_t)PIN_OWNER_FREE) {
        s_owner[idx] = (uint8_t)who;
        return 0;
    }
    return -1;
}

void hwval_pin_release(uint8_t port, uint8_t num)
{
    uint8_t idx = hwval_pin_index(port, num);

    if (idx != 0xFFU) {
        s_owner[idx] = (uint8_t)PIN_OWNER_FREE;
    }
}

int hwval_pin_may_drive(uint8_t port, uint8_t num, hwval_pin_owner_t who)
{
    uint8_t idx = hwval_pin_index(port, num);

    if (idx == 0xFFU) {
        return 0;
    }
    if ((hwval_pin_table[idx].owner == PIN_OWNER_SWDBG) ||
        (hwval_pin_table[idx].owner == PIN_OWNER_CONSOLE) ||
        (hwval_pin_table[idx].owner == PIN_OWNER_LSE)) {
        return 0; /* hard-con reserved (CON-005) */
    }
    if ((s_owner[idx] == (uint8_t)PIN_OWNER_FREE) ||
        (s_owner[idx] == (uint8_t)who)) {
        return (int)hwval_pin_table[idx].drivable;
    }
    return 0;
}

/* is a pin an endpoint of any harness wire? (GPIO-003 exclusion list) */
int hwval_pin_is_wire_endpoint(uint8_t port, uint8_t num)
{
    uint16_t want = (uint16_t)((port << 8U) | num);

    for (uint8_t i = 0U; i < HWVAL_WIRE_TABLE_N; i++) {
        uint16_t a = (uint16_t)((hwval_wire_table[i].ap << 8U) |
                                hwval_wire_table[i].an);
        uint16_t b = (uint16_t)((hwval_wire_table[i].bp << 8U) |
                                hwval_wire_table[i].bn);
        if ((a == want) || (b == want)) {
            return 1;
        }
    }
    return 0;
}

const struct hwval_wire *hwval_wire_at(int index)
{
    return (index < HWVAL_WIRE_TABLE_N) ? &hwval_wire_table[index] : NULL;
}