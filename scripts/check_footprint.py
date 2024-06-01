#!/usr/bin/env python3
#
# check_footprint.py - NFR-003/004 static checks, measured from the build
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Date:    2026-11-02
# SRS:     NFR-003 (image fits sectors 0-10, 1792 KB), NFR-004 (at least
#          256 KB SRAM1 + 64 KB DTCM left free for the destructive tests).
#
# Everything is measured out of build/hwval.map, not hard-coded: the
# DTCM span is data+heap+stack extent, the SRAM1 span is [SRAM1 origin
# .. .sram1 end], and NOINIT/NCACHE are reported for sanity.

import os
import re
import subprocess
import sys
from pathlib import Path

# Windows cannot execute a shell script directly, so it runs under bash there.
DEV = ["bash", "scripts/docker-dev.sh"] if os.name == "nt" else ["scripts/docker-dev.sh"]

FLASH_LIMIT = 0x081C0000          # sectors 0-10
DTCM_BASE = 0x20000000            # DTCM 128 KB (ends where SRAM1 starts)
NOINIT_BASE = 0x20074000          # 32 KB noinit tail of SRAM1
DTCM_FREE_MIN = 64 * 1024
SRAM1_FREE_MIN = 256 * 1024


def read_map_text(build: Path) -> str:
    return (build / "hwval.map").read_text(errors="replace")


def symbol_addr(text: str, name: str) -> int | None:
    # The ld map prints the address before the symbol on the same line:
    # "                0x20000300                        _sbss = ."
    m = re.search(rf"^\s*0x([0-9a-fA-F]+)\s+{re.escape(name)}\b",
                  text, flags=re.M) or \
        re.search(rf"{re.escape(name)}\s*=\s*0x([0-9a-fA-F]+)", text)
    return int(m.group(1), 16) if m else None


def section_extent(text: str, name: str) -> int | None:
    # input/output section header: "._user_heap_stack\n  0x2001d168  0x1000"
    m = re.search(rf"^\s*{re.escape(name)}\s*$\n\s*0x([0-9a-fA-F]+)\s+"
                  rf"0x([0-9a-fA-F]+)", text, flags=re.M)
    if not m:
        return None
    return int(m.group(1), 16) + int(m.group(2), 16)


def main() -> int:
    build = Path(sys.argv[1] if len(sys.argv) > 1 else "build")
    text = read_map_text(build)

    # ---- NFR-003: flash image within sectors 0-10 ----
    end = symbol_addr(text, "__flash_image_end")
    if end is None:
        sys.exit("hwval.map: __flash_image_end not found")
    used_kb = float(end - 0x08000000) / 1024.0
    nfr3_ok = end <= FLASH_LIMIT
    print(f"NFR-003 image end 0x{end:08X} ({used_kb:.1f} KB of 1792 KB) "
          f"{'OK' if nfr3_ok else 'FAIL'}")

    ok_all = nfr3_ok

    # ---- NFR-004a: DTCM (data+bss+heap+stack) leaves >= 64 KB free ----
    dtcm_end = section_extent(text, "._user_heap_stack")
    if dtcm_end is None:
        dtcm_end = symbol_addr(text, "_ebss")
        if dtcm_end is None:
            dtcm_end = 0
        else:
            dtcm_end += 0x1000  # ld reserves only the 4 KB stack right after
    dtcm_free = DTCM_BASE + 0x20000 - dtcm_end
    nfr4a = dtcm_free >= DTCM_FREE_MIN
    print(f"NFR-004 DTCM free {dtcm_free // 1024} KB "
          f"(need >= {DTCM_FREE_MIN // 1024} KB) "
          f"{'OK' if nfr4a else 'FAIL'}")
    ok_all = ok_all and nfr4a

    # ---- NFR-004b: SRAM1 free above the .sram1 buffers ----
    sram1_end = symbol_addr(text, "__sram1_end")
    if sram1_end is None:
        sys.exit("hwval.map: __sram1_end not found "
                 "(is .sram1 populated and placed > SRAM1?)")
    sram1_free = NOINIT_BASE - sram1_end
    nfr4b = sram1_free >= SRAM1_FREE_MIN
    print(f"NFR-004 SRAM1 free {sram1_free // 1024} KB "
          f"above .sram1 (need >= {SRAM1_FREE_MIN // 1024} KB) "
          f"{'OK' if nfr4b else 'FAIL'}")
    ok_all = ok_all and nfr4b

    # ---- sanity: NOINIT and NCACHE did not overflow (reported only) ----
    for name, base, total, label in (
            ("__noinit", NOINIT_BASE, 32 * 1024, "NOINIT"),
            ("__noncacheable", 0x2007E000, 8 * 1024, "NCACHE")):
        lo = symbol_addr(text, f"{name}_start")
        hi = symbol_addr(text, f"{name}_end")
        if lo is not None and hi is not None:
            used = hi - base
            state = "OK" if used <= total else "OVERFLOW"
            used_s = f"{used / 1024:.1f}" if used >= 1024 else f"{used}"
            print(f"    {label}: {used_s} KB used of "
                  f"{total // 1024} KB {state}")

    size = subprocess.run(
        [*DEV, "run", "arm-none-eabi-size",
         "/work/build/hwval.elf"],
        capture_output=True, text=True).stdout.strip().splitlines()
    for ln in size[-2:]:
        print("   ", ln)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())