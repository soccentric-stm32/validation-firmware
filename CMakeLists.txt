#
# CMakeLists.txt - HWVAL firmware, NUCLEO-F767ZI
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Date:    2026-11-02
# Target:  STM32F767ZIT6
# SRS:     SRS-HWVAL-F767 (GEN-001, GEN-006, NFR-007, NFR-008)
#
# Build (inside the stm32-dev container, CON-001):
#   cmake -S . -B build -G Ninja
#   cmake --build build
# or simply: scripts/build.sh [clean]
#
# Outputs in build/: hwval.elf, hwval.bin, hwval.hex, hwval.map (GEN-001).
#

cmake_minimum_required(VERSION 3.22)

set(HWVAL_VERSION "0.1.0")

# Cross toolchain: the build runs in the container where arm-none-eabi-gcc
# is the only compiler. Set explicitly so a stray host compiler is never used.
set(CMAKE_SYSTEM_NAME     Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY) # no OS to run a test exe in
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # compile_commands.json for clang-tidy (make lint)
find_program(ARMCC arm-none-eabi-gcc REQUIRED)
find_program(ARMOBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(SIZE_TOOL arm-none-eabi-size REQUIRED)
set(CMAKE_C_COMPILER   ${ARMCC})
set(CMAKE_ASM_COMPILER ${ARMCC})
set(CMAKE_OBJCOPY      ${ARMOBJCOPY})

project(hwval LANGUAGES C ASM)

# --- SDK location ----------------------------------------------------------
# STM32CubeF7 v1.17.4 is provided by the docker image (CON-002).
set(CUBE_ROOT "$ENV{STM32CUBE_F7}")
if(NOT CUBE_ROOT)
    set(CUBE_ROOT "/opt/st/STM32CubeF7")
endif()
if(NOT EXISTS "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Inc/stm32f7xx_hal.h")
    message(FATAL_ERROR
        "STM32CubeF7 not found at ${CUBE_ROOT}.\n"
        "Build through scripts/docker-dev.sh (see docker/README.md, CL-002).")
endif()

# --- Build metadata (GEN-003) ----------------------------------------------
if(EXISTS "${CUBE_ROOT}/VERSION")
    file(READ "${CUBE_ROOT}/VERSION" HWVAL_CUBE_VERSION)
    string(STRIP "${HWVAL_CUBE_VERSION}" HWVAL_CUBE_VERSION)
else()
    set(HWVAL_CUBE_VERSION "unknown")
endif()
execute_process(COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short=12 HEAD
    OUTPUT_VARIABLE HWVAL_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse HEAD
    OUTPUT_VARIABLE HWVAL_GIT_COMMIT_FULL OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} log -1 --date=iso --format=%cd
    OUTPUT_VARIABLE HWVAL_COMMIT_DATE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
string(TIMESTAMP HWVAL_BUILD_DATE "%Y-%m-%d %H:%M:%S")
string(TIMESTAMP HWVAL_BUILD_TIMESTAMP "%s" UTC)

# --- Toolchain flags --------------------------------------------------------
set(CPU_FLAGS -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(${CPU_FLAGS} -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
add_link_options(${CPU_FLAGS} -Wl,--gc-sections -Wl,--cref
    -Wl,-Map=${CMAKE_BINARY_DIR}/hwval.map
    --specs=nano.specs -u _printf_float)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -g3")
set(CMAKE_C_FLAGS_DEBUG   "-O0 -g3")

add_compile_definitions(
    STM32F767xx
    USE_HAL_DRIVER
    HSE_VALUE=8000000U          # ST-LINK MCO feeds PH0 in bypass mode (CON-005)
    USER_VECT_TAB_ADDRESS       # vector table at the bootloader slot (LAY-002)
    VECT_TAB_OFFSET=0x0000C000U
    HWVAL_VERSION_STR="${HWVAL_VERSION}"
    HWVAL_SRS_ID="SRS-HWVAL-F767"
    HWVAL_GIT_COMMIT="${HWVAL_GIT_COMMIT}"
    HWVAL_CUBE_VERSION="${HWVAL_CUBE_VERSION}"
    HWVAL_BUILD_DATE="${HWVAL_BUILD_DATE} ${HWVAL_COMMIT_DATE}")

# FreeRTOS heap size, configurable at build time (FRT-006).  The four tasks and
# their log buffers leave 3.5 KB of a 32 KB heap, below the 4 KB the BOOT health
# check requires, so every trial boot failed; 40 KB leaves about 11 KB free.
set(HWVAL_HEAP_KB "40" CACHE STRING "FreeRTOS heap_4 size in KB (FRT-006)")
math(EXPR HWVAL_HEAP_BYTES "${HWVAL_HEAP_KB} * 1024")

include_directories(src board tests
    "${CMAKE_BINARY_DIR}/generated"
    src/generated
    third_party/flatcc/include
    "${CUBE_ROOT}/Drivers/CMSIS/Include"
    "${CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32F7xx/Include"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Inc"
    "${CUBE_ROOT}/Middlewares/Third_Party/LwIP/src/include"
    board/lwip_arch)

# --- Sources ----------------------------------------------------------------
# HAL modules used so far. The list grows section by section (NFR-008):
set(HAL_SOURCES
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_cortex.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc_ex.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_gpio.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr_ex.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash_ex.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_iwdg.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_crc.c"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_crc_ex.c")

set(PROJECT_SOURCES
    src/main.c
    src/clock.c
    src/debug_uart.c
    src/version.c
    src/boot_slot.c
    src/uart3.c
    src/fault.c
    src/iwdg.c
    src/reset_cause.c
    src/mpu_setup.c
    src/hal_timebase.c
    src/syscalls.c
    src/rtos.c
    src/test_core.c
    src/cli.c
    src/settings.c
    src/harness.c
    src/hwlink.c
    src/flatcc_port.c
    src/link.c
    board/hwval_board_pins.c
    tests/test_rtos.c
    tests/test_dev.c
    tests/test_core_m7.c
    tests/test_cache_mpu.c
    tests/test_ram.c
    tests/test_flash.c)
list(APPEND PROJECT_SOURCES src/flash_ops.c)
list(APPEND PROJECT_SOURCES   tests/test_clk.c)
list(APPEND PROJECT_SOURCES   tests/test_pwr.c)
list(APPEND PROJECT_SOURCES   tests/test_rst_wdg.c)
list(APPEND PROJECT_SOURCES   tests/test_rtc.c)
list(APPEND PROJECT_SOURCES   tests/test_int.c)
list(APPEND PROJECT_SOURCES   tests/test_dma.c)
list(APPEND PROJECT_SOURCES   tests/test_gpio.c)
list(APPEND PROJECT_SOURCES   tests/test_led_btn.c)
list(APPEND PROJECT_SOURCES   tests/test_adc.c)
list(APPEND PROJECT_SOURCES   tests/test_dac.c)
list(APPEND PROJECT_SOURCES   tests/test_tim.c)
list(APPEND PROJECT_SOURCES   tests/test_uart.c)
list(APPEND PROJECT_SOURCES   tests/test_spi.c)
list(APPEND PROJECT_SOURCES   tests/test_sai_spdif.c)

# --- LwIP (SDK as shipped, CON-002): ETH-007/008 (section 8.27/section 11) --
# The middleware compiles into a static lib with SDK-level warning policy
# (-w) because the project -Werror policy does not extend to the SDK; the
# project-owned glue (src/net_lwip.c, arch headers) stays under -Werror.
set(LWIP_ROOT "${CUBE_ROOT}/Middlewares/Third_Party/LwIP/src")
set(LWIP_SOURCES
    "${LWIP_ROOT}/core/init.c"
    "${LWIP_ROOT}/core/def.c"
    "${LWIP_ROOT}/core/dns.c"
    "${LWIP_ROOT}/core/inet_chksum.c"
    "${LWIP_ROOT}/core/ip.c"
    "${LWIP_ROOT}/core/mem.c"
    "${LWIP_ROOT}/core/memp.c"
    "${LWIP_ROOT}/core/netif.c"
    "${LWIP_ROOT}/core/pbuf.c"
    "${LWIP_ROOT}/core/raw.c"
    "${LWIP_ROOT}/core/stats.c"
    "${LWIP_ROOT}/core/sys.c"
    "${LWIP_ROOT}/core/altcp.c"
    "${LWIP_ROOT}/core/altcp_alloc.c"
    "${LWIP_ROOT}/core/altcp_tcp.c"
    "${LWIP_ROOT}/core/tcp.c"
    "${LWIP_ROOT}/core/tcp_in.c"
    "${LWIP_ROOT}/core/tcp_out.c"
    "${LWIP_ROOT}/core/timeouts.c"
    "${LWIP_ROOT}/core/udp.c"
    "${LWIP_ROOT}/core/ipv4/autoip.c"
    "${LWIP_ROOT}/core/ipv4/dhcp.c"
    "${LWIP_ROOT}/core/ipv4/etharp.c"
    "${LWIP_ROOT}/core/ipv4/icmp.c"
    "${LWIP_ROOT}/core/ipv4/igmp.c"
    "${LWIP_ROOT}/core/ipv4/ip4.c"
    "${LWIP_ROOT}/core/ipv4/ip4_addr.c"
    "${LWIP_ROOT}/core/ipv4/ip4_frag.c"
    "${LWIP_ROOT}/netif/ethernet.c")
list(APPEND PROJECT_SOURCES src/net_lwip.c)
list(APPEND HAL_SOURCES
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_eth.c")
list(APPEND PROJECT_SOURCES   tests/test_i2c.c)
list(APPEND PROJECT_SOURCES   tests/test_can.c)
list(APPEND PROJECT_SOURCES   tests/test_sd_qspi_fmc.c)
list(APPEND PROJECT_SOURCES   tests/test_eth_usb.c)
list(APPEND PROJECT_SOURCES   tests/test_gfx_misc.c)
list(APPEND PROJECT_SOURCES   tests/test_dbg_brd.c)
list(APPEND PROJECT_SOURCES   tests/test_safe.c)

# flatcc 0.6.1 runtime as released (CON-002).  Only builder, verifier and
# emitter are linked; the project-owned arena hooks are in src/flatcc_port.c.
set(FLATCC_ROOT "${CMAKE_SOURCE_DIR}/third_party/flatcc")
set(FLATCC_SOURCES
    "${FLATCC_ROOT}/src/runtime/builder.c"
    "${FLATCC_ROOT}/src/runtime/verifier.c"
    "${FLATCC_ROOT}/src/runtime/emitter.c"
    "${FLATCC_ROOT}/src/runtime/refmap.c")

set(DEVICE_SOURCES
    "${CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32F7xx/Source/Templates/system_stm32f7xx.c"
    "${CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32F7xx/Source/Templates/gcc/startup_stm32f767xx.s")

# FreeRTOS V10.2.x kernel + native GCC CM7 port, no CMSIS wrappers (FRT-001)
set(FREERTOS_ROOT "${CUBE_ROOT}/Middlewares/Third_Party/FreeRTOS/Source")
include_directories(
    "${FREERTOS_ROOT}/include"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CM7/r0p1")
set(FREERTOS_SOURCES
    "${FREERTOS_ROOT}/list.c"
    "${FREERTOS_ROOT}/queue.c"
    "${FREERTOS_ROOT}/tasks.c"
    "${FREERTOS_ROOT}/timers.c"
    "${FREERTOS_ROOT}/event_groups.c"
    "${FREERTOS_ROOT}/stream_buffer.c"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CM7/r0p1/port.c"
    "${FREERTOS_ROOT}/portable/MemMang/heap_4.c")

# flatcc allocation hooks are visible everywhere (the runtime uses them).
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${CMAKE_SOURCE_DIR}/src/flatcc_port.h")
set(HWVAL_FLATCC_HEAP_KB "12" CACHE STRING "flatcc static arena size in KB")
math(EXPR HWVAL_FLATCC_HEAP_BYTES "${HWVAL_FLATCC_HEAP_KB} * 1024")

add_executable(hwval.elf ${PROJECT_SOURCES} ${HAL_SOURCES} ${FREERTOS_SOURCES}
    ${DEVICE_SOURCES} ${FLATCC_SOURCES})
target_compile_definitions(hwval.elf PRIVATE
    HWVAL_RTOS_HEAP_SIZE=${HWVAL_HEAP_BYTES}
    HWVAL_FLATCC_HEAP_SIZE=${HWVAL_FLATCC_HEAP_BYTES}
    FLATCC_ALLOC=hwval_flatcc_alloc
    FLATCC_FREE=hwval_flatcc_free
    FLATCC_REALLOC=hwval_flatcc_realloc
    FLATCC_CALLOC=hwval_flatcc_calloc
    FLATCC_ALIGNED_ALLOC=hwval_flatcc_aligned_alloc
    FLATCC_ALIGNED_FREE=hwval_flatcc_aligned_free
    FLATCC_EMITTER_ALLOC=hwval_flatcc_alloc
    FLATCC_EMITTER_FREE=hwval_flatcc_free)
# The generated flatcc headers and the SDK runtime use compact formatting.
set_source_files_properties(src/link.c PROPERTIES
    COMPILE_OPTIONS "-Wno-misleading-indentation")
set_source_files_properties(${FLATCC_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# LwIP: project-owned options/arch headers + SDK sources (warnings off;
# the SDK is as-shipped per CON-002). NO_SYS polling, see lwipopts.h.
add_library(lwip_sdk STATIC ${LWIP_SOURCES})
target_include_directories(lwip_sdk PRIVATE
    board
    board/lwip_arch
    "${CUBE_ROOT}/Drivers/CMSIS/Include"
    "${CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32F7xx/Include"
    "${CUBE_ROOT}/Drivers/STM32F7xx_HAL_Driver/Inc"
    "${LWIP_ROOT}/include")
target_compile_options(lwip_sdk PRIVATE ${CPU_FLAGS} -O2 -w)
target_compile_definitions(lwip_sdk PRIVATE
    STM32F767xx USE_HAL_DRIVER HSE_VALUE=8000000U)
target_link_libraries(hwval.elf lwip_sdk)

# --- HWLINK schema (LINK-012) ----------------------------------------------
# The generated C files are committed so the container does not need a host
# flatcc compiler.  When flatcc is available, regenerate them as part of the
# build; the build-tree copy takes precedence over src/generated.
find_program(FLATCC flatcc)
if(FLATCC)
    set(FLATCC_OUT "${CMAKE_BINARY_DIR}/generated")
    add_custom_command(
        OUTPUT
            "${FLATCC_OUT}/hwlink_reader.h"
            "${FLATCC_OUT}/hwlink_builder.h"
            "${FLATCC_OUT}/hwlink_verifier.h"
            "${FLATCC_OUT}/flatbuffers_common_reader.h"
            "${FLATCC_OUT}/flatbuffers_common_builder.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FLATCC_OUT}"
        COMMAND ${FLATCC} -a -o "${FLATCC_OUT}" "${CMAKE_SOURCE_DIR}/protocol/hwlink.fbs"
        DEPENDS "${CMAKE_SOURCE_DIR}/protocol/hwlink.fbs"
        COMMENT "Generating HWLINK FlatBuffers C headers")
    add_custom_target(hwlink_schema DEPENDS
        "${FLATCC_OUT}/hwlink_reader.h"
        "${FLATCC_OUT}/hwlink_builder.h"
        "${FLATCC_OUT}/hwlink_verifier.h")
    add_dependencies(hwval.elf hwlink_schema)
endif()

# GEN-006: link code and constants into flash sectors 0-10 only; the linker
# script (ld/STM32F767ZITx_FLASH.ld) fails the build on any overlap with
# sector 11 (0x081C0000, kept free for the destructive flash tests ASM-004).
target_link_options(hwval.elf PRIVATE
    "-T${CMAKE_SOURCE_DIR}/ld/STM32F767ZITx_FLASH.ld")

# --- Static analysis (NFR-007) ---------------------------------------------
# clang-tidy reuses the compile flags this very configure step produced, so
# it must run from a configured build tree:  make lint / make lint-strict.
find_program(CLANG_TIDY clang-tidy REQUIRED)
option(HWVAL_LINT_STRICT "Treat any clang-tidy finding as an error (lint-strict)" OFF)
if(HWVAL_LINT_STRICT)
    set(HWVAL_LINT_FLAGS --warnings-as-errors=*)
endif()

# The target runs with the build dir as CWD, so make source paths absolute.
set(HWVAL_LINT_SOURCES)
foreach(_src ${PROJECT_SOURCES})
    if(IS_ABSOLUTE "${_src}")
        list(APPEND HWVAL_LINT_SOURCES "${_src}")
    else()
        list(APPEND HWVAL_LINT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
    endif()
endforeach()
# clang-tidy runs the host clang but needs the newlib headers used by the
# arm-none-eabi toolchain; otherwise <stdio.h> is missing.
set(HWVAL_LINT_EXTRA_ARGS --extra-arg=-isystem
    --extra-arg=/usr/lib/arm-none-eabi/include)
add_custom_target(clang-tidy
    COMMAND ${CLANG_TIDY} -quiet -p ${CMAKE_BINARY_DIR} ${HWVAL_LINT_FLAGS}
            ${HWVAL_LINT_EXTRA_ARGS} ${HWVAL_LINT_SOURCES}
    COMMENT "Running clang-tidy over the project sources"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM)

set(ELF "$<TARGET_FILE:hwval.elf>")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# hwval.img is the slot image (256-byte BOOT header + hwval.bin) that bootctl
# uploads; BOOT accepts it only after it is signed (tools/mkimage.py --sign in
# the bootloader repository).
add_custom_command(TARGET hwval.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary  ${ELF} ${CMAKE_BINARY_DIR}/hwval.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex    ${ELF} ${CMAKE_BINARY_DIR}/hwval.hex
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/mkimage.py
            --input ${CMAKE_BINARY_DIR}/hwval.bin
            --output ${CMAKE_BINARY_DIR}/hwval.img
            --version ${HWVAL_VERSION}
            --commit "${HWVAL_GIT_COMMIT_FULL}"
            --timestamp ${HWVAL_BUILD_TIMESTAMP}
            --name hwval
    COMMAND ${SIZE_TOOL} $<TARGET_FILE:hwval.elf>
    BYPRODUCTS ${CMAKE_BINARY_DIR}/hwval.bin ${CMAKE_BINARY_DIR}/hwval.hex
               ${CMAKE_BINARY_DIR}/hwval.img
    COMMENT "Generating hwval.bin, hwval.hex and hwval.img")