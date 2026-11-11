# Hardware Reference — NUCLEO-F767ZI

| | |
|---|---|
| **Author** | Sandesh Ghimire <sandesh@soccentric.com> |
| **Date** | 2026-11-02 |
| **Board** | ST NUCLEO-F767ZI (Nucleo-144 family) |
| **MCU** | STM32F767ZIT6 — Arm Cortex-M7 r1p0, LQFP144 |
| **Debugger** | On-board ST-LINK/V2.1 |

This document describes the development board used by this project. It has two
kinds of information:

- **Measured** — read from the connected board over SWD/USB on 2026-11-02.
  These values are specific to *this* board.
- **Reference** — taken from ST documentation for the board and the chip.
  Always cross-check against the documents listed in [References](#14-references)
  and the silkscreen on the PCB before relying on a pin or jumper.

---

## Contents

1. [Board at a glance](#1-board-at-a-glance)
2. [Measured board identity](#2-measured-board-identity)
3. [Microcontroller](#3-microcontroller)
4. [Memory map](#4-memory-map)
5. [Flash organisation and option bytes](#5-flash-organisation-and-option-bytes)
6. [Clocks](#6-clocks)
7. [Power](#7-power)
8. [Debug and programming](#8-debug-and-programming)
9. [On-board user I/O](#9-on-board-user-io)
10. [Ethernet](#10-ethernet)
11. [USB OTG FS (user USB)](#11-usb-otg-fs-user-usb)
12. [Expansion headers (Arduino / Zio)](#12-expansion-headers-arduino--zio)
13. [Pin conflicts and reserved pins](#13-pin-conflicts-and-reserved-pins)
14. [References](#14-references)
15. [Reproducing the measurements](#15-reproducing-the-measurements)

---

## 1. Board at a glance

```
            +----------------------------------------------------+
  USB  ---> | ST-LINK/V2.1  (SWD + Virtual COM + USB drive)       |
 (micro-B)  +--------------------------+-------------------------+
                                       | SWD / USART3 / 8 MHz MCO
            +--------------------------v-------------------------+
            |                 STM32F767ZIT6                       |
            |  Cortex-M7 @ 216 MHz | 2 MB Flash | 512 KB SRAM      |
            +---+---------+---------+---------+---------+--------+
                |         |         |         |         |
             3 LEDs    User btn   RJ45 Eth   USB OTG   Zio/Arduino
             PB0/7/14  PC13      LAN8742A   FS micro-AB + morpho headers
```

| Feature | Detail |
|---|---|
| CPU | Cortex-M7, 216 MHz max, double-precision FPU, L1 cache 16 KB I + 16 KB D |
| Flash | 2 MB, single-bank or dual-bank mode (single-bank on this board) |
| RAM | 512 KB total (DTCM 128 KB + SRAM1 368 KB + SRAM2 16 KB) + 16 KB ITCM RAM + 4 KB backup SRAM |
| Debugger | ST-LINK/V2.1: SWD, virtual COM port, drag-and-drop flashing |
| User LEDs | 3 (green, blue, red) |
| Buttons | User (blue) and Reset (black) |
| Ethernet | 10/100 Mbit, RMII, Microchip LAN8742A PHY, RJ45 |
| USB | USB OTG FS on a micro-AB connector, with VBUS switch and over-current detection |
| Expansion | ST Zio connector (Arduino Uno V3 compatible) + ST morpho headers (all MCU I/O) |
| Clocks | 8 MHz from ST-LINK MCO (HSE bypass), 32.768 kHz LSE crystal |
| Power | USB 5 V from ST-LINK (default), VIN 7–12 V, E5V, or USB charger |

---

## 2. Measured board identity

All values below were read from the connected board.

### 2.1 ST-LINK probe

| Item | Value |
|---|---|
| USB VID:PID | `0483:374b` (ST-LINK/V2.1) |
| USB speed | Full speed (12 Mbit/s), USB 2.0 |
| Serial number | `0671FF525171494867105524` |
| Firmware | `V2J40M27` (JTAG/SWD engine v40, mass-storage/VCP v27) |
| USB interfaces | 4: vendor debug, mass storage, CDC control, CDC data |
| Requested bus power | 300 mA |
| Virtual COM port | `/dev/ttyACM0` (`/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_0671FF525171494867105524-if02`) |
| USB drive | `/dev/sda`, FAT, label `NOD_F767ZI`, ~6 MB |
| Target voltage | 3.265 V |

### 2.2 MCU identification

| Register | Address | Raw value | Decoded |
|---|---|---|---|
| `DBGMCU_IDCODE` | `0xE0042000` | `0x10016451` | DEV_ID `0x451` (STM32F76x/F77x), REV_ID `0x1001` = **revision Z** |
| `SCB_CPUID` | `0xE000ED00` | `0x411FC270` | Arm Cortex-M7, variant r1, revision p0 (**r1p0**) |
| Flash size | `0x1FF0F442` | `0x0800` | 2048 KB |
| Unique ID [31:0] | `0x1FF0F420` | `0x00290028` | Die X = 0x0028 (40), Y = 0x0029 (41) on the wafer |
| Unique ID [63:32] | `0x1FF0F424` | `0x3031510D` | Wafer number 0x0D (13); lot bytes `Q 1 0` |
| Unique ID [95:64] | `0x1FF0F428` | `0x30373635` | Lot bytes `5 6 7 0` |

Debug resources reported by OpenOCD: 8 hardware breakpoints, 4 watchpoints.

### 2.3 Firmware currently on the chip

The flash is **not blank**. Some firmware, most likely ST's factory demo, is
running at power-up:

| Item | Value | Meaning |
|---|---|---|
| Initial stack pointer | `0x20080000` | Top of the 512 KB SRAM region |
| Reset vector | `0x080018A9` | Thumb entry point in flash |
| PC when halted | `0x08001B6E` | Code running from flash |
| System clock | 96 MHz | PLL from HSI 16 MHz (M=8, N=96, P=2), see §6.3 |
| Serial output | none in 3 s | Nothing printed on the VCP |

The first flash of this project will overwrite it. If you want to keep it, read
it back first (see §15).

---

## 3. Microcontroller

**STM32F767ZIT6**: `F7` high-performance line, `67` with Ethernet, USB HS,
LCD-TFT and JPEG and without the crypto unit (that is the F777),
`Z` = 144 pins, `I` = 2 MB flash, `T` = LQFP, `6` = −40…85 °C.

### 3.1 Core

| Feature | Detail |
|---|---|
| Core | Arm Cortex-M7, 6-stage superscalar pipeline with branch prediction |
| Max frequency | 216 MHz (needs voltage scale 1 + over-drive) |
| FPU | Single and double precision (FPv5-D16) |
| Caches | 16 KB instruction + 16 KB data L1, with ECC |
| TCM | 16 KB ITCM RAM, 128 KB DTCM RAM (zero wait state) |
| MPU | Yes |
| ART accelerator | Flash prefetch / cache on the ITCM flash path |
| Debug | SWD, JTAG, SWO trace, ETM (4-bit trace on the morpho header) |

### 3.2 Peripherals

| Group | Peripherals |
|---|---|
| Timers | 2 advanced (TIM1, TIM8), 32-bit general purpose (TIM2, TIM5), 16-bit general purpose, 2 basic (TIM6, TIM7), 1 low-power (LPTIM1), 2 watchdogs, SysTick |
| Analog | 3 × 12-bit ADC (2.4 MSPS, up to 24 channels), 2 × 12-bit DAC, temperature sensor, VREFINT |
| Serial | 4 × USART + 4 × UART, 6 × SPI (3 with I2S), 4 × I2C, 3 × CAN 2.0B, 2 × SAI, SPDIF-RX, HDMI-CEC |
| Storage | 2 × SDMMC, Quad-SPI, FMC (SRAM, NOR, NAND, SDRAM) |
| Connectivity | 10/100 Ethernet MAC with DMA, IEEE 1588; USB OTG FS; USB OTG HS (ULPI) |
| Graphics / media | LCD-TFT controller, Chrom-ART (DMA2D), hardware JPEG codec, DCMI camera interface |
| Signal processing | DFSDM (digital filter for sigma-delta modulators) |
| Other | True RNG, CRC unit, RTC with backup registers, 2 × general DMA (16 streams) |
| I/O | Up to 114 GPIO on LQFP144, most 5 V tolerant |

---

## 4. Memory map

The flash and system memory can each be reached through two paths: the
**AXIM** bus (cached by L1) and the **ITCM** bus (accelerated by ART).

| Region | Start | End | Size | Notes |
|---|---|---|---|---|
| ITCM RAM | `0x0000 0000` | `0x0000 3FFF` | 16 KB | Zero wait state, CPU only |
| System memory (ITCM) | `0x0010 0000` | `0x0010 EDBF` | ~60 KB | ST ROM bootloader |
| Flash (ITCM) | `0x0020 0000` | `0x003F FFFF` | 2 MB | Default boot address (BOOT_ADD0) |
| Flash (AXIM) | `0x0800 0000` | `0x081F FFFF` | 2 MB | Usual link address for code |
| System memory (AXIM) | `0x1FF0 0000` | `0x1FF0 EDBF` | ~60 KB | ST ROM bootloader |
| OTP | `0x1FF0 F000` | `0x1FF0 F41F` | 1 KB + lock bytes | One-time programmable |
| Unique ID | `0x1FF0 F420` | `0x1FF0 F42B` | 96 bits | See §2.2 |
| Flash size register | `0x1FF0 F442` | | 16 bits | |
| DTCM RAM | `0x2000 0000` | `0x2001 FFFF` | 128 KB | Zero wait state; also reachable by DMA |
| SRAM1 | `0x2002 0000` | `0x2007 BFFF` | 368 KB | Cached through AXIM |
| SRAM2 | `0x2007 C000` | `0x2007 FFFF` | 16 KB | |
| APB1 peripherals | `0x4000 0000` | `0x4000 FFFF` | | TIM2–7, USART2/3, UART4/5/7/8, I2C1–4, CAN, DAC… |
| APB2 peripherals | `0x4001 0000` | `0x4001 FFFF` | | TIM1/8, USART1/6, ADC, SPI1/4/5/6, SAI, SDMMC… |
| AHB1 peripherals | `0x4002 0000` | `0x4007 FFFF` | | GPIO, RCC, flash interface, DMA, Ethernet, USB HS |
| Backup SRAM | `0x4002 4000` | `0x4002 4FFF` | 4 KB | Kept in VBAT domain |
| AHB2 peripherals | `0x5000 0000` | | | USB OTG FS, DCMI, JPEG, RNG |
| FMC banks | `0x6000 0000` | `0xDFFF FFFF` | | External memory |
| Quad-SPI | `0x9000 0000` | `0x9FFF FFFF` | 256 MB | Memory-mapped external flash |
| Cortex-M7 private | `0xE000 0000` | `0xE00F FFFF` | | NVIC, SysTick, SCB, debug |
| DBGMCU | `0xE004 2000` | | | Chip ID and debug freeze control |

**Linker hint:** a typical layout puts `.text` at `0x08000000`, the stack and
fast data in DTCM (`0x20000000`, 128 KB), and the heap and DMA buffers in SRAM1.
Buffers in SRAM1 used by DMA must be cache-maintained or placed in a region the
MPU marks non-cacheable.

---

## 5. Flash organisation and option bytes

### 5.1 Sector layout (measured: single-bank mode)

| Sector | Offset | Address (AXIM) | Size |
|---|---|---|---|
| 0 | `0x000000` | `0x0800 0000` | 32 KB |
| 1 | `0x008000` | `0x0800 8000` | 32 KB |
| 2 | `0x010000` | `0x0801 0000` | 32 KB |
| 3 | `0x018000` | `0x0801 8000` | 32 KB |
| 4 | `0x020000` | `0x0802 0000` | 128 KB |
| 5 | `0x040000` | `0x0804 0000` | 256 KB |
| 6 | `0x080000` | `0x0808 0000` | 256 KB |
| 7 | `0x0C0000` | `0x080C 0000` | 256 KB |
| 8 | `0x100000` | `0x0810 0000` | 256 KB |
| 9 | `0x140000` | `0x0814 0000` | 256 KB |
| 10 | `0x180000` | `0x0818 0000` | 256 KB |
| 11 | `0x1C0000` | `0x081C 0000` | 256 KB |

All sectors are currently **not write-protected**. Flash is erased per sector,
not per page. `st-info` reports a 2 KB page size, which does not match this chip
and can be ignored.

In dual-bank mode (option bit `nDBANK = 0`) the layout becomes two banks of
1 MB, each with 4 × 16 KB, 1 × 64 KB and 7 × 128 KB sectors. Small sectors at
the start are useful for a bootloader or for storing settings.

### 5.2 Option bytes (measured)

`FLASH_OPTCR` (`0x40023C14`) = `0xFFFFAAFD`
`FLASH_OPTCR1` (`0x40023C18`) = `0x00400080`

| Field | Value | Meaning |
|---|---|---|
| RDP | `0xAA` | Read protection **level 0**: flash can be read and debugged |
| nWRP[11:0] | all 1 | No sector write-protected |
| BOR_LEV | `11` | Brown-out reset **off** (only the POR/PDR threshold, ~1.7 V, is active) |
| WWDG_SW / IWDG_SW | 1 / 1 | Watchdogs enabled by software, not by hardware |
| nRST_STOP / nRST_STDBY | 1 / 1 | No reset when entering Stop / Standby |
| IWDG_STOP / IWDG_STDBY | 1 / 1 | IWDG keeps running in Stop / Standby |
| nDBANK | 1 | **Single-bank** mode (2 MB, 12 sectors) |
| nDBOOT | 1 | Dual-bank boot disabled |
| BOOT_ADD0 | `0x0080` | BOOT0 pin low → boot from `0x00200000` (flash via ITCM) |
| BOOT_ADD1 | `0x0040` | BOOT0 pin high → boot from `0x00100000` (system bootloader) |

`FLASH_ACR` (`0x40023C00`) = `0x00000303`: 3 wait states, prefetch on, ART on.
This matches the 96 MHz the current firmware runs at.

**Recommendation:** consider setting `BOR_LEV` to level 3 (~2.7 V) for
production builds so the chip resets cleanly on a sagging supply. Never change
RDP to level 2: that is permanent and disables debug for good.

---

## 6. Clocks

### 6.1 Clock sources on the board

| Source | Frequency | Board connection |
|---|---|---|
| HSI | 16 MHz | Internal RC, ±1 % |
| HSE | 8 MHz | From the ST-LINK MCU's MCO into `PH0/OSC_IN`. Configure HSE in **bypass** mode. A crystal footprint is present but not fitted by default |
| LSI | ~32 kHz | Internal RC, for the IWDG |
| LSE | 32.768 kHz | Crystal on `PC14/PC15`, for the RTC |

### 6.2 Typical 216 MHz configuration

| Setting | Value |
|---|---|
| PLL source | HSE bypass 8 MHz |
| PLLM / PLLN / PLLP / PLLQ | 4 / 216 / 2 / 9 (PLL48CLK = 48 MHz for USB) |
| SYSCLK / HCLK | 216 MHz |
| APB1 (PCLK1) | 54 MHz (/4); APB1 timers 108 MHz |
| APB2 (PCLK2) | 108 MHz (/2); APB2 timers 216 MHz |
| Voltage scale | Scale 1 + over-drive enabled |
| Flash latency | 7 wait states (at 2.7–3.6 V) |

### 6.3 Clock state of the firmware currently on the chip (measured)

| Register | Value | Decoded |
|---|---|---|
| `RCC_CR` | `0x03007F83` | HSI on and ready, HSE off, PLL on and ready |
| `RCC_PLLCFGR` | `0x24001808` | Source HSI; M = 8, N = 96, P = /2, Q = 4 |
| `RCC_CFGR` | `0x0000100A` | SYSCLK = PLL; AHB /1; APB1 /2; APB2 /1 |
| `RCC_BDCR` | `0x00000000` | LSE and RTC off |

Result: SYSCLK = 16 MHz / 8 × 96 / 2 = **96 MHz**, APB1 = 48 MHz, APB2 = 96 MHz.

---

## 7. Power

| Source | Input | Notes |
|---|---|---|
| ST-LINK USB (U5V) | 5 V from the ST-LINK USB port | Default. The ST-LINK requests 300 mA from the host; see UM1974 before drawing more |
| VIN | 7–12 V on the Zio / morpho header | Through the on-board 5 V regulator, up to ~800 mA |
| E5V | External 5 V on the morpho header | |
| CHGR | 5 V from a USB charger on the ST-LINK port | ST-LINK does not enumerate |

- The 5 V source is selected with a jumper (JP5 on Nucleo-144 boards).
- The MCU runs from 3.3 V generated on the board. Measured: **3.265 V**.
- An IDD jumper lets you put an ammeter in series with the MCU supply to
  measure its current.
- A power LED shows that 5 V is present, and an LED next to the user USB
  connector shows a VBUS over-current fault.
- GPIOs are 3.3 V. Most pins are 5 V tolerant as inputs, but not the ones used
  as analog inputs (ADC/DAC) or `PA4/PA5` in DAC mode. Check the `FT` column of
  the datasheet pin table.

---

## 8. Debug and programming

### 8.1 Interfaces

| Function | MCU pins | Notes |
|---|---|---|
| SWDIO | PA13 | Shared with JTMS. Do not reconfigure, or you lose debug access |
| SWCLK | PA14 | Shared with JTCK |
| SWO | PB3 | Trace output for ITM `printf`. Routed to the ST-LINK |
| NRST | NRST | Driven by the ST-LINK and the black reset button |
| VCP TX (MCU → PC) | PD8 (USART3_TX) | Appears as `/dev/ttyACM0` |
| VCP RX (PC → MCU) | PD9 (USART3_RX) | |
| BOOT0 | BOOT0 pin | Pulled low on the board → boots from flash |

The ST-LINK can be disconnected from the on-board MCU with its selection
jumpers so that it can debug an external target through the SWD connector.
Leave both jumpers fitted to debug the on-board MCU.

### 8.2 Host tools (installed by `scripts/install-tools.sh`)

| Tool | Version | Use |
|---|---|---|
| `arm-none-eabi-gcc` | 13.2.rel1 | Cross compiler |
| `gdb-multiarch` | 15.1 | Debugger |
| `openocd` | 0.12.0 | GDB server and flashing. Board config: `board/st_nucleo_f7.cfg` |
| `st-info` / `st-flash` | 1.8.0 | Quick probe and flash |
| `picocom` | 3.1 | Serial console |

### 8.3 Common commands

Flash an ELF file and reset:

```bash
openocd -f board/st_nucleo_f7.cfg -c "program build/firmware.elf verify reset exit"
```

Flash a raw binary at the start of flash:

```bash
st-flash --reset write build/firmware.bin 0x08000000
```

Start a GDB server (port 3333):

```bash
openocd -f board/st_nucleo_f7.cfg
```

Open the serial console (Ctrl-A Ctrl-X to quit):

```bash
picocom -b 115200 /dev/ttyACM0
```

You can also copy a `.bin` file onto the `NOD_F767ZI` USB drive. The ST-LINK
writes it to flash at `0x08000000` and resets the chip.

### 8.4 Recovery

If firmware disables SWD, enters low power right after reset, or crashes too
early to attach, connect under reset:

```bash
openocd -f board/st_nucleo_f7.cfg -c "reset_config srst_only connect_assert_srst" -c "init; reset halt; flash erase_sector 0 0 last; exit"
```

---

## 9. On-board user I/O

| Item | Colour / type | MCU pin | Active level | Notes |
|---|---|---|---|---|
| LD1 | Green LED | PB0 | High = on | Can be moved to PA5 with a solder bridge |
| LD2 | Blue LED | PB7 | High = on | |
| LD3 | Red LED | PB14 | High = on | |
| B1 USER | Blue push button | PC13 | **High when pressed** | External pull-down on the board; EXTI line 13 |
| B2 RESET | Black push button | NRST | Low = reset | |

Other LEDs on the board are driven by hardware, not the MCU: the ST-LINK
communication LED (red/green, blinks during debug traffic), the 5 V power LED,
and the USB OTG VBUS / over-current LEDs.

---

## 10. Ethernet

The MCU's Ethernet MAC connects to a **Microchip LAN8742A** PHY over RMII. The
PHY's 50 MHz reference clock comes from its own 25 MHz crystal, and the PHY
drives `REF_CLK` back to the MCU.

| RMII signal | MCU pin | Alternate function |
|---|---|---|
| REF_CLK | PA1 | AF11 |
| MDIO | PA2 | AF11 |
| MDC | PC1 | AF11 |
| CRS_DV | PA7 | AF11 |
| RXD0 | PC4 | AF11 |
| RXD1 | PC5 | AF11 |
| TX_EN | PG11 | AF11 |
| TXD0 | PG13 | AF11 |
| TXD1 | PB13 | AF11 |

- PHY address: **0** (on the MDIO bus).
- Set `SYSCFG_PMC.MII_RMII_SEL = 1` (RMII) **before** enabling the Ethernet clocks.
- Ethernet DMA descriptors and buffers must be in SRAM that the MPU marks
  non-cacheable, or the code must clean/invalidate the D-cache around every
  transfer. Otherwise the MAC and the CPU see different data.

---

## 11. USB OTG FS (user USB)

| Signal | MCU pin | Notes |
|---|---|---|
| D− | PA11 | OTG_FS_DM, AF10 |
| D+ | PA12 | OTG_FS_DP, AF10 |
| VBUS sense | PA9 | OTG_FS_VBUS |
| ID | PA10 | OTG_FS_ID (host/device detect) |
| VBUS switch enable | PG6 | Drives the power switch that supplies 5 V in host mode |
| Over-current | PG7 | Input from the power switch, low on fault |

- Connector: micro-AB on the opposite edge from the ST-LINK USB port.
- USB needs an accurate 48 MHz clock: use HSE + PLLQ, not HSI.
- PA9 is also USART1_TX. Do not use USART1 on PA9 while USB is enabled.

---

## 12. Expansion headers (Arduino / Zio)

The Zio connector is a superset of the Arduino Uno V3 header. The table lists
the Arduino-compatible pins. The ST morpho headers on both outer edges bring
out every MCU pin.

### 12.1 Digital (Arduino D0–D15)

| Arduino | MCU pin | Main functions |
|---|---|---|
| D0 | PG9 | USART6_RX |
| D1 | PG14 | USART6_TX |
| D2 | PF15 | GPIO |
| D3 | PE13 | TIM1_CH3 |
| D4 | PF14 | GPIO |
| D5 | PE11 | TIM1_CH2 |
| D6 | PE9 | TIM1_CH1 |
| D7 | PF13 | GPIO |
| D8 | PF12 | GPIO |
| D9 | PD15 | TIM4_CH4 |
| D10 | PD14 | SPI1_NSS (software), TIM4_CH3 |
| D11 | PA7 | SPI1_MOSI, TIM14_CH1 — **conflicts with Ethernet CRS_DV** |
| D12 | PA6 | SPI1_MISO |
| D13 | PA5 | SPI1_SCK |
| D14 | PB9 | I2C1_SDA |
| D15 | PB8 | I2C1_SCL |

### 12.2 Analog (Arduino A0–A5)

| Arduino | MCU pin | ADC channel |
|---|---|---|
| A0 | PA3 | ADC123_IN3 |
| A1 | PC0 | ADC123_IN10 |
| A2 | PC3 | ADC123_IN13 |
| A3 | PF3 | ADC3_IN9 |
| A4 | PF5 | ADC3_IN15 |
| A5 | PF10 | ADC3_IN8 |

---

## 13. Pin conflicts and reserved pins

| Pin(s) | Used by | Watch out |
|---|---|---|
| PA13, PA14 | SWD | Reconfiguring them locks out the debugger (recover with §8.4) |
| PB3 | SWO | Free only if SWO trace is not used |
| PD8, PD9 | ST-LINK virtual COM (USART3) | Use as the main console |
| PH0, PH1 | HSE input (MCO from ST-LINK) | Not available as GPIO |
| PC14, PC15 | LSE crystal | Not available as GPIO |
| PA7 | Ethernet CRS_DV and Arduino D11 | Use one or the other; a solder bridge selects the routing |
| PB13 | Ethernet TXD1 | Not free when Ethernet is used |
| PA1, PA2, PC1, PC4, PC5, PG11, PG13 | Ethernet RMII | Reserved if Ethernet is used |
| PA9–PA12, PG6, PG7 | USB OTG FS | Reserved if USB is used |
| PB0, PB7, PB14 | LEDs | Usable as outputs, but the LEDs will light |
| PC13 | User button | Has an external pull-down |

---

## 14. References

| Document | Content |
|---|---|
| **UM1974** — STM32 Nucleo-144 boards user manual | Board schematics, jumpers, solder bridges, connectors |
| **DS11532** — STM32F765xx/F767xx/F768Ax/F769xx datasheet | Pinout, alternate functions, electrical characteristics |
| **RM0410** — STM32F76xxx/F77xxx reference manual | Every peripheral and register |
| **PM0253** — STM32F7/H7 Cortex-M7 programming manual | Core, caches, MPU, instruction set |
| **ES0334** — STM32F76x/F77x errata sheet | Silicon bugs. Check entries for **revision Z** |
| **AN4667** — STM32F7 system architecture and performance | Bus matrix, caches, TCM, DMA and memory placement |
| **LAN8742A datasheet** (Microchip) | Ethernet PHY registers |

All ST documents can be found on st.com by searching for the document number.

---

## 15. Reproducing the measurements

Probe the ST-LINK and the chip:

```bash
st-info --probe
```

Read the identity registers and option bytes:

```bash
openocd -f board/st_nucleo_f7.cfg -c "init; halt; mdw 0xE0042000; mdh 0x1FF0F442; mdw 0x1FF0F420 3; mdw 0xE000ED00; mdw 0x40023C14; mdw 0x40023C18; resume; exit"
```

Show the flash sector layout and protection:

```bash
openocd -f board/st_nucleo_f7.cfg -c "init; flash info 0; exit"
```

Back up the firmware currently on the chip (2 MB):

```bash
st-flash read backup-factory.bin 0x08000000 0x200000
```

List the USB descriptors of the ST-LINK:

```bash
lsusb -v -d 0483:374b
```
