#
# Makefile - Top-level build, clean and flash for HWVAL
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Target:  NUCLEO-F767ZI (STM32F767ZI)
#
# The firmware is built with CMake + Ninja inside the stm32-dev container
# (CON-001), the same way as scripts/build.sh. Flashing and debugging also
# run in the container by default; set NATIVE=1 to use the host's OpenOCD,
# GDB and picocom instead (scripts/install-tools.sh).
#
#   make                build the firmware (same as make build)
#   make build          configure (if needed) and build
#   make clean          remove the build directory
#   make rebuild        clean, then build
#   make flash          build, sign hwval.img and install it through BOOT
#   make erase          erase the whole flash (BOOT and both slots too)
#   make reset          reset the board
#   make debug          build, flash through BOOT, then GDB halted at reset
#   make gdbserver      OpenOCD GDB server on :3333
#   make serial         serial console on the ST-LINK VCP (115200 8N1)
#   make lint           clang-tidy static analysis (scripts/cppcheck.sh too)
#   make lint-strict    clang-tidy, failing on any finding
#   make probe          detect the ST-LINK and the target chip
#   make size           show the firmware size
#   make image          build the stm32-dev Docker image
#   make help           show this help
#
# Variables (override on the command line, e.g. make BUILD_TYPE=Debug):
#   BUILD_TYPE   Release | Debug                       (Release, as build.sh)
#   NATIVE       1 = flash/debug/serial with host tools (0)
#   BOOTLOADER   bootloader checkout with tools/ and keys/ (../bootloader)
#   SIGN_KEY     image signing key (BOOTLOADER/keys/dev-signing-key.pem)
#   SECURITY_COUNTER  anti-rollback counter, >= the board's floor (2)
#   PORT         serial port for make flash (default: bootctl auto-detect)
#
# HWVAL runs as a slot image under the A/B bootloader.  make flash never
# programs the raw ELF: BOOT only starts a signed image, which bootctl
# uploads and BOOT verifies, then waits for HWVAL to confirm its trial.
#

# Windows: run every recipe under Git's POSIX sh, also from cmd.exe or
# PowerShell with only Git\cmd on PATH. Otherwise native make falls back to
# cmd.exe, which cannot run the recipes (quotes, pipes, find, rm -rf), and
# `sh scripts/...` is not found. Git's usr\bin goes first on PATH so find,
# sort and friends are the POSIX tools, not the Windows ones of the same name.
ifeq ($(OS),Windows_NT)
ifneq ($(origin SHELL),command line)
GIT_USR_BIN := $(firstword \
  $(if $(wildcard C:/Program?Files/Git/usr/bin/sh.exe),C:/Program?Files/Git/usr/bin) \
  $(if $(wildcard $(subst \,/,$(LOCALAPPDATA))/Programs/Git/usr/bin/sh.exe),$(subst \,/,$(LOCALAPPDATA))/Programs/Git/usr/bin) \
  $(if $(wildcard C:/msys64/usr/bin/sh.exe),C:/msys64/usr/bin))
ifneq ($(GIT_USR_BIN),)
SHELL := $(subst ?, ,$(GIT_USR_BIN))/sh.exe
export PATH := $(subst /,\,$(subst ?, ,$(GIT_USR_BIN)));$(PATH)
endif
endif
endif

BUILD_TYPE ?= Release
NATIVE     ?= 0

BUILD_DIR  := build
ELF        := $(BUILD_DIR)/hwval.elf
IMAGE      := $(BUILD_DIR)/hwval.img
SIGNED     := $(BUILD_DIR)/hwval-signed.img
DEV        := scripts/docker-dev.sh

# Gow make on Windows runs a simple recipe line itself instead of through a
# shell, and the MSYS bash.exe it starts that way exits 1 without running the
# script at all - no output, no status.  Prefixing the command with `sh` (Git
# Bash's own sh) takes the path make needs, so the script runs and its exit
# status is kept.  Linux and macOS start the scripts directly, as before.
ifeq ($(OS),Windows_NT)
DEV        := sh $(DEV)
SERIAL_CMD := sh scripts/serial.sh
else
SERIAL_CMD := scripts/serial.sh
endif
# On Windows `python3` is the Microsoft Store alias stub, which exits 9009, so
# the host tools run as plain `python` there.
ifeq ($(OS),Windows_NT)
PYTHON     ?= python
else
PYTHON     ?= python3
endif
BOOTLOADER ?= $(patsubst %/tools/bootctl.py,%,$(firstword $(wildcard ../bootloader/tools/bootctl.py ../../bootloader/bootloader/tools/bootctl.py) ../bootloader/tools/bootctl.py))
SIGN_KEY   ?= $(BOOTLOADER)/keys/dev-signing-key.pem
SECURITY_COUNTER ?= 3
PORT       ?=
OPENOCD    := openocd -f board/st_nucleo_f7.cfg

.DEFAULT_GOAL := help

.PHONY: all build configure clean rebuild lint lint-strict flash erase reset \
        debug gdbserver serial probe size image help

all: build

# Reconfigure when the build directory is new or the build type changed;
# otherwise let Ninja's own dependency tracking decide what to rebuild.
configure:
	@if ! grep -qx "CMAKE_BUILD_TYPE:STRING=$(BUILD_TYPE)" $(BUILD_DIR)/CMakeCache.txt 2>/dev/null; then \
		echo "Configuring $(BUILD_TYPE) build in $(BUILD_DIR)/"; \
		$(DEV) run cmake -S /work -B /work/$(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
	fi

build: configure
	$(DEV) run cmake --build /work/$(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean
	@"$(MAKE)" --no-print-directory build

# clang-tidy needs the compile commands from a configured build tree.
lint: configure
	$(DEV) run cmake --build /work/$(BUILD_DIR) --target clang-tidy

lint-strict:
	$(DEV) run cmake -S /work -B /work/$(BUILD_DIR) -G Ninja -DHWVAL_LINT_STRICT=ON
	$(DEV) run cmake --build /work/$(BUILD_DIR) --target clang-tidy

size:
	@test -f $(ELF) || { echo "error: $(ELF) not found, run make build"; exit 1; }
	$(DEV) run arm-none-eabi-size /work/$(ELF)

image:
	$(DEV) build

flash: build
	@test -f $(SIGN_KEY) || { echo "error: $(SIGN_KEY) not found; set SIGN_KEY=/path/to/key.pem"; exit 1; }
	$(PYTHON) $(BOOTLOADER)/tools/mkimage.py --sign $(IMAGE) --key $(SIGN_KEY) \
		--security-counter $(SECURITY_COUNTER) --output $(SIGNED)
	$(PYTHON) $(BOOTLOADER)/tools/bootctl.py $(if $(PORT),--port $(PORT),) \
		update $(SIGNED) --activate --wait

ifeq ($(NATIVE),1)

erase:
	$(OPENOCD) -c "init; reset halt; flash erase_sector 0 0 last; exit"

reset:
	$(OPENOCD) -c "init; reset run; exit"

debug: flash
	$(OPENOCD) -l $(BUILD_DIR)/openocd.log & ocd=$$!; \
	trap 'kill $$ocd 2>/dev/null' EXIT; sleep 1; \
	gdb-multiarch -q $(ELF) -ex "target extended-remote :3333" -ex "monitor reset halt"

gdbserver:
	$(OPENOCD)

serial:
	$(SERIAL_CMD)

probe:
	st-info --probe

else

erase:
	$(DEV) erase

reset:
	$(DEV) reset

debug: flash
	$(DEV) debug $(ELF)

gdbserver:
	$(DEV) gdbserver

serial:
	$(DEV) serial

probe:
	$(DEV) probe

endif

help:
	@sed -n "/^#   make/p; /^# Variables/,/^#$$/p" $(firstword $(MAKEFILE_LIST)) | sed "s/^# \{0,1\}//"
