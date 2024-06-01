# Docker Development Environment

| | |
|---|---|
| **Author** | Sandesh Ghimire <sandesh@soccentric.com> |
| **Date** | 2026-11-02 |
| **Board** | NUCLEO-F767ZI (see [Hardware.md](../docs/Hardware.md)) |

A container with everything needed to build, flash and debug the firmware, so
the host only needs Docker. The wrapper script
[scripts/docker-dev.sh](../scripts/docker-dev.sh) runs every tool inside the
container.

## What is in the image

| Tool | Version (Ubuntu 24.04 packages) |
|---|---|
| `arm-none-eabi-gcc`, binutils, newlib, libstdc++ | 13.2.rel1 |
| `clang-tidy` | 18.1 (static analysis, `make lint`) |
| `gdb-multiarch` (also available as `arm-none-eabi-gdb`) | 15.1 |
| `cmake`, `ninja`, `make` | 3.28 / 1.11 / 4.3 |
| `openocd` | 0.12.0 |
| `st-info`, `st-flash` | 1.8.0 |
| `picocom` | 3.1 |
| `stm32flash` | Flash over the ROM bootloader on a UART (BOOT0 high) |
| `dfu-util` | 0.11. Flash over USB DFU (ROM bootloader on the user USB port) |
| `srecord` (`srec_cat`) | Convert and merge `.hex` / `.bin` / `.srec` images |
| **STM32CubeF7 SDK** | v1.17.4, at `$STM32CUBE_F7` = `/opt/st/STM32CubeF7` |

## STM32CubeF7 SDK

The image contains ST's complete STM32CubeF7 package (1.1 GB), taken from
ST's GitHub release with all submodules. The version is in
`$STM32CUBE_F7/VERSION` and can be changed at build time:

```bash
docker build --build-arg CUBE_F7_VERSION=v1.17.4 -t stm32-dev:latest docker
```

| Path under `$STM32CUBE_F7` | Contents |
|---|---|
| `Drivers/STM32F7xx_HAL_Driver` | HAL and LL drivers (`Inc/`, `Src/`) |
| `Drivers/CMSIS/Include` | Arm CMSIS core headers (`core_cm7.h` …) |
| `Drivers/CMSIS/Device/ST/STM32F7xx` | `stm32f767xx.h`, `system_stm32f7xx.c`, GCC startup files in `Source/Templates/gcc/` |
| `Drivers/BSP/STM32F7xx_Nucleo_144` | Board support for this board (LEDs, button) |
| `Drivers/BSP/Components` | Drivers for external chips, including the LAN8742 Ethernet PHY |
| `Middlewares/Third_Party` | FreeRTOS, LwIP, FatFs, mbedTLS, LibJPEG |
| `Middlewares/ST` | USB Device and USB Host libraries, STemWin, TouchGFX, audio |
| `Projects/STM32F767ZI-Nucleo` | Examples for this board: 41 HAL, 73 LL, 14 mixed HAL/LL, 8 applications (EEPROM, FatFs, LwIP, USB Device, USB Host), demonstration, templates |
| `Utilities` | CPU usage, fonts, logs and other helpers |

The GCC linker script for this chip is in each example's
`SW4STM32/STM32F767ZI-Nucleo/STM32F767ZITx_FLASH.ld`, and the matching
startup file is next to it (`SW4STM32/startup_stm32f767xx.s`).

To look at a stock example in the container:

```bash
scripts/docker-dev.sh shell
cd $STM32CUBE_F7/Projects/STM32F767ZI-Nucleo/Examples/GPIO/GPIO_IOToggle
```

Use the include paths `Inc`, `$STM32CUBE_F7/Drivers/CMSIS/Include`,
`$STM32CUBE_F7/Drivers/CMSIS/Device/ST/STM32F7xx/Include` and
`$STM32CUBE_F7/Drivers/STM32F7xx_HAL_Driver/Inc`, the defines
`-DSTM32F767xx -DUSE_HAL_DRIVER`, and the CPU flags
`-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard`.

The SDK is part of the image and read-only from the project's point of view.
To change a driver, copy the files into the project instead of editing
`/opt/st`.

### Not included

ST's own GUI and command-line tools (STM32CubeMX, STM32CubeIDE,
STM32CubeProgrammer, STM32CubeCLT) need an st.com account and a licence
acceptance to download, so they cannot be fetched automatically by the
image build. Everything they are needed for here (building, flashing,
debugging) is covered by the open tools above.

## Host requirements

- Docker, with your user in the `docker` group.
- Linux host. The ST-LINK is passed in through `/dev/bus/usb`, and the
  virtual COM port through `/dev/ttyACM*`.
- The ST-LINK udev rules installed on the host, so your user can open the
  device. `scripts/install-tools.sh` installs them; on a machine without
  that script, install the `stlink-tools` or `openocd` package for the rules.

## Quick start

Build the image (done automatically on first use):

```bash
scripts/docker-dev.sh build
```

Check that the container can see the board:

```bash
scripts/docker-dev.sh probe
```

Open a shell with the toolchain:

```bash
scripts/docker-dev.sh shell
```

## Commands

| Command | What it does |
|---|---|
| `build` | Build the `stm32-dev:latest` image from `docker/Dockerfile` |
| `shell` | Interactive shell, project mounted at `/work` |
| `run <cmd> [args]` | Run one command in the container, e.g. `run cmake --build build` |
| `probe` | Show the ST-LINK and the target chip |
| `flash <file> [addr]` | Program a `.elf`/`.hex`, or a `.bin` at `addr` (default `0x08000000`), verify and reset |
| `erase` | Erase the whole flash |
| `reset` | Reset the target and let it run |
| `gdbserver` | Run OpenOCD in the foreground: GDB on port 3333, telnet on 4444 |
| `debug <elf> [--load]` | Start OpenOCD and a GDB session halted at reset. `--load` flashes the ELF first |
| `serial [baud]` | Serial console on the ST-LINK virtual COM port (default 115200). Quit with Ctrl-A Ctrl-X |

Settings can be changed with environment variables:

| Variable | Default |
|---|---|
| `STM32_IMAGE` | `stm32-dev:latest` |
| `OPENOCD_CFG` | `board/st_nucleo_f7.cfg` |
| `SERIAL_PORT` | first `/dev/ttyACM*` |

## Debugging

### GDB inside the container

```bash
scripts/docker-dev.sh debug build/firmware.elf --load
```

GDB starts halted at the reset vector. Useful commands: `break main`,
`continue`, `next`, `print var`, `monitor reset halt`, `load`.

GDB commands can also be piped in for scripted checks:

```bash
printf 'break main\ncontinue\nbacktrace\nquit\n' | scripts/docker-dev.sh debug build/firmware.elf
```

### GDB or an IDE on the host

The container uses host networking, so OpenOCD's ports are reachable at
`localhost`:

```bash
scripts/docker-dev.sh gdbserver
```

Then in another terminal:

```bash
gdb-multiarch build/firmware.elf -ex "target extended-remote :3333" -ex "monitor reset halt"
```

Files built in the container record source paths under `/work`. If the host
GDB cannot find the sources, map them back to the project directory:

```gdb
set substitute-path /work /mnt/data/workspace/stm32
```

For VS Code with the Cortex-Debug extension, use `"servertype": "external"`
and `"gdbTarget": "localhost:3333"` while `gdbserver` is running.

## Notes

- The container runs as your host user, so build output in the project is
  owned by you, not root.
- Only files inside the project directory are visible in the container.
  `flash` and `debug` refuse paths outside it.
- Only one program can use the ST-LINK at a time. Stop `gdbserver` before
  running `flash`, `probe` or `erase`.
- If you unplug and replug the board, restart `serial` or `shell`. The
  ST-LINK USB node is picked up again automatically, but the `/dev/ttyACM*`
  device list is read when the container starts.

## Factory firmware backup

The 2 MB of flash as shipped by ST was read on 2026-11-02 (SHA-256
`a5ffdf6c5adb676412e4397d58a8b7528d922dbe5f5dfdc5fab948c0b27d51da`). It is
ST's own firmware, so it is not distributed with this repository; keep the
dump outside the project. To restore it, copy it into the project directory
(the container only sees files there) and flash it:

```bash
scripts/docker-dev.sh flash factory-firmware.bin
```
