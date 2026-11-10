# Software Requirements Specification — NUCLEO-F767ZI Hardware Validation Firmware

| | |
|---|---|
| **Document** | SRS-HWVAL-F767 |
| **Version** | 0.2 (draft) |
| **Author** | Sandesh Ghimire <sandesh@soccentric.com> |
| **Date** | 2026-11-03 (v0.2: HWLINK link mode for the GUI, §6.4) |
| **Status** | Draft for review |
| **Hardware** | NUCLEO-F767ZI, STM32F767ZIT6 rev Z (see [Hardware.md](Hardware.md)) |
| **Software base** | FreeRTOS V10.2.0 (as shipped by STM32CubeF7 v1.17.4; the SRS originally said V10.2.1 — CL-011) and HAL/LL from STM32CubeF7 v1.17.4 | |

---

## Contents

1. [Introduction](#1-introduction)
2. [Overall description](#2-overall-description)
3. [Test environment and test levels](#3-test-environment-and-test-levels)
4. [General firmware requirements](#4-general-firmware-requirements)
5. [FreeRTOS requirements](#5-freertos-requirements)
6. [Terminal (CLI) requirements](#6-terminal-cli-requirements)
7. [Test framework requirements](#7-test-framework-requirements)
8. [Hardware test requirements](#8-hardware-test-requirements)
9. [Safety and protection requirements](#9-safety-and-protection-requirements)
10. [Non-functional requirements](#10-non-functional-requirements)
11. [Host-side tooling requirements](#11-host-side-tooling-requirements)
12. [Coverage matrix](#12-coverage-matrix)
- [Appendix A — Loopback harness](#appendix-a--loopback-harness)
- [Appendix B — External equipment](#appendix-b--external-equipment)
- [Appendix C — Test ID catalogue format](#appendix-c--test-id-catalogue-format)
- [Appendix D — Open items](#appendix-d--open-items)

---

## 1. Introduction

### 1.1 Purpose

This document specifies the requirements for **HWVAL**, a FreeRTOS-based
firmware application that verifies and validates every hardware feature of the
NUCLEO-F767ZI board and of the STM32F767ZI microcontroller on it. The
application is operated from a terminal over the ST-LINK virtual COM port
(USART3 → USB → `/dev/ttyACM0`), or from the HWVAL Studio GUI [GUI] over the
same port using the binary HWLINK protocol [LINK] (link mode, §6.4).

### 1.2 Scope

HWVAL shall:

- test every functional block of the STM32F767ZI that is usable on this board;
- test every component fitted on the NUCLEO-F767ZI that the MCU can reach;
- explicitly account for every block that **cannot** be exercised on this
  board, with the reason, so that coverage is 100 % of the hardware inventory;
- present all tests through an interactive, scriptable terminal interface;
- present the same tests to the HWVAL Studio GUI through HWLINK link mode;
- report results in a form that both a person and a host script can read.

HWVAL is a validation tool, not a product application. It does not cover
production programming, calibration storage or field updates; field updates
are done by the A/B bootloader [BOOT], which HWVAL does not run under.

### 1.3 Definitions

| Term | Meaning |
|---|---|
| **HWVAL** | The hardware validation firmware specified here |
| **CLI** | The command-line interface HWVAL presents on the VCP |
| **VCP** | ST-LINK virtual COM port, wired to USART3 (PD8 TX, PD9 RX) |
| **Test case** | One self-contained check with a unique ID and a PASS/FAIL result |
| **Test group** | A set of test cases for one hardware block (e.g. `ADC`) |
| **Harness** | Passive jumper wires between header pins, defined in Appendix A |
| **Operator** | The person at the bench, when a test needs observation or action |
| **DUT** | Device under test: the NUCLEO-F767ZI board |
| **N/A** | Hardware block not present or not usable on this board |
| **Destructive** | A test that changes non-volatile state (flash, backup domain) |
| **HWLINK** | Binary framing (COBS + CRC32) and FlatBuffers protocol shared by HWVAL, the bootloader, the demo app and the GUI [LINK] |
| **Link mode** | CLI state in which USART3 carries HWLINK frames instead of text (§6.4) |
| **GUI** | HWVAL Studio, the cross-platform host application [GUI] |

### 1.4 Requirement conventions

- **shall** = mandatory, **should** = recommended, **may** = optional.
- Each requirement has a unique ID: `<AREA>-<NNN>`.
- Each requirement states a **level** (§3.2) and a **verification method**:

| Method | Meaning |
|---|---|
| **T** | Test — executed by HWVAL or a host script with a pass/fail outcome |
| **D** | Demonstration — shown working, observed by the operator |
| **I** | Inspection — of source code, build output or documentation |
| **A** | Analysis — by calculation or review of measurements |

### 1.5 References

| ID | Document |
|---|---|
| [HW] | [Hardware.md](Hardware.md) — board reference for this project |
| [UM1974] | ST, STM32 Nucleo-144 boards user manual |
| [DS11532] | ST, STM32F765xx/F767xx/F768Ax/F769xx datasheet |
| [RM0410] | ST, STM32F76xxx/F77xxx reference manual |
| [PM0253] | ST, STM32F7/H7 Cortex-M7 programming manual |
| [ES0334] | ST, STM32F76x/F77x errata sheet (revision Z) |
| [AN4667] | ST, STM32F7 system architecture and performance |
| [LAN8742A] | Microchip, LAN8742A Ethernet PHY datasheet |
| [FRTOS] | FreeRTOS V10.2.0 reference manual (shipped middleware of [CubeF7]) | |
| [DOCKER] | [docker/README.md](docker/README.md) — build environment |
| [GUI] | [validation-gui/SRS.md](https://github.com/soccentric-stm32/validation-gui/blob/HEAD/SRS.md) — SRS-GUI-F767, HWVAL Studio host GUI |
| [LINK] | [validation-gui/SRS.md, Appendix A](https://github.com/soccentric-stm32/validation-gui/blob/HEAD/SRS.md#appendix-a--hwlink-protocol) — HWLINK protocol (normative) |
| [BOOT] | [bootloader/SRS.md](https://github.com/soccentric-stm32/bootloader/blob/HEAD/SRS.md) — SRS-BOOT-F767, A/B bootloader |

---

## 2. Overall description

### 2.1 Product perspective

```
 Host PC                                     NUCLEO-F767ZI
+------------------+   USB (ST-LINK)   +-------------------------------+
| terminal /       |<=================>| ST-LINK/V2.1                  |
| hwtest.py runner |   VCP 115200 8N1  |   VCP  <-> USART3 (PD8/PD9)   |
| OpenOCD (flash,  |   SWD / SWO       |   SWD  <-> PA13/PA14, SWO PB3 |
|  SWO capture)    |                   |   MCO 8 MHz -> PH0 (HSE byp.) |
+------------------+                   |                               |
                                       |  STM32F767ZI running HWVAL    |
 Optional equipment (Appendix B)       |   FreeRTOS                    |
  Ethernet network ------------------->|   CLI task / test runner      |
  USB cable / USB stick -------------->|   test groups (§8)            |
  microSD, CAN, QSPI, I2C modules ---->|                               |
  scope, ammeter, bench supply ------->|  Harness on Zio/morpho (App.A)|
                                       +-------------------------------+
```

### 2.2 Users

| User | Use |
|---|---|
| Firmware developer | Bring-up, regression after board or driver changes |
| Hardware engineer | Board validation, fault finding on a specific peripheral or pin |
| Test technician | Running the full suite and recording the report |
| CI / host script | Unattended runs of all tests that need no operator |
| HWVAL Studio GUI | Interactive and operator-mode runs, run history and reports over HWLINK [GUI] |

### 2.3 Constraints

| ID | Constraint |
|---|---|
| CON-001 | The firmware shall be built with the arm-none-eabi GCC toolchain in the project Docker image [DOCKER]. |
| CON-002 | The firmware shall use FreeRTOS V10.2.0 (the version CubeF7 v1.17.4 ships, CL-011) and the HAL/LL drivers from STM32CubeF7 v1.17.4 as shipped in the image, unmodified. Fixes shall be applied in project-owned files. |
| CON-003 | The firmware shall run from internal flash and internal RAM only. The board has no external memory. |
| CON-004 | The CLI shall use USART3 on the ST-LINK VCP. USART3 therefore cannot be loopback-tested as a free peripheral (see UART-004). |
| CON-005 | SWD pins (PA13, PA14) and the HSE input (PH0, PH1) shall never be reconfigured. |
| CON-006 | Pins shared by on-board circuits (Ethernet RMII, USB OTG FS, LEDs, button, LSE) shall only be driven by the test that owns that circuit. |

### 2.4 Assumptions

| ID | Assumption |
|---|---|
| ASM-001 | The board is in the default ST jumper and solder-bridge configuration [UM1974] unless a test states otherwise. |
| ASM-002 | The board is powered from the ST-LINK USB port unless a power test asks for another source. |
| ASM-003 | Option bytes are at the measured defaults [HW §5.2]: RDP level 0, single-bank, no write protection. |
| ASM-004 | Flash sector 11 (`0x081C0000`–`0x081FFFFF`, 256 KB) is free for destructive flash tests (FLS-*). |
| ASM-005 | HWVAL runs only in the bench configuration (single-bank, flashed over SWD at `0x0800 0000`). The product configuration (dual-bank, bootloader + application slots) is set up and torn down by the GUI over SWD [BOOT §2.5]; HWVAL is never loaded by the bootloader. |

---

## 3. Test environment and test levels

### 3.1 Setup

| ID | Requirement | Level | Method |
|---|---|---|---|
| ENV-001 | HWVAL shall be fully usable with only the ST-LINK USB cable connected (Level S tests). | S | T |
| ENV-002 | HWVAL shall detect which harness wires (Appendix A) are fitted before running a Level L test, and report SKIP with reason `NO_HARNESS` when the wire is missing. | L | T |
| ENV-003 | HWVAL shall ask the operator before a Level E test, naming the equipment required, and report SKIP when the operator declines. | E | D |
| ENV-004 | Every test case shall declare exactly one level and whether it needs an operator. | All | I |

### 3.2 Test levels

| Level | Name | Needs |
|---|---|---|
| **S** | Self-contained | The board and the ST-LINK USB cable only |
| **L** | Loopback | The passive jumper harness of Appendix A |
| **E** | External equipment | Items in Appendix B (network, USB devices, modules, instruments) |
| **O** (flag) | Operator | A person to observe (e.g. LED colour) or act (press a button, replug). Can be combined with S, L or E |

---

## 4. General firmware requirements

| ID | Requirement | Level | Method |
|---|---|---|---|
| GEN-001 | HWVAL shall build with a single CMake command in the Docker image and produce `.elf`, `.bin` and `.hex` images and a `.map` file. | — | I |
| GEN-002 | HWVAL shall be flashable with `scripts/docker-dev.sh flash` and boot without a debugger attached. | S | D |
| GEN-003 | HWVAL shall embed a version string (semantic version, git commit, build date) and print it on boot and with `version`. | S | T |
| GEN-004 | HWVAL shall run the core at 216 MHz from the PLL fed by the 8 MHz HSE bypass clock, with voltage scale 1, over-drive (and its switching-ready state), 7 flash wait states, ART and prefetch on, I-cache and D-cache on, unless a test changes this temporarily. Note (CL-009): on this HAL/board `CSR1.VOSRDY` never sets (ST's own F767ZI examples do not wait for it); the flow follows the ST example — scaling set, over-drive enable + wait `ODRDY`/`ODSWRDY`. | S | T |
| GEN-005 | Any test that changes clocks, caches, power modes, or peripheral state shall restore the default configuration of GEN-004 before it returns, or reset the MCU when restoring is not possible, and state that in its result. | All | T |
| GEN-006 | HWVAL shall link code and constants into flash sectors 0–10 only, keeping sector 11 free (ASM-004). The build shall fail if the image overlaps sector 11. | — | I |
| GEN-007 | HWVAL shall place DMA buffers in a memory region that the MPU marks non-cacheable, or perform explicit D-cache maintenance on them. | — | I |
| GEN-008 | HWVAL shall install handlers for HardFault, MemManage, BusFault and UsageFault that print the fault registers (CFSR, HFSR, MMFAR, BFAR), the stacked PC, LR and xPSR, and the current task name, then reset or halt according to a CLI setting. | S | T |
| GEN-009 | HWVAL shall keep the independent watchdog running during normal operation and refresh it from a dedicated low-priority task, so that a hung test causes a reset that is reported on the next boot. | S | T |
| GEN-010 | HWVAL shall print the reset cause (RCC_CSR flags) on every boot, then clear the flags. | S | T |
| GEN-011 | All source files shall carry the author header of the project. | — | I |
| GEN-012 | The HAL time base shall use a hardware timer other than SysTick (SysTick is owned by FreeRTOS). The timer chosen shall be documented and excluded from destructive timer tests while in use. | S | I |

---

## 5. FreeRTOS requirements

### 5.1 Configuration

| ID | Requirement | Level | Method |
|---|---|---|---|
| FRT-001 | HWVAL shall use the FreeRTOS `ARM_CM7` GCC port with the native FreeRTOS API. The CMSIS-RTOS wrappers shall not be required. | — | I |
| FRT-002 | The kernel tick shall be 1 kHz from SysTick. | S | T |
| FRT-003 | `configASSERT` shall be enabled and report file and line on the CLI before halting. | S | T |
| FRT-004 | `configCHECK_FOR_STACK_OVERFLOW` shall be 2, and the stack overflow hook shall print the task name. | S | T |
| FRT-005 | The malloc-failed hook shall be enabled and print the requested size and the free heap. | S | T |
| FRT-006 | The heap shall be `heap_4` and its size shall be configurable at build time. | — | I |
| FRT-007 | Run-time statistics shall be enabled, using the DWT cycle counter or a 32-bit timer as the time source. | S | T |
| FRT-008 | Interrupt priorities shall respect `configMAX_SYSCALL_INTERRUPT_PRIORITY`. Every ISR that calls a FreeRTOS `FromISR` API shall have a priority number equal to or higher (less urgent) than that value. | — | I |
| FRT-009 | The FPU context shall be saved per task (lazy stacking) so that tests using floating point in several tasks get correct results. | S | T |

### 5.2 Tasks

| ID | Requirement | Level | Method |
|---|---|---|---|
| FRT-010 | HWVAL shall have at least these tasks: **CLI** (terminal I/O and command parsing), **Runner** (executes test cases), **Heartbeat** (blinks a status LED), **Watchdog** (refreshes IWDG and supervises the Runner). | S | I |
| FRT-011 | The CLI shall stay responsive while a test is running. The `abort` command shall stop the running test within 500 ms, unless the test is inside a critical section that cannot be interrupted, in which case it stops when that section ends. | S | T |
| FRT-012 | The Heartbeat task shall blink LD1 (green) at 1 Hz when idle, fast (5 Hz) while a test runs, and switch to LD3 (red) after any test fails, until results are cleared. A test that owns the LEDs suspends the heartbeat for its duration. | S, O | D |
| FRT-013 | The Watchdog task shall reset the MCU through IWDG when the Runner has not reported progress for longer than the running test's declared timeout plus 5 s. | S | T |

### 5.3 RTOS self-tests

| ID | Requirement | Level | Method |
|---|---|---|---|
| FRT-020 | HWVAL shall test task creation, deletion, suspension, resumption and priority changes. | S | T |
| FRT-021 | HWVAL shall test queues (send, receive, timeout, overflow), binary and counting semaphores, mutexes (including priority inheritance), event groups, task notifications, stream buffers and message buffers. | S | T |
| FRT-022 | HWVAL shall test software timers (one-shot and auto-reload) for period accuracy within ±1 tick. | S | T |
| FRT-023 | HWVAL shall measure the tick rate against an independent hardware timer and pass if it is within ±0.1 %. | S | T |
| FRT-024 | HWVAL shall measure and report context-switch time and interrupt latency in CPU cycles. | S | T |
| FRT-025 | HWVAL shall report per-task stack high-water marks, CPU usage and free heap with the `tasks` command. | S | T |
| FRT-026 | HWVAL shall test that the stack overflow hook and malloc-failed hook trigger, using a dedicated test task that deliberately overflows and an oversize allocation. These are marked destructive because they reset the MCU. | S | T |

---

## 6. Terminal (CLI) requirements

### 6.1 Serial link

| ID | Requirement | Level | Method |
|---|---|---|---|
| CLI-001 | The CLI shall run on USART3 (PD8 TX, PD9 RX) at 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control by default. | S | T |
| CLI-002 | Reception shall be interrupt- or DMA-driven with a ring buffer of at least 512 bytes. No character shall be lost at 115200 baud when a full line is pasted. | S | T |
| CLI-003 | Transmission shall be DMA- or interrupt-driven and shall not block test execution. Output from several tasks shall not interleave within a line. | S | T |
| CLI-004 | The `baud <rate>` command shall change the VCP baud rate (supported: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600) after printing a confirmation, and revert to 115200 if no character is received within 10 s at the new rate. | S | T |
| CLI-005 | The CLI shall accept both `\r` and `\r\n` line endings and send `\r\n`. | S | T |

### 6.2 Line editing and interaction

| ID | Requirement | Level | Method |
|---|---|---|---|
| CLI-010 | The CLI shall show a prompt `hwval> ` and support backspace, Ctrl-C (abort current command or test), Ctrl-U (clear line), and up/down arrow command history of at least 10 entries. | S | T |
| CLI-011 | The CLI shall support Tab completion of command names and test IDs. | S | D |
| CLI-012 | The CLI shall echo typed characters in interactive mode and shall have a `mode script` setting that turns echo, colour and the prompt off for host scripts. | S | T |
| CLI-013 | ANSI colour (green PASS, red FAIL, yellow SKIP) shall be on in interactive mode and selectable with `color on/off`. | S | D |
| CLI-014 | Operator prompts shall state the action or observation, list valid answers (e.g. `[y/n]`, `[press B1]`), and time out after a configurable period (default 60 s) with result SKIP `NO_OPERATOR`. | O | T |

### 6.3 Commands

| ID | Command | Requirement | Method |
|---|---|---|---|
| CLI-020 | `help [cmd]` | List all commands with one-line help, or detailed help for one. | T |
| CLI-021 | `version` | Print firmware version, build info, FreeRTOS and CubeF7 versions. | T |
| CLI-022 | `info` | Print board and chip identity: DEV_ID, REV_ID, CPUID, flash size, package, 96-bit unique ID, option bytes, clock tree frequencies, reset cause, uptime. | T |
| CLI-023 | `list [group\|level]` | List test cases with ID, level, operator flag, destructive flag, timeout and one-line description. | T |
| CLI-024 | `run <id\|group\|all> [...]` | Run one or more test cases. `all` runs every test allowed by the current filters. | T |
| CLI-025 | `run ... --level S,L,E` / `--no-operator` / `--no-destructive` | Filters for automated runs. Defaults: all levels, operator allowed, destructive not allowed. | T |
| CLI-026 | `run ... --repeat N` / `--until-fail` | Repeat for soak testing. | T |
| CLI-027 | `abort` | Stop the running test and restore state (FRT-011, GEN-005). | T |
| CLI-028 | `results [clear]` | Show or clear the results table of the current session. | T |
| CLI-029 | `report` | Print the summary report (§7.3). | T |
| CLI-030 | `harness` | Detect and list fitted harness wires (ENV-002). | T |
| CLI-031 | `tasks` | FreeRTOS task list and run-time stats (FRT-025). | T |
| CLI-032 | `log <level>` | Set log level: `error`, `warn`, `info`, `debug`. | T |
| CLI-033 | `reset [hard]` | Software reset (NVIC_SystemReset). `hard` uses IWDG. | T |
| CLI-034 | `mode interactive\|script\|link` | See CLI-012; `link` see §6.4. | T |
| CLI-035 | `format text\|json` | Result output format (§7.2). | T |
| CLI-036 | `set <key> <value>` / `get <key>` | Runtime settings: operator timeout, fault action, test parameters. | T |
| CLI-037 | `peek <addr> [n]` / `poke <addr> <value>` | Read/write memory or registers for debugging. `poke` shall refuse addresses in flash, OTP and option bytes, and shall require `--force` for any peripheral address. | T |
| CLI-038 | `gpio <pin> [in\|out\|high\|low\|pullup\|pulldown\|read]` | Manual pin control for fault finding. Refuses the pins of CON-005. | T |
| CLI-039 | `clock` | Print SYSCLK, HCLK, PCLK1/2, timer clocks, PLL, PLLI2S, PLLSAI, MCO settings. | T |
| CLI-040 | Unknown command | Print `ERR unknown command` and a hint to `help`, without changing state. | T |

### 6.4 Link mode (HWLINK)

Link mode lets the HWVAL Studio GUI [GUI] drive HWVAL with the binary HWLINK
protocol [LINK] on the same USART3. The text CLI stays for people at a
terminal and for `hwtest.py`. These requirements implement the GUI change
requests CR-01…CR-08 ([GUI] Appendix B).

| ID | Requirement | Level | Method |
|---|---|---|---|
| LINK-001 | `mode link` shall switch USART3 from the text CLI to HWLINK framing [LINK A.2] after printing `OK link`. The HWLINK message `EnterTextMode` shall switch back to the text CLI (interactive mode). (CR-01) | S | T |
| LINK-002 | While in link mode HWVAL shall send no text on USART3; log output shall be sent as HWLINK `Log` events. (CR-01) | S | T |
| LINK-003 | HWVAL shall implement the common HWLINK messages and the HWVAL message set of [LINK A.3]: `Hello`, `Ping`, `SetBaud`, `Reset`, `GetDeviceInfo`, `ListGroups`, `ListTests`, `GetHarness`, `Run`, `OperatorAnswer`, `Abort`, `GetResults`, `ClearResults`, `GetReport`, `GetSetting`, `SetSetting`, `EnterTextMode`, with the events `RunStarted`, `TestStarted`, `TestResult`, `OperatorPrompt`, `RunFinished`, `Progress`, `Log`. (CR-02) | S | T |
| LINK-004 | `ListGroups`/`ListTests` shall be generated from the single test table (TFW-002), so that the GUI sidebar and catalogue always match the firmware. | S | T, I |
| LINK-005 | `Run` options shall map one-to-one to CLI-024…CLI-026 (selection, levels, operator, destructive, repeat, until-fail), and `Abort` to CLI-027 / FRT-011. | S | T |
| LINK-006 | Every field of a text/JSON result line (TFW-010, TFW-011) shall be present in `TestResult`, plus structured values (TFW-013). (CR-02, CR-03) | S | T |
| LINK-007 | `HelloReply` shall report role `HWVAL`, firmware name/version/commit/build time, chip UID, DEV_ID, REV_ID, flash mode `SINGLE_BANK` and the HWLINK protocol version. (CR-06) | S | T |
| LINK-008 | Operator prompts (CLI-014), equipment confirmations (ENV-003, SAFE-009) and destructive confirmations (SAFE-005) shall be sent as `OperatorPrompt` events with kind, text, valid answers and timeout, and answered with `OperatorAnswer`; the timeout behaviour of CLI-014 is unchanged. (CR-05) | O | T |
| LINK-009 | For "press B1" prompts, HWVAL shall complete the prompt itself when the press is detected and report it, so that the GUI can close its dialog. (CR-07) | O | T |
| LINK-010 | When link mode was active before a reset caused by a test (TFW-007) or by the watchdog, HWVAL shall boot straight into link mode (flag in a backup register), and make the preserved results (TFW-022) available through `GetResults`; a run that was interrupted shall resume with the remaining tests and end with `RunFinished`. (CR-04) | S | T |
| LINK-011 | Within link mode, HWVAL shall answer every request within 50 ms (NFR-001), handle a repeated sequence number by resending its last response [LINK A.4], and verify every received FlatBuffer before use [LINK A.2]. | S | T |
| LINK-012 | The FlatBuffers code shall be generated with flatcc from the shared schema `protocol/hwlink.fbs` at build time; the flatcc runtime shall be used as released, in project-owned build files (CON-002 applies to it like to the SDK). | — | I |

---

## 7. Test framework requirements

### 7.1 Test cases

| ID | Requirement | Method |
|---|---|---|
| TFW-001 | Each test case shall have: ID (Appendix C), group, title, level, operator flag, destructive flag, timeout, requirement IDs it verifies, and a function that returns PASS, FAIL, SKIP or N/A with a reason. | I |
| TFW-002 | Test cases shall be registered in a single table so that `list` shows all of them and the coverage matrix (§12) can be checked by script. | I |
| TFW-003 | Each test case shall set up the hardware it needs, check preconditions (harness, equipment, pin ownership), run, and release the hardware, including on abort and timeout. | T |
| TFW-004 | A FAIL result shall include the measured value, the expected value or limits, and the register or step where the failure was detected. | T |
| TFW-005 | A test case shall never depend on the result of another test case having run first. | I |
| TFW-006 | `run all` shall run tests in a fixed order: non-destructive Level S, then L, then E, then destructive tests, then tests that reset the MCU last. | T |
| TFW-007 | Tests that reset the MCU (watchdog, low-power standby, fault tests) shall store their expected outcome in a backup register before the reset, and the boot code shall evaluate and report the result after the reset. | T |
| TFW-008 | Measured limits (frequencies, voltages, timings) shall be defined as named constants with their source (datasheet table, calculation). | I |

### 7.2 Result output

| ID | Requirement | Method |
|---|---|---|
| TFW-010 | Each test shall print one start line and one result line. In text format: `RESULT <id> <PASS\|FAIL\|SKIP\|N/A> <duration_ms> <reason or detail>`. | T |
| TFW-011 | In JSON format, each result shall be a single-line JSON object with fields `id`, `group`, `result`, `duration_ms`, `reason`, `measured`, `limits`, `reqs`. | T |
| TFW-012 | Result lines shall start at the beginning of a line and never be split by other output, so that a host script can match `^RESULT `. | T |
| TFW-013 | Where a measured value or a limit is numeric, the test case shall provide it as a number with a unit (`measured: {value, unit}`, `limits: {min, max, unit}`) in addition to the text form; the JSON format (TFW-011) and `TestResult` (LINK-006) shall carry both. (CR-03) | T |

### 7.3 Report

| ID | Requirement | Method |
|---|---|---|
| TFW-020 | `report` shall print: firmware version, chip unique ID, date/time from RTC if set, totals of PASS/FAIL/SKIP/N/A per group and overall, the list of FAIL results with reasons, and the list of SKIP results with reasons. | T |
| TFW-021 | The report shall end with `REPORT END <overall PASS\|FAIL>` where overall is FAIL if any test failed. | T |
| TFW-022 | Results shall survive a software or watchdog reset within a session by being kept in a RAM region not cleared at start-up (marked with a magic word and CRC), until `results clear` or power-off. | T |

---

## 8. Hardware test requirements

Each sub-section covers one hardware block. Every block in the coverage
matrix (§12) maps to at least one requirement here, or to an explicit N/A
requirement that states why it cannot be tested on this board.

### 8.1 Device identity (DEV)

| ID | Requirement | Level | Method |
|---|---|---|---|
| DEV-001 | Read `DBGMCU_IDCODE` and pass if DEV_ID = `0x451`. Report REV_ID and the revision letter (`0x1001` = Z). | S | T |
| DEV-002 | Read `SCB_CPUID` and pass if implementer = Arm (`0x41`), part number = Cortex-M7 (`0xC27`). Report variant and revision. | S | T |
| DEV-003 | Read the flash size register (`0x1FF0F442`) and pass if it reads 2048 KB. | S | T |
| DEV-004 | Read the package register (`0x1FF0F7E0`) and pass if it reports LQFP144. | S | T |
| DEV-005 | Read and print the 96-bit unique ID (`0x1FF0F420`), decoding wafer X/Y, wafer number and lot number. Pass if it is not all 0 and not all 1. | S | T |
| DEV-006 | Read and print the factory calibration values VREFINT_CAL (`0x1FF0F44A`), TS_CAL1 (`0x1FF0F44C`) and TS_CAL2 (`0x1FF0F44E`). Pass if they are within the plausible ranges of [DS11532]. | S | T |

### 8.2 Cortex-M7 core (CORE)

| ID | Requirement | Level | Method |
|---|---|---|---|
| CORE-001 | Execute an integer instruction test covering ALU, multiply/divide (UDIV, SDIV), saturating, DSP/SIMD (e.g. SMLAD, QADD8), bit-field and CLZ instructions against known results. | S | T |
| CORE-002 | Test the FPU in single precision and double precision (add, mul, div, sqrt, fused multiply-add, conversions) against known results, including denormals, infinities and NaN handling per the FPSCR settings. | S | T |
| CORE-003 | Test that a divide by zero raises a UsageFault when `DIV_0_TRP` is set and that the handler recovers. | S | T |
| CORE-004 | Test that an unaligned access raises a UsageFault when `UNALIGN_TRP` is set, and that unaligned access works when it is clear. | S | T |
| CORE-005 | Test that an access to an unmapped address raises a BusFault, with BFAR reporting the address, and that the test recovers. | S | T |
| CORE-006 | Test that executing an undefined instruction raises a UsageFault (UNDEFINSTR). | S | T |
| CORE-007 | Test the SVCall and PendSV exceptions and that the NMI handler can be triggered by setting NMIPENDSET. | S | T |
| CORE-008 | Test SysTick: reload, count, interrupt rate against an independent timer within ±0.1 %. | S | T |
| CORE-009 | Run a fixed benchmark (integer and floating point) with caches off/on, from flash via AXIM, from flash via ITCM, and from ITCM RAM, and report cycles. Pass if the ordering matches the expected (cached/ITCM faster than uncached flash). | S | T, A |
| CORE-010 | Test the WFI and WFE instructions with SEV and an interrupt wake-up. | S | T |
| CORE-011 | Test exclusive access instructions (LDREX/STREX, CLREX) for atomic increment under interrupts. | S | T |
| CORE-012 | Test the DMB, DSB and ISB barriers by running self-modifying code in RAM (write, clean D-cache, invalidate I-cache, ISB, execute). | S | T |

### 8.3 Caches, TCM and MPU (CACHE, MPU)

| ID | Requirement | Level | Method |
|---|---|---|---|
| CACHE-001 | Test I-cache enable, disable and invalidate. Measure a loop's cycle count with I-cache on and off. | S | T |
| CACHE-002 | Test D-cache enable, disable, clean, invalidate and clean+invalidate by address and for the whole cache. Demonstrate stale data when DMA writes to a cacheable buffer without maintenance, and correct data after invalidate. | S | T |
| CACHE-003 | Test that write-through and write-back attributes (set by the MPU) behave as expected with DMA reading a CPU-written buffer. | S | T |
| CACHE-004 | Test that ITCM RAM and DTCM RAM are accessible by the CPU with zero wait state (cycle-measured), and that DTCM is reachable by DMA. | S | T |
| MPU-001 | Read `MPU_TYPE` and report the number of regions. | S | T |
| MPU-002 | Test that each MPU region can be programmed, that a read-only region raises a MemManage fault on write, that an execute-never region raises a fault on execution, and that a no-access region faults on read. | S | T |
| MPU-003 | Test sub-region disable and the background region (PRIVDEFENA). | S | T |
| MPU-004 | Test that an unprivileged task access to a privileged-only region faults. | S | T |

### 8.4 Internal RAM (RAM)

| ID | Requirement | Level | Method |
|---|---|---|---|
| RAM-001 | Run a March C− test on all of **ITCM RAM** (16 KB) not used by HWVAL at the time of test. | S | T |
| RAM-002 | Run a March C− test on all of **DTCM RAM** (128 KB) not used by HWVAL at the time of test. | S | T |
| RAM-003 | Run a March C− test on all of **SRAM1** (368 KB) not used by HWVAL at the time of test. | S | T |
| RAM-004 | Run a March C− test on all of **SRAM2** (16 KB) not used by HWVAL at the time of test. | S | T |
| RAM-005 | The RAM regions used by HWVAL itself shall be tested with a non-destructive (save, test, restore) word-by-word March test with interrupts disabled, in blocks small enough to keep the CLI responsive between blocks. Together RAM-001…RAM-005 shall cover 100 % of the 512 KB + 16 KB. | S | T |
| RAM-006 | Test data bus (walking 1s/0s) and address bus (power-of-two offsets) for each RAM region. | S | T |
| RAM-007 | Test 8-, 16-, 32- and 64-bit accesses to each RAM region. | S | T |
| RAM-008 | Run a March C− test on the 4 KB **backup SRAM** with the backup regulator off. This test is destructive for backup SRAM content. | S | T |
| RAM-009 | Test that backup SRAM content is retained through a system reset and, with the backup regulator on, through Standby mode. | S, O | T |

### 8.5 Flash, ART and option bytes (FLS, OB, OTP)

| ID | Requirement | Level | Method |
|---|---|---|---|
| FLS-001 | Compute a CRC32 of the HWVAL image in flash with the CRC peripheral and compare with the value stored at build time. | S | T |
| FLS-002 | Read all 2 MB of flash through the AXIM path and the ITCM path and check that both give identical data. | S | T |
| FLS-003 | Test the flash wait-state setting: for each `LATENCY` value and HCLK combination in [RM0410] table, confirm correct reads at the minimum allowed latency. | S | T |
| FLS-004 | Test ART accelerator and prefetch on/off and report the cycle difference for a flash-resident loop. | S | T |
| FLS-005 | **Destructive:** erase sector 11, check it reads all `0xFF`, program it with patterns (`0x00`, `0x55`, `0xAA`, address-in-word) in 8/16/32/64-bit parallelism (PSIZE) according to the supply voltage, verify, then erase again. | S | T |
| FLS-006 | **Destructive:** test that programming a non-erased location is refused (PGSERR/PGPERR flags set) and that the error flags can be cleared. | S | T |
| FLS-007 | Test the flash unlock/lock sequence, and that a wrong key locks the flash interface until reset (reported, then reset). | S | T |
| FLS-008 | Test flash interrupts (end-of-operation and error) during FLS-005. | S | T |
| FLS-009 | Measure sector erase and word program times and pass if within [DS11532] limits. | S | T, A |
| OB-001 | Read and decode all option bytes (FLASH_OPTCR, FLASH_OPTCR1): RDP, nWRP, BOR level, user bits, nDBANK, nDBOOT, BOOT_ADD0/1. Pass if they match ASM-003. | S | T |
| OB-002 | Option-byte **writes** shall not be tested (see SAFE-001). The test shall report N/A with reason `PROTECTED_BY_POLICY`. | — | I |
| OTP-001 | Read all OTP blocks and lock bytes (`0x1FF0F000`–`0x1FF0F41F`) and report which blocks are programmed or locked. | S | T |
| OTP-002 | OTP **writes** shall not be tested (see SAFE-002). The test shall report N/A with reason `IRREVERSIBLE`. | — | I |

### 8.6 Clocks (CLK)

| ID | Requirement | Level | Method |
|---|---|---|---|
| CLK-001 | Test HSI 16 MHz: enable, ready, measure against LSE using a timer input capture. Pass within ±1 % (at room temperature). | S | T |
| CLK-002 | Test HSE in bypass mode from the ST-LINK MCO: enable, ready, measure against LSE. Pass at 8 MHz within ±0.1 %. | S | T |
| CLK-003 | Test HSE crystal mode: N/A on this board (crystal X3 not fitted by default). Report N/A `NOT_FITTED`, unless a setting declares the crystal fitted, in which case run as CLK-002. | S | T |
| CLK-004 | Test LSE 32.768 kHz: enable, ready, measure against HSE with TIM5 channel 4 input capture. Pass within ±100 ppm. Test each LSE drive level. | S | T |
| CLK-005 | Test LSI: enable, ready, measure against HSE with TIM5 channel 4. Pass within the [DS11532] range (about 17–47 kHz). | S | T |
| CLK-006 | Test the main PLL with HSI and with HSE as source for at least 3 frequency settings, including 216 MHz, and measure SYSCLK against LSE. | S | T |
| CLK-007 | Test PLLQ = 48 MHz output (for USB/RNG/SDMMC) and the 48 MHz clock mux (PLLQ or PLLSAIP). | S | T |
| CLK-008 | Test PLLI2S: lock and measure its output through MCO2 or a dependent peripheral (I2S/SAI clock). | S | T |
| CLK-009 | Test PLLSAI: lock and measure its outputs (SAI clock, LCD-TFT clock, 48 MHz). | S | T |
| CLK-010 | Test MCO1 (PA8) with HSI, LSE, HSE and PLL sources and all prescalers, and MCO2 (PC9) with SYSCLK, PLLI2S, HSE and PLL sources. With harness wire MCO→timer input, measure each frequency. Without harness, run as Level E with a scope or frequency counter. | L / E | T |
| CLK-011 | Test the Clock Security System on HSE: enable CSS and check the CSSON bit, and test the NMI handler path by pending the NMI in software (CORE-007). An actual HSE failure cannot be induced, because the HSE comes from the ST-LINK MCO, which firmware cannot stop; the induced-failure part shall report N/A `NOT_INDUCIBLE`. | S | I, T |
| CLK-012 | Test the LSE Clock Security System (LSECSS) configuration and interrupt path. **N/A on the STM32F767: RM0410 (RCC_BDCR) has no LSECSSON/LSECSSD bits — the LSE CSS does not exist on this device (F76x only on F730/750). The test reports N/A with the register-map evidence (CL-016).** | S | T |
| CLK-013 | Test AHB, APB1 and APB2 prescalers by measuring a timer clocked from each bus against LSE, including the timer clock ×2 rule when APB prescaler ≠ 1. | S | T |
| CLK-014 | Test peripheral clock enable/disable and reset bits in RCC for every peripheral listed in §12: after reset, the peripheral's registers read their reset values. | S | T |
| CLK-015 | Test the clock-ready and PLL-lock interrupts in RCC_CIR. | S | T |

### 8.7 Power (PWR)

| ID | Requirement | Level | Method |
|---|---|---|---|
| PWR-001 | Measure VDDA (≈ VDD) with the ADC using VREFINT and VREFINT_CAL. Pass if 3.3 V ± 5 %. (Measured at bring-up: 3.265 V.) | S | T |
| PWR-002 | Measure VBAT with the ADC VBAT channel (÷4 bridge). On this board VBAT is tied to VDD; pass if equal to PWR-001 within ±5 %. | S | T |
| PWR-003 | Test the Programmable Voltage Detector at each PVD level: configure the level above and below the measured VDD and check the PVDO flag and EXTI line 16 interrupt. | S | T |
| PWR-004 | Test voltage scaling (scale 1, 2, 3) and over-drive / over-drive switching enable and ready flags at the corresponding maximum frequencies. | S | T |
| PWR-005 | Test **Sleep** mode: enter with WFI, wake on a timer interrupt and on EXTI from B1. Check time in sleep against the timer. | S, O | T |
| PWR-006 | Test **Stop** mode (main regulator, low-power regulator, under-drive, flash power-down options): enter and wake on RTC wake-up timer and EXTI line. Check that clocks are restored afterwards. | S | T |
| PWR-007 | Test **Standby** mode: enter, wake on RTC alarm, RTC wake-up timer, and wake-up pin (PC13/B1). Check the SBF and WUF flags after reset and report the wake-up source (via TFW-007). | S, O | T |
| PWR-008 | Test wake-up pins WKUP1–WKUP4 (PA0, PA2, PC1, PC13) with rising and falling edge polarity: PC13 via B1 (operator), the others via harness wire from a GPIO. WKUP5/WKUP6 (PI8, PI11) are not bonded on LQFP144: N/A `NOT_BONDED`. | L, O | T |
| PWR-009 | For each power mode (Run at 216/168/96/16 MHz, Sleep, Stop variants, Standby), hold the mode for an operator-set time and display the expected current range from [DS11532], so the operator can measure on the IDD jumper (JP6) with an ammeter and enter the reading. Pass if within range. | E, O | T, A |
| PWR-010 | Test the backup domain: write-protect (DBP bit), backup domain reset (BDRST) clearing RTC and backup registers, and backup regulator enable/ready. | S | T |
| PWR-011 | Power input sources (U5V, VIN, E5V, CHGR via jumper JP5): the operator powers the board from each source in turn; HWVAL reports VDD (PWR-001) and the reset cause, and asks the operator to confirm the power LED. | E, O | D |
| PWR-012 | Board power LED and 5 V presence: operator confirmation. | S, O | D |

### 8.8 Reset (RST)

| ID | Requirement | Level | Method |
|---|---|---|---|
| RST-001 | Report and clear the reset flags on boot: low-power, window watchdog, independent watchdog, software, power-on/power-down, pin, brown-out. | S | T |
| RST-002 | Test software reset (`NVIC_SystemReset`): after reset, SFTRSTF set. | S | T |
| RST-003 | Test the reset button B2: operator presses it; after reset, PINRSTF set and no other cause. | S, O | T |
| RST-004 | Test power-on reset: operator unplugs and replugs USB; after boot, PORRSTF set. | S, O | T |
| RST-005 | Test low-power reset: with option bit nRST_STOP/nRST_STDBY = 1 (no reset) only the absence of LPWRRSTF after Stop/Standby is tested. Setting those bits is excluded (SAFE-001). | S | T |
| RST-006 | Brown-out reset: BOR is off on this board (option bytes). Report the configured BOR level and N/A for an induced BOR test unless a bench supply is used (Level E: lower VDD below threshold through E5V/3V3 with a programmable supply, operator-driven). | E, O | T |
| RST-007 | Test the reset of every peripheral through its RCC reset bit (shared with CLK-014). | S | T |

### 8.9 Watchdogs (WDG)

| ID | Requirement | Level | Method |
|---|---|---|---|
| WDG-001 | Test IWDG: prescaler and reload settings for at least 3 timeouts. Stop refreshing and measure time to reset with the RTC or a backup-domain counter. Pass within the LSI tolerance. | S | T |
| WDG-002 | Test the IWDG window option: refreshing before the window opens causes a reset. | S | T |
| WDG-003 | Test WWDG: early wake-up interrupt (EWI) fires; refreshing outside the window causes a reset; letting the counter reach 0x3F causes a reset. | S | T |
| WDG-004 | Test the DBGMCU freeze bits for IWDG and WWDG: with a debugger halt the watchdog stops (Level E: requires OpenOCD on the host). | E | T |

### 8.10 RTC and backup registers (RTC)

| ID | Requirement | Level | Method |
|---|---|---|---|
| RTC-001 | Initialise the RTC from LSE, set date and time, and check that it advances by 10 s ± 1 ms against the HSE-based timer. | S | T |
| RTC-002 | Test the RTC from LSI and from HSE (with prescaler) as clock sources. | S | T |
| RTC-003 | Test calendar roll-over: end of minute, hour, day, month, year, and 29 February in a leap year. | S | T |
| RTC-004 | Test 12 h and 24 h formats and BCD/binary register access. | S | T |
| RTC-005 | Test Alarm A and Alarm B with each mask combination (seconds, minutes, hours, date) and the sub-second alarm. | S | T |
| RTC-006 | Test the wake-up timer at each clock selection and its interrupt (EXTI line 22). | S | T |
| RTC-007 | Test smooth digital calibration (CALP/CALM) and coarse offset; measure the change in the RTC 1 Hz or 512 Hz output on RTC_OUT (PC13 — shared with B1, so only via the calibration output frequency measured internally or by harness/scope). | L / E | T |
| RTC-008 | Test the time-stamp function on PC13, triggered by a B1 press, and the time-stamp on tamper event. The alternative time-stamp pin PI8 is not bonded on LQFP144: N/A `NOT_BONDED`. | S, O | T |
| RTC-009 | Test the tamper inputs on PC13 (RTC_TAMP1 and RTC_TAMP3 per [DS11532]): edge and level detection, filter, precharge, and erase of backup registers on tamper, using B1 (operator). RTC_TAMP2 is on PI8, not bonded on LQFP144: N/A `NOT_BONDED`. | S, O | T |
| RTC-010 | Test all 32 backup registers: write patterns, read back, survive system reset, cleared by backup-domain reset and by tamper event. | S | T |
| RTC-011 | Test RTC shift (sub-second adjust) and reference clock detection (RTC_REFIN) — reference input requires a 50/60 Hz source (Level E) or is reported N/A. | E | T |

### 8.11 Interrupts, EXTI and SYSCFG (INT)

| ID | Requirement | Level | Method |
|---|---|---|---|
| INT-001 | Trigger every NVIC interrupt line of the STM32F767 vector table (110 external IRQs — the CMSIS device header lists 16 system + 110 external entries; CL-017) by software pending (`NVIC_SetPendingIRQ`) and check its handler runs. | S | T |
| INT-002 | Test interrupt priority grouping, pre-emption and tail-chaining with 3 nested interrupts. | S | T |
| INT-003 | Test BASEPRI and PRIMASK masking, and FAULTMASK. | S | T |
| INT-004 | Test all 16 GPIO EXTI lines (0–15) with rising, falling and both edges, using harness wires or software-triggered events (SWIER) where no wire is fitted. | S, L | T |
| INT-005 | Test the internal EXTI lines: 16 (PVD), 17 (RTC alarm), 18 (USB OTG FS wake-up), 19 (Ethernet wake-up), 20 (USB OTG HS wake-up), 21 (RTC tamper/time-stamp), 22 (RTC wake-up), 23 (LPTIM1) and 24 (MDIO slave), as applicable, through their source events. | S | T |
| INT-006 | Test the EXTI event mode (WFE wake-up without interrupt). | S | T |
| INT-007 | Test SYSCFG: EXTI port mapping for all ports, memory remap (MEMRMP: boot alias, FMC swap), the compensation cell (enable, ready), the Ethernet MII/RMII select, I2C fast-mode-plus drive bits, and the class B lock bits. | S | T |
| INT-008 | Measure interrupt latency from EXTI edge (harness GPIO-to-GPIO) to handler entry in cycles. | L | T, A |

### 8.12 DMA (DMA)

| ID | Requirement | Level | Method |
|---|---|---|---|
| DMA-001 | Test memory-to-memory transfer on every stream of DMA2 (8 streams) with 8/16/32-bit data, increment modes, FIFO thresholds and bursts (single, INCR4/8/16). DMA1 does not support memory-to-memory; test each DMA1 stream with a peripheral request in DMA-003. | S | T |
| DMA-002 | Test transfer-complete, half-transfer, transfer-error, FIFO-error and direct-mode-error interrupts. | S | T |
| DMA-003 | Test every DMA1 and DMA2 stream with at least one peripheral request mapped to it (see request tables in [RM0410]); the peripheral tests in §8 shall together cover every stream and every channel selection used by HWVAL. | S, L | T |
| DMA-004 | Test circular mode and double-buffer mode. | S | T |
| DMA-005 | Test DMA access to each memory: DTCM, SRAM1, SRAM2, flash (read), backup SRAM. ITCM RAM is not DMA-accessible: test that a DMA to ITCM produces a transfer error. | S | T |
| DMA-006 | Measure memory-to-memory throughput for DTCM→SRAM1 and SRAM1→SRAM1 and report MB/s. | S | T, A |
| DMA-007 | Test DMA priority arbitration with two simultaneous streams of different priority. | S | T |

### 8.13 GPIO (GPIO)

| ID | Requirement | Level | Method |
|---|---|---|---|
| GPIO-001 | HWVAL shall contain a pin table of all 114 I/O of the LQFP144 package (ports A–G fully, PH0/PH1), with for each pin: header position (Zio/morpho), on-board connection, owner test, and whether it may be driven. | — | I |
| GPIO-002 | **Pull test:** for every pin that may be driven, configure input with pull-up and check it reads 1, then pull-down and check it reads 0. Detects pins shorted to VDD, GND or a strong on-board driver. | S | T |
| GPIO-003 | **Short test:** for every drivable pin, drive it high then low while all other drivable pins are inputs with the opposite pull, and check no other pin follows. Detects shorts between pins (including neighbours on the headers). | S | T |
| GPIO-004 | **Loopback test:** for every harness pair (Appendix A), drive each pin in turn and read the other, both levels, in push-pull and open-drain with pull-up. | L | T |
| GPIO-005 | Test the output speed settings (low, medium, high, very high) by measuring rise time from GPIO to GPIO through a harness wire with a timer input capture, or with a scope at Level E. | L / E | T |
| GPIO-006 | Test the BSRR set/reset, ODR, IDR registers and the LCKR lock sequence on each port. | S | T |
| GPIO-007 | Test alternate-function selection AF0–AF15 by reading back AFRL/AFRH for every pin. | S | T |
| GPIO-008 | Test analog mode (lowest leakage) on every analog-capable pin, as part of ADC tests. | S | T |
| GPIO-009 | Ports I, J and K are not bonded on LQFP144. Report N/A `NOT_BONDED` after checking that their clocks can be enabled and registers are accessible. | S | T |
| GPIO-010 | Test 5 V tolerance of FT pins: Level E only, with a 5 V source through a resistor, operator-driven. Shall be off by default. | E, O | T |

### 8.14 On-board LEDs and buttons (LED, BTN)

| ID | Requirement | Level | Method |
|---|---|---|---|
| LED-001 | Light LD1 (green, PB0), LD2 (blue, PB7) and LD3 (red, PB14) one at a time; the operator confirms each colour and position. | S, O | T |
| LED-002 | Drive each LED pin with PWM at 0, 25, 50, 75, 100 % via its timer channel (PB0 TIM3_CH3, PB7 TIM4_CH2, PB14 TIM12_CH1); the operator confirms the brightness steps. | S, O | T |
| LED-003 | Read back each LED pin's IDR while driving it to check it is not shorted. | S | T |
| LED-004 | Board-driven LEDs: ST-LINK COM LED (blinks during VCP/SWD traffic), power LED, USB OTG VBUS and over-current LEDs, Ethernet link/activity LEDs on the RJ45. Each is checked by operator observation in the test of its circuit (CLI-001, PWR-012, USB-020, USB-022, ETH-005). | S, O | D |
| BTN-001 | Read B1 (PC13, active high) idle = 0; ask the operator to press and hold, read 1; release, read 0. | S, O | T |
| BTN-002 | Test B1 as EXTI line 13 interrupt on press and release, with software debounce, counting 5 presses. | S, O | T |
| BTN-003 | B2 (reset) is covered by RST-003. | S, O | T |

### 8.15 ADC (ADC)

| ID | Requirement | Level | Method |
|---|---|---|---|
| ADC-001 | Calibrate (where supported) and test ADC1, ADC2 and ADC3 independently: resolution 12, 10, 8, 6 bits; right/left alignment; every sampling time. | S | T |
| ADC-002 | Convert the internal channels: VREFINT (pass vs VREFINT_CAL ±2 %), temperature sensor (report °C from TS_CAL1/TS_CAL2; pass if 0–60 °C on the bench), VBAT (PWR-002). | S | T |
| ADC-003 | Convert every external ADC channel that is on a drivable pin, with the pin driven by GPIO high (expect full scale) and low (expect 0) through internal pulls or harness. | S, L | T |
| ADC-004 | **DAC→ADC loopback:** with harness wire DAC_OUT1 (PA4) → an ADC input, and DAC_OUT2 (PA5) → another ADC input, sweep 0–4095 in 16 steps and check linearity: INL and DNL within [DS11532] limits plus DAC error. | L | T, A |
| ADC-005 | Test regular sequences (up to 16 ranks), injected sequences (4 ranks), scan, continuous, discontinuous, and single modes. | S, L | T |
| ADC-006 | Test external triggers from timers (TIM1, TIM2, TIM3, TIM4, TIM5, TIM8 TRGO/CC events) and from EXTI 11/15. | S | T |
| ADC-007 | Test the analog watchdog (upper/lower thresholds, single/all channels) and overrun detection. | S, L | T |
| ADC-008 | Test DMA transfer of conversions (single and circular). | S | T |
| ADC-009 | Test multi-ADC modes: dual and triple regular simultaneous, interleaved (to reach 7.2 MSPS), injected simultaneous, alternate trigger. | S, L | T |
| ADC-010 | Measure noise: 1024 samples of VREFINT, report standard deviation in LSB; pass if below a defined limit. | S | T, A |
| ADC-011 | Measure the maximum sample rate per ADC with DMA and report MSPS. | S | T, A |
| ADC-012 | Test the ADC end-of-conversion, end-of-injected and overrun interrupts. | S | T |

### 8.16 DAC (DAC)

| ID | Requirement | Level | Method |
|---|---|---|---|
| DAC-001 | Test DAC channel 1 (PA4) and channel 2 (PA5) with 8-bit and 12-bit, left/right-aligned data, output buffer on and off; read back the output with the ADC (ADC-004). | L | T |
| DAC-002 | Test dual-channel simultaneous update registers. | L | T |
| DAC-003 | Test DAC triggers: software, TIM2/4/5/6/7/8 TRGO, EXTI 9. | L | T |
| DAC-004 | Test noise and triangle wave generation; check with ADC that the output varies within the expected amplitude. | L | T |
| DAC-005 | Test DAC DMA (sine table, circular) at a set update rate, and the DMA underrun flag and interrupt. | L | T |
| DAC-006 | Without the harness, DAC tests shall run only the register and trigger checks (output not verified) and report partial result SKIP `NO_HARNESS` for output checks. | S | T |

### 8.17 Timers (TIM, LPTIM)

The F767 has TIM1–TIM14 and LPTIM1. Every timer shall be tested.

| ID | Requirement | Level | Method |
|---|---|---|---|
| TIM-001 | For each of TIM1–TIM14: test up-counting, down-counting and centre-aligned modes (where supported), prescaler, auto-reload with and without preload, update interrupt rate measured against an independent timer. | S | T |
| TIM-002 | Test 32-bit counting on TIM2 and TIM5 (count past 0xFFFF). | S | T |
| TIM-003 | Test every capture/compare channel of every timer (TIM1/8: 4 + complementary; TIM2–5: 4; TIM9/12: 2; TIM10/11/13/14: 1) in output compare (toggle, active, inactive, forced) and PWM modes 1 and 2, with the output looped by harness to an input-capture channel of another timer measuring frequency and duty. Channels without a harness wire are tested internally using the CCxIF flags only. | S, L | T |
| TIM-004 | Test input capture: rising, falling, both edges, input filter and prescaler; PWM input mode (period and duty on two channels). | L | T |
| TIM-005 | Test encoder interface mode (TIM1–5, TIM8) with two GPIOs generating quadrature signals through harness wires, counting up and down. | L | T |
| TIM-006 | Test one-pulse mode and the repetition counter (TIM1/TIM8). | S, L | T |
| TIM-007 | Test complementary outputs with dead-time insertion on TIM1 and TIM8, measuring dead time by input capture. | L | T, A |
| TIM-008 | Test the break inputs BRK and BRK2 on TIM1 and TIM8 (from GPIO via harness, and from the system break sources: lock-up, PVD) and that outputs go to the idle state. | L | T |
| TIM-009 | Test timer synchronisation: master/slave modes (reset, gated, trigger), internal trigger connections (ITRx) between timers, and timer chaining (one timer as prescaler of another). | S | T |
| TIM-010 | Test hall sensor interface (XOR of three inputs) on TIM1/TIM8 with harness wires. | L | T |
| TIM-011 | Test timer DMA requests (update, CCx, trigger) and the DMA burst mode (DCR/DMAR). | S | T |
| TIM-012 | Test TIM6 and TIM7 (basic timers) as time bases and as DAC triggers (DAC-003). | S | T |
| TIM-013 | Test the special remap inputs: TIM5_CH4 from LSI/LSE (CLK-004/005), TIM11_CH1 from HSE_RTC, TIM2_ITR1 from Ethernet PTP / USB SOF. | S | T |
| TIM-014 | Test the external clock modes 1 and 2 (ETR input) via harness. | L | T |
| LPTIM-001 | Test LPTIM1 with each clock source (APB, LSI, LSE, HSI, external input), compare match, auto-reload interrupts, PWM and one-shot outputs, encoder mode, and wake-up from Stop mode. | S, L | T |
| LPTIM-002 | Test the LPTIM1 timeout and trigger input modes. | L | T |

### 8.18 USART / UART (UART)

| ID | Requirement | Level | Method |
|---|---|---|---|
| UART-001 | For USART1, USART2, USART6, UART4, UART5, UART7 and UART8: with a TX→RX harness wire, send and receive a 1 KB pseudo-random block at 9600, 115200, 921600 baud and at the maximum baud of the instance; check data and error flags. | L | T |
| UART-002 | For each instance in UART-001, test 7/8/9 data bits, parity none/even/odd, 1/2 stop bits, oversampling by 8 and 16, MSB-first, data inversion, TX/RX pin swap (SWAP bit, with the harness wired both ways). | L | T |
| UART-003 | For each instance in UART-001 without a harness, use the half-duplex single-wire mode with internal loopback where available, or report SKIP `NO_HARNESS`. | S | T |
| UART-004 | USART3 (VCP) is tested through the CLI itself (CLI-001…CLI-005), including the baud-rate change. It shall not be loopback-tested. | S | T |
| UART-005 | Test hardware flow control (RTS/CTS) on at least USART2 and USART6 with harness wires RTS→CTS. | L | T |
| UART-006 | Test error detection: parity error (send with wrong parity from another UART), framing error (wrong baud), overrun (stop reading), noise flag, and break detection/transmission. | L | T |
| UART-007 | Test receiver timeout, character match (CMF) and idle line detection interrupts. | L | T |
| UART-008 | Test DMA TX and RX for each instance. | L | T |
| UART-009 | Test the special modes on at least one instance each: LIN (break 10/11 bits), IrDA SIR (normal and low-power), smartcard (T=0 with guard time, using two USARTs and harness), synchronous master mode (CK pin), RS-485 driver enable (DE pin timing), multiprocessor mute/wake-up, auto baud-rate detection (all four modes). | L | T |
| UART-010 | Test UART wake-up from Stop mode on start bit / address match (USART1/2/3/6, UART4/5/7/8 per [RM0410] capability). | L | T |

### 8.19 SPI and I2S (SPI, I2S)

| ID | Requirement | Level | Method |
|---|---|---|---|
| SPI-001 | For each of SPI1–SPI6 with a MOSI→MISO harness wire: full-duplex master transfer of 1 KB at each baud prescaler down to the fastest the pins allow, in all four CPOL/CPHA modes, with 4- to 16-bit data sizes, MSB/LSB first. | L | T |
| SPI-002 | Test master–slave between two SPI instances via harness (SCK, MOSI, MISO, NSS), in both directions, hardware NSS and NSS pulse mode. | L | T |
| SPI-003 | Test the CRC calculation (8/16-bit) with CRC error injection. | L | T |
| SPI-004 | Test TI frame mode, simplex (TX-only, RX-only), bidirectional half-duplex. | L | T |
| SPI-005 | Test DMA TX/RX and the FIFO thresholds (FRXTH). | L | T |
| SPI-006 | Test mode fault (MODF), overrun (OVR), frame format error (FRE) flags and interrupts. | L | T |
| SPI-007 | Without harness, each SPI instance shall run register checks and a transmit-only DMA transfer and report the receive part SKIP `NO_HARNESS`. | S | T |
| I2S-001 | For SPI1/I2S1, SPI2/I2S2 and SPI3/I2S3: test I2S master transmit to another I2S slave receive via harness (CK, WS, SD) in Philips, MSB, LSB and PCM short/long standards, 16/24/32-bit data, at 8, 16, 44.1, 48, 96 kHz using PLLI2S. | L | T |
| I2S-002 | Test the I2S master clock output (MCK) frequency by timer capture via harness. | L | T |
| I2S-003 | Full-duplex I2S: the STM32F7 SPI/I2S has no I2Sx_ext block, so full duplex is tested with two instances (I2S-001). Report N/A `NOT_PRESENT` for single-instance full duplex. | — | I |

### 8.20 SAI (SAI)

| ID | Requirement | Level | Method |
|---|---|---|---|
| SAI-001 | For SAI1 and SAI2: test block A as master transmitter and block B as synchronous slave receiver (internal synchronisation), in I2S, LSB/MSB-justified, PCM/DSP and TDM (up to 16 slots) protocols, 8–32 bit data. | S | T |
| SAI-002 | Test SAI1 ↔ SAI2 external synchronisation and an external harness loop (SD_A → SD_B) on at least one instance. | L | T |
| SAI-003 | Test AC'97 mode framing. | L | T |
| SAI-004 | Test SPDIF transmitter mode of SAI with output looped by harness to SPDIFRX (SPDIF-001). | L | T |
| SAI-005 | Test companding (µ-law, A-law), mute detection and the FIFO request/interrupts, and DMA. | S, L | T |
| SAI-006 | Test the master clock output (MCLK) frequency for each SAI clock source (PLLSAI, PLLI2S, external I2S_CKIN). | L | T |

### 8.21 SPDIF-RX (SPDIF)

| ID | Requirement | Level | Method |
|---|---|---|---|
| SPDIF-001 | Receive the SAI SPDIF stream (SAI-004) on each SPDIFRX input that is on a header pin (IN0–IN3). Check synchronisation, data, channel status and user bits, parity, and the sample rate measurement. | L | T |
| SPDIF-002 | Test SPDIFRX error flags (parity, overrun, synchronisation failure, time-out) by disconnecting the stream (harness unplugged on operator request or SAI stopped). | L | T |

### 8.22 I2C (I2C)

| ID | Requirement | Level | Method |
|---|---|---|---|
| I2C-001 | For I2C1–I2C4: test master–slave transfers between two I2C instances connected by harness wires (SCL–SCL, SDA–SDA) with the internal pull-ups or board pull-ups, at 10 kHz, 100 kHz (standard), 400 kHz (fast) and 1 MHz (fast-mode plus, with FM+ drive enabled). | L | T |
| I2C-002 | Test 7-bit and 10-bit addressing, dual addresses with masks, general call, clock stretching, NACK detection, repeated start, and the RELOAD/AUTOEND modes for >255 byte transfers. | L | T |
| I2C-003 | Test SMBus features: PEC generation/check, SMBus alert, timeouts (TIMEOUTA/B), host notification. | L | T |
| I2C-004 | Test error flags: bus error, arbitration loss (two masters), overrun/underrun, and the analog and digital noise filters. | L | T |
| I2C-005 | Test I2C wake-up from Stop mode on address match. | L | T |
| I2C-006 | Test DMA TX and RX. | L | T |
| I2C-007 | Bus scan of every I2C instance at Level S and report devices found (none expected on the bare board). With an external I2C module (Appendix B) the scan shall find its address. | S / E | T |
| I2C-008 | Test that each I2C pin pair can be recovered from a stuck bus (SDA held low) by clocking SCL. | L | T |

### 8.23 CAN (CAN)

| ID | Requirement | Level | Method |
|---|---|---|---|
| CAN-001 | For CAN1, CAN2 and CAN3: test silent loopback mode at 125 k, 250 k, 500 k and 1 Mbit/s with standard (11-bit) and extended (29-bit) IDs, data and remote frames, DLC 0–8. | S | T |
| CAN-002 | Test all 3 transmit mailboxes with priority by ID and by request order, and transmit abort. | S | T |
| CAN-003 | Test the acceptance filters: all 28 filter banks shared by CAN1/CAN2 (CAN2 start bank setting), 32-bit and 16-bit scale, mask and list modes, assignment to FIFO 0 and FIFO 1; CAN3's own 14 filter banks. | S | T |
| CAN-004 | Test FIFO overrun and FIFO lock mode, and all CAN interrupts (TX, RX0, RX1, SCE). | S | T |
| CAN-005 | Test error handling: error counters, error passive and bus-off (forced by a harness wiring with no ACK), automatic bus-off recovery (ABOM), and the last error code. | L | T |
| CAN-006 | Test time-triggered communication mode (TTCM) time stamps. | S | T |
| CAN-007 | Test sleep mode and wake-up on bus activity (AWUM). | L | T |
| CAN-008 | **External bus:** with two CAN transceiver modules (Appendix B) connecting CAN1 and CAN2 (and CAN3) on a terminated bus, test real bus transfers in normal mode at 500 kbit/s and 1 Mbit/s. | E | T |
| CAN-009 | Without transceivers, a direct TX→RX harness wire per instance with normal mode shall be supported as Level L (single-node with loopback mode off is not valid on CAN; use loopback mode with the TX pin observed by a GPIO input to check activity). | L | T |

### 8.24 SDMMC (SD)

| ID | Requirement | Level | Method |
|---|---|---|---|
| SD-001 | For SDMMC1 and SDMMC2 at Level S: test register access, clock divider, power control and the command path state machine (command timeout with no card). | S | T |
| SD-002 | With a microSD card adapter wired to SDMMC1 (Appendix B): card detect (if wired), initialisation (CMD0/8/55/41/2/3/9/7), read CID/CSD/SCR/SD status, 1-bit and 4-bit bus, default and high-speed clock. | E | T |
| SD-003 | On the card: write, read and verify a 1 MB test area in single-block and multi-block modes with DMA, and report throughput. The test area is a configurable LBA range; the test shall save and restore the original data unless the operator allows overwriting. | E | T |
| SD-004 | Test SDMMC error flags: CRC fail, timeout, FIFO underrun/overrun (by forcing DMA stalls). | E | T |
| SD-005 | Repeat SD-002/SD-003 on SDMMC2 when wired. | E | T |
| SD-006 | Test FatFs (from the SDK) on the card: mount, create, write, read, delete a file. | E | T |
| SD-007 | Test SD card over SPI (the Adafruit-shield path supported by the BSP, CS on PF14) as an alternative. | E | T |

### 8.25 Quad-SPI (QSPI)

| ID | Requirement | Level | Method |
|---|---|---|---|
| QSPI-001 | At Level S: test register access, clock prescaler, FIFO threshold, and the indirect mode state machine (timeout with no device), and the memory-mapped mode address space read without a device (expect a defined value or a timeout, not a hang). | S | T |
| QSPI-002 | With an external QSPI NOR flash module (Appendix B): read JEDEC ID, then indirect read/write/erase, quad I/O, DDR mode (if supported by the device), auto-polling mode, memory-mapped mode (execute a test function from QSPI), dual-flash mode if two devices fitted. | E | T |
| QSPI-003 | Measure memory-mapped read throughput. | E | T, A |

### 8.26 FMC (FMC)

| ID | Requirement | Level | Method |
|---|---|---|---|
| FMC-001 | At Level S: test register access and configuration of the NOR/SRAM, NAND and SDRAM controllers (timing registers, SDRAM command mode register, refresh rate). | S | T |
| FMC-002 | The board has no external memory. With an external SRAM or SDRAM board on the morpho headers (Appendix B), run the RAM tests of §8.4 on it. Otherwise report N/A `NOT_FITTED`. | E | T |

### 8.27 Ethernet (ETH)

| ID | Requirement | Level | Method |
|---|---|---|---|
| ETH-001 | Select RMII in SYSCFG, enable the MAC clocks, check the 50 MHz REF_CLK from the PHY is present (MAC DMA software reset completes). | S | T |
| ETH-002 | Read the LAN8742A over MDIO at PHY address 0: PHY ID registers 2/3 (OUI and model), basic control/status registers. Pass if the ID matches the LAN8742A. | S | T |
| ETH-003 | Test all PHY registers that are readable and the ones writable (control, auto-negotiation advertisement) with save and restore. | S | T |
| ETH-004 | Test MAC internal loopback (MACCR.LM): send 100 frames of sizes 64–1518 bytes, receive and compare; check checksum offload (IPv4 header, TCP/UDP/ICMP). | S | T |
| ETH-005 | Test PHY near-end loopback (BCR loopback bit) at 10 and 100 Mbit/s, full duplex, with the same frames as ETH-004. Operator confirms the RJ45 LEDs during link tests. | S | T |
| ETH-006 | With a network cable to a switch or host PC (Appendix B): auto-negotiation result (speed, duplex), link up/down detection when the operator unplugs and replugs the cable (PHY interrupt or polling). | E, O | T |
| ETH-007 | With the network: run LwIP (from the SDK) with DHCP (or static IP fallback), answer ARP and ICMP ping from the host, and run a TCP echo and UDP echo server. | E | T |
| ETH-008 | Measure TCP throughput (RX and TX) with the host (e.g. iperf-compatible or a custom echo) and report Mbit/s. | E | T, A |
| ETH-009 | Test MAC address filtering: unicast perfect filter, hash filter for multicast, promiscuous mode, broadcast reject. | E | T |
| ETH-010 | Test the IEEE 1588 PTP timer: start, fine and coarse update, target-time interrupt, time-stamping of TX and RX frames in loopback; PPS output on the header if wired (Level E with scope). | S / E | T |
| ETH-011 | Test the MMC (management counters) for TX/RX good/bad frames. | S | T |
| ETH-012 | Test the MAC power-management: magic packet and wake-up frame detection (with network), ETH wake-up EXTI line 19. | E | T |
| ETH-013 | Test the MAC DMA descriptor rings with cache maintenance (GEN-007) under load. | S, E | T |

### 8.28 USB OTG FS (USB)

| ID | Requirement | Level | Method |
|---|---|---|---|
| USB-001 | At Level S: test core soft reset, register access, AHB idle, FIFO RAM (1.25 KB) write/read pattern test through the DFIFO access window. | S | T |
| USB-010 | **Device mode:** with a USB cable from the user USB connector to the host (Appendix B), enumerate as a CDC-ACM device (SDK USB Device library). The host script checks enumeration and echoes 1 MB through it; report throughput. | E | T |
| USB-011 | Device mode: enumerate as MSC (RAM disk) and HID; the host checks each. | E | T |
| USB-012 | Device mode: test suspend, resume and remote wake-up (via B1). | E, O | T |
| USB-013 | Test VBUS sensing on PA9 (cable plugged/unplugged detection) and the OTG_FS wake-up interrupt (EXTI 18). | E, O | T |
| USB-020 | **Host mode:** drive the VBUS switch enable (PG6) on and off; check VBUS on PA9 follows and the operator confirms the VBUS LED. | S, O | T |
| USB-021 | Host mode: with a USB flash drive (Appendix B) on an OTG adapter, enumerate it, read its descriptors, mount with FatFs, write/read/verify a file; with a USB keyboard or mouse, decode HID reports. | E | T |
| USB-022 | Test the over-current input (PG7): read it in normal state; induced over-current is Level E with a load on VBUS; the operator confirms the over-current LED. | E, O | T |
| USB-023 | Test the ID pin (PA10): read the level with no cable (B-device) and with an OTG A-cable (A-device). | E, O | T |
| USB-024 | Test SOF output and the SOF-to-TIM2 trigger (TIM-013). | E | T |
| USB-025 | Test OTG protocols HNP/SRP where the host can take part; otherwise N/A with reason. | E | T |

### 8.29 USB OTG HS (USBHS)

| ID | Requirement | Level | Method |
|---|---|---|---|
| USBHS-001 | Test the OTG_HS core: soft reset, register access, FIFO RAM (4 KB) pattern test, DMA (internal) to SRAM. | S | T |
| USBHS-002 | High-speed operation needs an external ULPI PHY, which is not fitted on this board. Report N/A `NOT_FITTED` for ULPI tests. | — | I |
| USBHS-003 | Full-speed operation through the OTG_HS embedded FS PHY on PB14/PB15: PB14 drives LD3 on this board, so this test shall be Level E with a USB breakout on PB14/PB15 (and LD3 disconnected by its solder bridge per [UM1974]), and repeat USB-010 on OTG_HS. Otherwise N/A `PIN_CONFLICT`. | E | T |

### 8.30 Graphics and media (LTDC, DMA2D, JPEG, DCMI)

| ID | Requirement | Level | Method |
|---|---|---|---|
| LTDC-001 | Test the LCD-TFT controller without a display: configure a 320×240 timing from PLLSAI, both layers, all pixel formats, colour keying, CLUT, blending, background colour; check the line interrupt, register reload interrupt and the current position register counting. | S | T |
| LTDC-002 | Measure HSYNC/VSYNC/pixel clock via harness to timer input capture (Level L) or a scope (Level E). | L / E | T |
| LTDC-003 | With an RGB display module (Appendix B), show test patterns (colour bars, gradients, grid) for operator confirmation. | E, O | D |
| DMA2D-001 | Test Chrom-ART: register-to-memory fill, memory-to-memory copy, memory-to-memory with pixel format conversion (all input formats to all output formats), and blending, comparing results with a CPU reference. | S | T |
| DMA2D-002 | Test DMA2D CLUT loading, line offsets, transfer-complete/error/watermark interrupts, and abort/suspend. | S | T |
| DMA2D-003 | Measure DMA2D fill and copy throughput vs CPU. | S | T, A |
| JPEG-001 | Test the hardware JPEG codec: decode a known baseline JPEG image stored in flash and compare the output with a stored CRC; encode a known raw image and compare the output length and CRC; for 4:2:0, 4:2:2, 4:4:4 and greyscale. | S | T |
| JPEG-002 | Test JPEG header parsing, quantisation and Huffman table programming, FIFO interrupts and DMA (MDMA is not on F7: use DMA2), and error handling with a corrupted image. | S | T |
| DCMI-001 | Test the camera interface without a camera: configure snapshot mode with external sync and drive PIXCLK, HSYNC, VSYNC and D0–D7 from GPIOs or timers via harness to capture a known frame. | L | T |
| DCMI-002 | Test embedded synchronisation codes, crop window, JPEG mode, frame/line/overrun interrupts via the harness generator. | L | T |
| DCMI-003 | With a camera module (Appendix B), capture a frame and report its size and a checksum; operator may confirm the image via the host. | E | T |

### 8.31 Other digital peripherals (CRC, RNG, CEC, DFSDM, MDIOS)

| ID | Requirement | Level | Method |
|---|---|---|---|
| CRC-001 | Test the CRC unit with the default CRC-32 polynomial and with programmable 7/8/16/32-bit polynomials, initial value, input and output reversal, against known check values (e.g. "123456789"), using CPU and DMA input. | S | T |
| RNG-001 | Test the RNG: enable, data ready, no seed or clock error; collect 1 Mbit and run the FIPS 140-2 monobit, poker, runs and long-run tests. | S | T |
| RNG-002 | Test the RNG clock error detection (stop the 48 MHz clock) and seed error reporting path. | S | T |
| CEC-001 | Test HDMI-CEC in loopback: CEC line (PB6 or PA15) with an external pull-up (harness to 3V3 through a resistor, or internal pull-up if within the CEC spec for test); transmit a frame to its own logical address in listen mode and receive it; test arbitration, bit timing errors, and acknowledge. | L | T |
| CEC-002 | Test CEC wake-up from Stop mode on a received message. | L | T |
| DFSDM-001 | Test DFSDM using the parallel data input (CPU writes to CHyDATINR) for each of the 8 channels and 4 filters: sinc1–sinc5 and FastSinc orders, oversampling ratios, integrator, offset and right-shift, and compare with a software model. | S | T |
| DFSDM-002 | Test DFSDM serial input: SPI-mode bitstream generated by an SPI master or SAI on harness wires (data + clock) into a DFSDM channel, Manchester mode by SAI SPDIF-like encoding where possible. | L | T |
| DFSDM-003 | Test DFSDM analog watchdog, short-circuit detector, extremes detector, injected and regular conversions, triggers from timers and DMA. | S, L | T |
| DFSDM-004 | Test the clock absence detector. | L | T |
| MDIOS-001 | Test the MDIO slave: wire the Ethernet MAC MDIO/MDC master to the MDIOS pins via harness (with the PHY sharing the bus, MDIOS at a different PHY address), read and write all 32 MDIOS registers from the MAC master, and check the write/read/error flags and interrupts. | L | T |

### 8.32 Debug features (DBG)

| ID | Requirement | Level | Method |
|---|---|---|---|
| DBG-001 | Test the DWT cycle counter (CYCCNT): enable, measure a known delay against SysTick. | S | T |
| DBG-002 | Test DWT watchpoints by programming a comparator to trigger the DebugMonitor exception on a write to a test variable (monitor-mode debug, without an external debugger). | S | T |
| DBG-003 | Test the FPB by setting a breakpoint in monitor mode on a test function and catching the DebugMonitor exception. | S | T |
| DBG-004 | Test ITM stimulus ports 0–31 and SWO output on PB3 at 2 MHz (Manchester/NRZ). The host captures SWO through OpenOCD and the host script checks the message. | E | T |
| DBG-005 | Test DBGMCU freeze bits for timers, RTC, I2C SMBus timeouts and CAN during a debugger halt (Level E with OpenOCD). | E | T |
| DBG-006 | Test the DBGMCU low-power debug bits (DBG_SLEEP/STOP/STANDBY): with them set, the debugger stays connected in each low-power mode. | E | T |
| DBG-007 | SWD access is verified by flashing and by the host tool (`probe`). ETM trace (TRACECLK, TRACED0–3 on PE2–PE6) requires a trace probe not on the board: report N/A `NOT_FITTED` unless a trace probe is listed as available. | E | I |

### 8.33 ST-LINK and board-level functions (STLK, BRD)

| ID | Requirement | Level | Method |
|---|---|---|---|
| STLK-001 | VCP data path: verified by CLI-001…CLI-005 and by a host-side stress test of 1 MB echo at 115200 and 921600 baud with zero errors. | S | T |
| STLK-002 | ST-LINK MCO 8 MHz: verified by CLK-002. | S | T |
| STLK-003 | ST-LINK USB mass-storage flashing: the operator copies HWVAL `.bin` to the `NOD_F767ZI` drive and HWVAL boots with the new build ID. | E, O | D |
| STLK-004 | ST-LINK SWD and reset control: verified by flashing, `scripts/docker-dev.sh reset`, and the connect-under-reset recovery procedure of [HW §8.4]. | E | D |
| STLK-005 | ST-LINK COM LED: observed by the operator during STLK-001. | S, O | D |
| BRD-001 | Solder bridges and jumpers: an inspection checklist (in the test report) records the position of JP5 (power source), JP6 (IDD), the ST-LINK jumpers, and any solder bridge changed from default. | — | I |
| BRD-002 | Zio and morpho headers: every header pin is covered by GPIO-002/GPIO-003 (drivable pins) or listed in the pin table with its fixed function (power, GND, NRST, reserved). Power pins (3V3, 5V, VIN, IOREF, AVDD, AGND) are measured by the operator with a meter (Level E). | S, E | T, I |

---

## 9. Safety and protection requirements

| ID | Requirement | Method |
|---|---|---|
| SAFE-001 | HWVAL shall never write option bytes. Code paths that unlock the option bytes (OPTKEYR) shall not be compiled in. | I |
| SAFE-002 | HWVAL shall never write OTP. The OTP area shall be mapped read-only by the MPU. | I, T |
| SAFE-003 | HWVAL shall never set read protection level 1 or 2, write protection, or change the boot addresses. | I |
| SAFE-004 | Destructive flash tests shall be limited to sector 11, enforced by an address check in the flash driver wrapper that rejects any other address. | I, T |
| SAFE-005 | Destructive tests (flash, backup SRAM, backup registers, RTC configuration, SD card writes) shall run only when `--destructive` is given, and shall print what will be changed and ask for confirmation in interactive mode. | T |
| SAFE-006 | HWVAL shall refuse to drive any pin that the pin table marks as owned by another circuit unless the owning test is running. | T |
| SAFE-007 | Tests that change the clock shall keep the HSE/PLL within datasheet limits, and shall switch the system clock to HSI before reconfiguring the PLL. | I |
| SAFE-008 | Low-power tests shall leave a wake-up source armed, and HWVAL shall always enable `DBG_SLEEP/STOP/STANDBY` so that the debugger can connect if a wake-up fails. | I |
| SAFE-009 | GPIO-010 (5 V test) and RST-006 (bench supply) shall require a confirmation that names the equipment and the expected wiring. | T |
| SAFE-010 | SD card and USB drive write tests shall use a dedicated file or LBA range and shall not format the medium without explicit operator confirmation. | T |
| SAFE-011 | HWVAL shall check `FLASH_OPTCR.nDBANK` at boot. If the flash is in dual-bank mode (product configuration, ASM-005), HWVAL shall print and report a warning, and every destructive flash test shall report N/A `WRONG_FLASH_MODE` instead of erasing. | T |
| SAFE-012 | In link mode, `Run` with destructive tests allowed shall still confirm each destructive test through `OperatorPrompt` (LINK-008); the `allow destructive` option alone shall not be enough. | T |

---

## 10. Non-functional requirements

| ID | Requirement | Method |
|---|---|---|
| NFR-001 | **Performance:** a CLI command shall be acknowledged within 50 ms of the line ending being received. | T |
| NFR-002 | **Duration:** `run all --level S --no-operator --no-destructive` shall complete in under 5 minutes. | T |
| NFR-003 | **Footprint:** the HWVAL image shall fit in sectors 0–10 (1792 KB) including test assets (JPEG images, sine tables). | I |
| NFR-004 | **RAM:** HWVAL shall leave at least 256 KB of SRAM1 and 64 KB of DTCM free for the destructive RAM tests to run in-place, using the save-test-restore method only for the remainder. | I, A |
| NFR-005 | **Reliability:** 100 consecutive `run all --level S --no-operator --no-destructive` loops shall complete with no FAIL on a known-good board. | T |
| NFR-006 | **Determinism:** every test shall give the same result on repeated runs on a known-good board (no flaky tests). Random test data shall come from a seeded PRNG with the seed printed. | T |
| NFR-007 | **Code quality:** the code shall compile with `-Wall -Wextra -Werror` and pass a static analysis run (cppcheck) with no high-severity findings. | I |
| NFR-008 | **Maintainability:** each hardware group shall live in its own source file `tests/test_<group>.c` with its pin and limit definitions in the matching header. | I |
| NFR-009 | **Portability:** board-specific pins and harness wiring shall be in one board file, so that the suite can be adapted to another Nucleo-144 F7 board. | I |
| NFR-010 | **Documentation:** a user guide shall describe the CLI, every test (generated from the test table), the harness wiring and the external equipment setup. | I |
| NFR-011 | **Footprint with link mode:** the flatcc runtime and the HWLINK code shall fit within NFR-003 and keep the RAM floors of NFR-004; `scripts/check_footprint.py` shall still pass. (CR-08) | I |

### Debug output (USART6)

USART3 is the HWVAL console and HWLINK port.  Debug lines go to USART6 TX
on PG14 (Arduino D1), 115200 8N1, in the format BOOT and the demo
applications use: `[<ms>] <TAG> <E|W|I|D> <text>` (milliseconds since reset, right-aligned to 7 digits, CR LF), tag `HWVAL`.

| ID | Requirement | Method |
|---|---|---|
| DUART-001 | HWVAL shall log start (version, commit, slot, trial state, reset cause), the BOOT trial health verdict, entering link mode, each test run (count, failures by ID, summary) and deliberate resets. | D |
| DUART-002 | `hwval_dbg()` shall copy the line into a 1 KB ring and return; a priority-1 task shall drain it by polling, with no interrupt and no DMA, because the interrupt and DMA tests own those (`USART6_IRQHandler` in test_int.c, the DMA2 sweep). Output may lag while CPU-bound tests run; a full ring drops lines. | I |
| DUART-003 | Tests that use or reset USART6 (UART-001/003 loopback, UART-009 special modes, RST-007 peripheral reset) shall pause the debug output and re-initialise USART6 afterwards, so their coverage is unchanged. | T |
| DUART-004 | PG14 and PG9 shall be console-owned in the pin table, so the GPIO and ADC tests never drive them. | I |

---

## 11. Host-side tooling requirements

| ID | Requirement | Method |
|---|---|---|
| HOST-001 | A host script `scripts/hwtest.py` shall connect to the VCP, switch HWVAL to `mode script` and `format json`, run a selected set of tests, and write a JSON and a Markdown report named with the chip unique ID and date. | T |
| HOST-002 | The host script shall forward operator prompts to the terminal and relay the answers. | D |
| HOST-003 | The host script shall support the host side of tests: USB CDC echo (USB-010), Ethernet ping/TCP/UDP (ETH-007/008), SWO capture via OpenOCD (DBG-004), and debugger-halt freeze checks (WDG-004, DBG-005). | T |
| HOST-004 | The host script shall exit with code 0 when all selected tests pass, 1 when any fail, 2 on communication errors. | T |
| HOST-005 | The host script shall run inside the project Docker image as well as on the host. | D |
| HOST-006 | The HWVAL Studio GUI [GUI] shall be a second host front end using link mode (§6.4). Its JSON/Markdown reports shall keep the `hwtest.py` naming and content (HOST-001), and `hwtest.py` shall keep working through the text CLI. | D |

---

## 12. Coverage matrix

Every hardware element of the board and the MCU is listed below with the
requirements that cover it. **N/A** rows are covered by a requirement that
documents why the element cannot be exercised on this board.

### 12.1 MCU core and system

| Element | Requirements |
|---|---|
| Device identity (IDCODE, CPUID, flash size, package, UID, calibration data) | DEV-001…DEV-006 |
| Cortex-M7 integer/DSP pipeline, exceptions, SVC/PendSV/NMI | CORE-001, CORE-003…CORE-007, CORE-010…CORE-012 |
| FPU (single and double precision), lazy stacking | CORE-002, FRT-009 |
| SysTick | CORE-008, FRT-002, FRT-023 |
| L1 I-cache and D-cache | CACHE-001…CACHE-003, CORE-009 |
| ITCM RAM, DTCM RAM | CACHE-004, RAM-001, RAM-002, DMA-005 |
| MPU | MPU-001…MPU-004, SAFE-002 |
| NVIC (110 external IRQs), priorities, masking | INT-001…INT-003 |
| Fault handling (HardFault, MemManage, BusFault, UsageFault) | GEN-008, CORE-003…CORE-006, MPU-002 |
| SRAM1, SRAM2 | RAM-003…RAM-007 |
| Backup SRAM | RAM-008, RAM-009, DMA-005 |
| Flash memory (2 MB), flash interface, ART, prefetch | FLS-001…FLS-009 |
| Option bytes | OB-001, OB-002 (writes N/A by policy) |
| OTP area | OTP-001, OTP-002 (writes N/A, irreversible) |
| System memory (ROM bootloader) | FLS-002 (read), §11 tooling: `stm32flash`/`dfu-util` boot entry is a demonstration item for STLK-004 |
| RCC: HSI, HSE bypass, HSE crystal, LSI, LSE, PLL, PLLI2S, PLLSAI | CLK-001…CLK-009 (CLK-003 N/A not fitted) |
| RCC: MCO1, MCO2 | CLK-010 |
| RCC: CSS (HSE), LSE CSS | CLK-011, CLK-012 |
| RCC: bus prescalers, peripheral clock enables and resets | CLK-013, CLK-014, RST-007 |
| RCC: interrupts | CLK-015 |
| PWR: VDD/VDDA/VBAT monitoring, PVD | PWR-001…PWR-003 |
| PWR: voltage scaling, over-drive, under-drive | PWR-004, PWR-006 |
| PWR: Sleep, Stop, Standby, wake-up pins | PWR-005…PWR-008 |
| PWR: backup domain, backup regulator | PWR-010, RAM-009 |
| Current consumption per mode (IDD) | PWR-009 |
| Reset sources (POR/PDR, pin, software, IWDG, WWDG, low-power, BOR) | RST-001…RST-006, GEN-010 |
| IWDG, WWDG | WDG-001…WDG-004, GEN-009 |
| RTC (calendar, alarms, wake-up, calibration, time-stamp, tamper, reference clock, shift) | RTC-001…RTC-009, RTC-011 |
| RTC backup registers (32) | RTC-010 |
| EXTI (lines 0–24) | INT-004…INT-006 |
| SYSCFG | INT-007 |
| DMA1 (8 streams), DMA2 (8 streams) | DMA-001…DMA-007 |
| CRC | CRC-001, FLS-001 |
| RNG | RNG-001, RNG-002 |
| DBGMCU, DWT, ITM, FPB, SWO | DBG-001…DBG-006, WDG-004 |
| ETM trace | DBG-007 (N/A without trace probe) |

### 12.2 MCU peripherals

| Element | Requirements |
|---|---|
| GPIOA–GPIOG, GPIOH (PH0/PH1 reserved for HSE) | GPIO-001…GPIO-008, GPIO-010, CON-005 |
| GPIOI, GPIOJ, GPIOK | GPIO-009 (N/A, not bonded on LQFP144) |
| ADC1, ADC2, ADC3, temperature sensor, VREFINT, VBAT | ADC-001…ADC-012, PWR-001, PWR-002 |
| DAC channel 1, channel 2 | DAC-001…DAC-006, ADC-004 |
| TIM1, TIM8 (advanced) | TIM-001, TIM-003…TIM-011, TIM-014 |
| TIM2, TIM5 (32-bit) | TIM-001…TIM-005, TIM-009, TIM-011, TIM-013, TIM-014 |
| TIM3, TIM4 | TIM-001, TIM-003…TIM-005, TIM-009, TIM-011, TIM-014, LED-002 |
| TIM6, TIM7 (basic) | TIM-001, TIM-012, DAC-003 |
| TIM9, TIM10, TIM11, TIM12, TIM13, TIM14 | TIM-001, TIM-003, TIM-004, TIM-009, TIM-013, LED-002 |
| LPTIM1 | LPTIM-001, LPTIM-002 |
| USART1, USART2, USART6, UART4, UART5, UART7, UART8 | UART-001…UART-003, UART-005…UART-010 |
| USART3 (VCP) | UART-004, CLI-001…CLI-005, STLK-001 |
| SPI1–SPI6 | SPI-001…SPI-007 |
| I2S1–I2S3 | I2S-001…I2S-003 |
| SAI1, SAI2 | SAI-001…SAI-006 |
| SPDIFRX | SPDIF-001, SPDIF-002 |
| I2C1–I2C4 | I2C-001…I2C-008 |
| CAN1, CAN2, CAN3 | CAN-001…CAN-009 |
| SDMMC1, SDMMC2 | SD-001…SD-007 |
| QUADSPI | QSPI-001…QSPI-003 |
| FMC (NOR/SRAM, NAND, SDRAM) | FMC-001, FMC-002 |
| Ethernet MAC, MAC DMA, PTP, MMC, power management | ETH-001, ETH-004, ETH-009…ETH-013 |
| USB OTG FS core, PHY, VBUS, ID, SOF | USB-001, USB-010…USB-013, USB-023…USB-025 |
| USB OTG HS core | USBHS-001, USBHS-003 |
| USB OTG HS ULPI (high speed) | USBHS-002 (N/A, no ULPI PHY fitted) |
| LTDC | LTDC-001…LTDC-003 |
| DMA2D (Chrom-ART) | DMA2D-001…DMA2D-003 |
| JPEG codec | JPEG-001, JPEG-002 |
| DCMI | DCMI-001…DCMI-003 |
| HDMI-CEC | CEC-001, CEC-002 |
| DFSDM1 (8 channels, 4 filters) | DFSDM-001…DFSDM-004 |
| MDIOS | MDIOS-001 |
| Cryptographic processor, HASH | Not present on STM32F767 (F777 only): N/A by part number, DEV-001 confirms the part |

### 12.3 Board components

| Element | Requirements |
|---|---|
| ST-LINK/V2.1: VCP | STLK-001, CLI-001…CLI-005 |
| ST-LINK/V2.1: MCO 8 MHz to PH0 | STLK-002, CLK-002 |
| ST-LINK/V2.1: SWD, NRST control | STLK-004, DBG-007 |
| ST-LINK/V2.1: SWO | DBG-004 |
| ST-LINK/V2.1: USB mass-storage flashing | STLK-003 |
| ST-LINK/V2.1: COM LED | STLK-005, LED-004 |
| LD1 green (PB0), LD2 blue (PB7), LD3 red (PB14) | LED-001…LED-003, FRT-012 |
| Power LED, VBUS LED, over-current LED, Ethernet RJ45 LEDs | LED-004, PWR-012, USB-020, USB-022, ETH-005 |
| B1 user button (PC13) | BTN-001, BTN-002, PWR-007, RTC-008, RTC-009 |
| B2 reset button | RST-003, BTN-003 |
| LSE crystal 32.768 kHz | CLK-004, RTC-001 |
| HSE crystal footprint (not fitted) | CLK-003 (N/A) |
| LAN8742A PHY, RJ45, 25 MHz PHY crystal, RMII | ETH-001…ETH-008 |
| USB OTG FS micro-AB connector | USB-010…USB-013, USB-021, USB-023 |
| USB VBUS power switch (PG6), over-current (PG7) | USB-020, USB-022 |
| 5 V input selection (U5V, VIN, E5V, CHGR, JP5) | PWR-011, BRD-001 |
| 3.3 V regulator | PWR-001 |
| IDD measurement jumper (JP6) | PWR-009, BRD-001 |
| Zio (Arduino) connector | GPIO-002…GPIO-004, BRD-002 |
| ST morpho headers | GPIO-002…GPIO-004, BRD-002 |
| Solder bridges and jumpers | BRD-001 |

### 12.4 Coverage check

| ID | Requirement | Method |
|---|---|---|
| COV-001 | Every row of §12.1–§12.3 shall map to at least one test case in the HWVAL test table (TFW-002). A build-time script shall fail the build if any requirement in §8 has no test case, or any test case lists no requirement. | I, T |
| COV-002 | Every peripheral base address defined in the CMSIS device header `stm32f767xx.h` shall appear in §12. A script shall compare the header's `*_BASE` definitions with the matrix. | I |
| COV-003 | Every IRQ in the CMSIS device header (110 external entries) shall be exercised by INT-001 and, for its peripheral, by the peripheral test. | T |

---

## Appendix A — Loopback harness

The harness is a set of female-female jumper wires on the Zio and morpho
headers. The pin pairs are fixed in the HWVAL design in
`board/hwval_board_pins.c` (`hwval_wire_table`) against the
alternate-function tables of [DS11532] and the board pin-out of [UM1974],
respecting the pin-conflict table of [HW §13] (OPEN-001 closed by that
board file; entries marked *verify* have their AF muxing confirmed on the
bench during bring-up, OPEN-004). Each wire connects the functions below.

| Wire | From | To | Used by | status |
|---|---|---|---|---|
| H01 | DAC_OUT1 (PA4) | ADC input A (PF3/ADC3_IN9) | ADC-004, DAC-* | fixed |
| H02 | DAC_OUT2 (PA5) | ADC input B (PF5/ADC3_IN15) | ADC-004, DAC-* | fixed |
| H03 | USART1_TX (PA9) | USART1_RX (PA10) | UART-001…010 | fixed |
| H04 | USART2_TX (PD5) | USART2_RX (PD6) | UART-001…010 | fixed |
| H05 | USART6_TX (PG14) | USART6_RX (PG9) | UART-001…010 | fixed |
| H06 | UART4_TX (PC10) | UART4_RX (PC11) | UART-001…010 | fixed |
| H07 | UART5_TX (PC12) | UART5_RX (PD2) | UART-001…010 | fixed |
| H08 | UART7_TX (PE7) | UART7_RX (PE8) | UART-001…010 | fixed |
| H09 | UART8_TX (PE0) | UART8_RX (PE1) | UART-001…010 | fixed |
| H10 | USART2_RTS (PD4) | USART2_CTS (PD3) | UART-005 | fixed |
| H11 | USART6_RTS (PG12) | USART6_CTS (PG13) | UART-005 | fixed |
| H12 | USART2_TX (PD5) | USART6_RX (PG9) | UART-006/009 (cross, parity/framing error injection) | fixed |
| H12b | USART6_TX (PG14) | USART2_RX (PD6) | UART-006/009 (cross) | fixed |
| H13 | SPI1_MOSI (PB5) | SPI1_MISO (PB4) | SPI-001 | fixed |
| H14 | SPI2_MOSI (PB15) | SPI2_MISO (PC2) | SPI-001 | fixed |
| H15 | SPI4_MOSI (PE14) | SPI4_MISO (PE13) | SPI-001 | fixed |
| H16 | SPI5_MOSI (PF11) | SPI5_MISO (PF9) | SPI-001 | fixed |
| H17 | I2C2_SCK (PF7) | I2C2_SDA (PF1) | clock-path note | verify |
| H19 | SPI4_SCK (PE12) | SPI5_SCK (PF7) | SPI-002, I2S-001 | fixed |
| H19b | SPI4_MOSI (PE14) | SPI5_MOSI (PF11, slave in) | SPI-002, I2S-001 | fixed |
| H20 | I2C1_SCL (PB8) | I2C3_SCL (PA8) | I2C-001…008 | fixed |
| H20b | I2C1_SDA (PB9) | I2C3_SDA (PC9) | I2C-001…008 | fixed |
| H20c | I2C2_SCL (PF1) | I2C4_SCL (PD13) | I2C-001…008 | fixed |
| H20d | I2C2_SDA (PF0) | I2C4_SDA (PD14) | I2C-001…008 | fixed |
| H21 | CAN1_TX (PD1) | CAN1_RX (PD0) | CAN-005/007/009 | fixed |
| H22 | TIM1_CH1 (PE9) | TIM8/3_CH1 (PC6) | TIM-003/004/007 | fixed |
| H23 | TIM1_CH2 (PE11) | TIM4_CH1 (PB6) | TIM-005 encoder | fixed |
| H23b | TIM1_CH3 (PE13) | TIM4_CH2 (PB7) | TIM-005 encoder | fixed |
| H24 | GPIO (PG10) | TIM1_BKIN2/ETR (PB12) | TIM-008/014 | verify |
| H25 | GPIO (PF12) | TIM1_CH1 (PE9) | TIM-010 hall (input side) | fixed |
| H25b | GPIO (PF13) | TIM1_CH2 (PE11) | TIM-010 hall (input side) | fixed |
| H25c | GPIO (PF15) | TIM1_CH3 (PE13) | TIM-010 hall (input side) | fixed |
| H26 | MCO1 (PA8) | TIM13_CH1 (PF8) | CLK-010 | fixed |
| H26b | MCO2 (PC9) | TIM10_CH1 (PB8, fan-out w/H20) | CLK-010 | fixed |
| H27 | SAI1_SD_A (PE5) | SAI1_SD_B (PE3) | SAI-001…003, SAI-005 | verify |
| H27b | SAI1_SD_A (PE5) | SPDIFRX_IN1 (PE9) | SAI-004, SPDIF-001/002 | verify |
| H28 | SPI/I2S clock+data (PF7) | DFSDM CKINy (PF10) | DFSDM-002/004 | verify |
| H31 | DCMI D0 self-wired (PC6) | (8-line deskew bench, §8.30) | DCMI-001/002 | verify |
| H33 | GPIO (PB10) | WKUP1 (PA0) | PWR-008 | fixed |
| H33b | GPIO (PB11) | WKUP2 (PA2, also ETH MDIO) | PWR-008 | verify |
| H33c | GPIO (PB12) | WKUP3 (PC1, fan-out w/H24) | PWR-008 | verify |
| H34 | GPIO (PA3) | EXTI-capable pin (PC3) | INT-004/008 | fixed |
| H35 | LPTIM1_OUT (PE10) | LPTIM1_IN1/ETR (PA0) | LPTIM-001/002 | verify |

HWVAL shall detect each wire (ENV-002) by driving one end and reading the
other before using it; wires marked *verify* are excluded from the
auto-detection bitmask until their AF configuration is bench-confirmed.

## Appendix B — External equipment

| Item | Used by |
|---|---|
| Host PC with USB (already used for ST-LINK) | All |
| Second USB cable (micro-AB) to the user USB connector | USB-010…USB-013 |
| USB OTG adapter, USB flash drive, USB keyboard/mouse | USB-021, USB-023 |
| Ethernet cable and switch or PC with a static/DHCP network | ETH-006…ETH-009, ETH-012 |
| microSD card adapter (SDMMC 4-bit wiring) and a test card | SD-002…SD-007 |
| QSPI NOR flash module (e.g. 3.3 V, 16 MB) | QSPI-002, QSPI-003 |
| 2–3 CAN transceiver modules (3.3 V) and 120 Ω terminations | CAN-008 |
| I2C sensor or EEPROM module (3.3 V) | I2C-007 |
| External SRAM/SDRAM board on the FMC pins (optional) | FMC-002 |
| RGB TFT display module (optional) | LTDC-003 |
| Camera module with DCMI interface (optional) | DCMI-003 |
| Oscilloscope or frequency counter | CLK-010, GPIO-005, LTDC-002, ETH-010 |
| Ammeter (µA–mA range) | PWR-009 |
| Multimeter | BRD-002 |
| Programmable bench supply | RST-006, PWR-011 |
| 50/60 Hz reference clock source (optional) | RTC-011 |
| USB breakout on PB14/PB15 (optional) | USBHS-003 |

## Appendix C — Test ID catalogue format

Test case IDs shall be `<GROUP>.<NNN>[.<sub>]`, for example `ADC.004.DAC1`,
where `<GROUP>` matches the requirement area. Each test case lists the
requirement IDs it verifies. Example of a catalogue entry:

```
ID:        UART.001.USART6
Title:     USART6 loopback 1 KB at 9600/115200/921600 baud
Level:     L       Operator: no      Destructive: no     Timeout: 5 s
Needs:     harness H05 (USART6 TX -> USART6 RX)
Verifies:  UART-001
```

## Appendix D — Open items

| ID | Item | Status |
|---|---|---|
| OPEN-001 | Fix the harness pin pairs (Appendix A) against [DS11532] and [UM1974]. | **Closed**: pairs fixed in `board/hwval_board_pins.c` (§8.13 commit; CL-005); Appendix A carries the same table. |
| OPEN-002 | Confirm the LD1 solder-bridge option (PB0/PA5) and the LD3/PB14 solder bridge for USBHS-003 on the board silkscreen. | Open bench item (no impact on non-destructive tests; USBHS-003 reports N/A `PIN_CONFLICT` until confirmed). |
| OPEN-003 | Confirm the IDD jumper reference designator (JP6 per UM1974) and the expected current per mode from [DS11532] tables (PWR-009). | Open bench item; PWR-009 prints the expected ranges from the datasheet tables. |
| OPEN-004 | Confirm which SPDIFRX inputs, DFSDM channels, CEC pin and MDIOS pins are reachable on the headers without conflicts. | Partially resolved: H27b (SPDIFRX_IN1), H28 (DFSDM) and H31 are marked *verify* in the wire table until bench-checked; MDIOS is register-path only (MDIOS.001). |
| OPEN-005 | Review [ES0334] for revision Z errata that change any expected result (e.g. ADC, I2C, CAN, Ethernet) and record workarounds in the test limits. | Open bench item — the bench pass walks the errata checklist before the first full run. |
| OPEN-006 | Decide which optional external modules (Appendix B) are available at the bench; tests without equipment report SKIP. | Resolved by design: every case gates on equipment presence (ENV-002/003) — no equipment → `SKIP NO_EQUIPMENT`, no configuration needed. |
| OPEN-007 | Simplify CLK-011: the HSE CSS cannot be triggered with the ST-LINK MCO bypass source; agree that only configuration and NMI path are verified. | **Closed**: CLK-011's text already carries exactly this split (configuration + NMI-path verify; induced failure N/A `NOT_INDUCIBLE`). |
| OPEN-008 | LSE clock security. CLK-012 assumed an LSECSS exists. | **Closed (CL-016)**: the F767 has no LSE CSS (no LSECSSON/LSECSSD in RCC_BDCR per RM0410); CLK-012 verifies N/A with the register-map evidence. |
| OPEN-009 | Close the ST-LINK VCP board→host solder bridge (CL-010). Both the text CLI and link mode need it ([GUI] CR-09). | Open bench item |
| OPEN-010 | Write the shared schema `protocol/hwlink.fbs` and add flatcc to the Docker image before implementing §6.4. | **Closed**: `protocol/hwlink.fbs` is now in this repository; the flatcc 0.6.1 runtime is vendored in `third_party/flatcc` and the generated headers are committed (`src/generated/`), with build-time regeneration when `flatcc` is on `PATH`. |
| OPEN-011 | Fix the exact `mode link` → text fallback when the GUI is closed without sending `EnterTextMode` (e.g. a text `\r` line after 5 s of no frames, as sketched in [GUI] CR-01). | **Closed**: implemented in `src/link.c` (a `\r` starts a 5 s probe; a non-text byte cancels it). |
