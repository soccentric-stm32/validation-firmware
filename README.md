# validation-firmware

HWVAL is a FreeRTOS firmware that tests every usable hardware block of the NUCLEO-F767ZI board and its STM32F767ZI MCU. It is driven from a terminal over the ST-LINK virtual COM port, from a Python script for automated runs, or from the HWVAL Studio desktop GUI through the binary HWLINK link mode. Each test is traced to a requirement, results are reported as JSON and Markdown, and build gates check the memory footprint, requirement coverage and static analysis. Use it for board bring-up, design validation and as the end-of-line test image on the production line.

[![The validation firmware running on the board](https://raw.githubusercontent.com/soccentric-stm32/soccentric-stm32.github.io/main/assets/shots/term-validation.webp)](https://soccentric-stm32.github.io/#validation)

## Features

- Tests every usable peripheral of the STM32F767ZI and the NUCLEO board
- 196 test cases grouped by hardware block
- Test levels: standalone, harness-wired and external equipment
- Interactive text console over the ST-LINK virtual COM port
- Script mode with JSON output for automation
- HWLINK link mode (COBS + FlatBuffers) for HWVAL Studio and HWVAL Factory
- Operator prompts for tests that need a human action
- Destructive flash tests confined to a reserved sector
- Ethernet tests with ping, TCP/UDP echo and throughput (LwIP)
- USB CDC echo, SWO and watchdog halt checks
- Boot banner with version, commit, build date and reset cause
- `hwtest.py` host tool that writes JSON and Markdown reports per board
- Footprint gate for image size and free RAM
- Coverage gate: every requirement has a test and every peripheral is listed
- Strict cppcheck and `-Wall -Wextra -Werror`
- Generated test catalogue and a full user guide
- Measured board reference in `Hardware.md`
- Reproducible Docker build environment

## Build and run

```bash
scripts/build.sh                             # build inside the stm32-dev container
scripts/docker-dev.sh flash build/hwval.elf  # flash over SWD
scripts/docker-dev.sh serial                 # open the console at 115200 8N1
python3 scripts/hwtest.py                    # run tests and write a report
```

## Related projects

- [bootloader](https://github.com/soccentric-stm32/bootloader) — A/B bootloader with UART update, CRC32 checks, trial boot and rollback
- [app-c-freertos](https://github.com/soccentric-stm32/app-c-freertos) — Demo application in C on FreeRTOS
- [app-c-zephyr](https://github.com/soccentric-stm32/app-c-zephyr) — Demo application in C on Zephyr RTOS
- [app-rust-freertos](https://github.com/soccentric-stm32/app-rust-freertos) — Demo application in Rust on Embassy
- [app-rust-zephyr](https://github.com/soccentric-stm32/app-rust-zephyr) — Demo application in Rust on Zephyr RTOS
- [validation-gui](https://github.com/soccentric-stm32/validation-gui) — HWVAL Studio: desktop GUI for tests, updates and flashing
- [factory-station](https://github.com/soccentric-stm32/factory-station) — HWVAL Factory: end-of-line test and provisioning station
- [soccentric-stm32.github.io](https://github.com/soccentric-stm32/soccentric-stm32.github.io) — Project website

## License

Released under the [MIT License](LICENSE) © 2026 Sandesh Ghimire. You are free
to use, modify and ship it in your own products.

Contributions are welcome. Fork the repository, make your change on a branch and
open a pull request. Bug reports, ideas and improvements are all appreciated.
