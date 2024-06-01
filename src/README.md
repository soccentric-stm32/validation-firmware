# Firmware source layout

`src/` is the only include root (`include_directories(src ...)` in
`CMakeLists.txt`). Every project include is prefixed with its layer, for
example:

```c
#include "app/test_core.h"
#include "bsp/clock.h"
#include "services/fault.h"
#include "protocol/hwlink.h"
```

| Directory | Responsibility |
|---|---|
| `app/` | Application entry point (`main.c`), CLI, test framework and test tables (`test_core.c`), harness detection, runtime settings, version banner and the bootloader slot contract. |
| `bsp/` | Board bring-up and peripheral drivers: clock tree, debug UART, USART3, IWDG, reset cause, MPU/cache setup, HAL time base, newlib syscalls and the sector-11-only flash wrapper. |
| `services/` | Cross-cutting services: fault handlers, FreeRTOS hooks/task creation and the LwIP wrapper used by the Ethernet tests. |
| `protocol/` | HWLINK framing (COBS + CRC-32), the interactive/GUI link mode and the flatcc arena hooks. |
| `generated/` | flatcc headers generated from `protocol/hwlink.fbs`; committed so a host `flatcc` is not required, regenerated at build time when available. |

Test cases live in `tests/` and are linked into the firmware image. Board
headers (`hwval_board.h`, `hwval_board_pins.h`, `hwval_limits.h`,
`hwval_sections.h`), the HAL/FreeRTOS/LwIP configuration and the LwIP arch port
live in `board/`.

Adding a source file: put it in the layer it belongs to, add the `.c` to
`PROJECT_SOURCES` in `CMakeLists.txt`, and include its header with the layer
prefix.
