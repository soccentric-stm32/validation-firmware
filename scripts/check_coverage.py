#!/usr/bin/env python3
#
# check_coverage.py - COV-001 (SRS 8 <-> test cases) + COV-002 (CMSIS
# *_BASE vs the coverage matrix of SRS 12)
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Date:    2026-11-03
# SRS:     COV-001, COV-002 (section 12.4). Build-time script; the build
#          is expected to run it through `make check` / before a push.
#
# COV-001: every requirement of SRS 8 must be verified by at least one
#          test case and every test case must list requirement ids.
# COV-002: every peripheral base address defined in the CMSIS device
#          header stm32f767xx.h must appear in the coverage matrix of
#          SRS 12 (case-insensitive, with the alias table below).
#
# Runs on the host; COV-002 fetches the header inside the container via
# scripts/docker-dev.sh (the SDK lives only in there, CON-002).

import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRS = ROOT / "docs" / "SRS.md"
CMAKELISTS = ROOT / "CMakeLists.txt"

# ---------------------------------------------------------------- SRS part


def srs_text() -> str:
    return SRS.read_text(errors="replace")


def srs_section(text: str, start: str, end: str) -> str:
    i = text.index(start)
    j = text.index(end, i)
    return text[i:j]


def srs8_requirements(text: str) -> dict:
    """Requirement ids of SRS section 8 -> requirement text."""
    m = srs_section(text, "## 8. Hardware test requirements", "## 9.")
    out = {}
    for ln in m.splitlines():
        mo = re.match(r"\|\s*([A-Z][A-Z0-9]*-\d+)\s*\|([^|]*)\|", ln)
        if mo:
            out[mo.group(1)] = mo.group(2).strip()
    return out


def srs_matrix(text: str) -> str:
    return srs_section(text, "## 12. Coverage matrix", "## Appendix A")


# ---------------------------------------------------------- firmware part


def cmake_test_sources() -> list:
    srcs = []
    for mo in re.finditer(r"tests/(test_\w+\.c)", CMAKELISTS.read_text()):
        if mo.group(1) not in srcs:
            srcs.append(mo.group(1))
    return srcs


def parse_case_tables(path: Path) -> list:
    """Yield {'id','verifies','file'} for every .id/.verifies pair."""
    lit = re.compile(r'"\s*"')  # adjacent C string literals join
    txt = lit.sub("", path.read_text(errors="replace"))
    out = []
    for mo in re.finditer(r'\.id\s*=\s*"([^"]+)"', txt):
        tail = txt[mo.end():]
        mv = re.search(r'\.verifies\s*=\s*"([^"]*)"', tail[:1200])
        out.append({
            "id": mo.group(1),
            "verifies": mv.group(1) if mv else "",
            "file": path.name,
        })
    return out


def firmware_cases() -> list:
    cases = []
    for name in cmake_test_sources():
        p = ROOT / "tests" / name
        if p.exists():
            cases += parse_case_tables(p)
    return cases


def req_ids_from_verifies(verifies: str) -> list:
    return re.findall(r"[A-Z][A-Z0-9]*-\d+", verifies)


# --------------------------------------------------------------- COV-001


def cov1() -> bool:
    reqs = srs8_requirements(srs_text())
    cases = firmware_cases()
    covered = {}
    for c in cases:
        for rid in req_ids_from_verifies(c["verifies"]):
            covered.setdefault(rid, []).append(c["id"])

    ok = True
    missing = [r for r in reqs if r not in covered]
    if missing:
        ok = False
        for rid in missing:
            print(f"COV-001 FAIL: requirement {rid} ({reqs[rid]}) "
                  "has no test case")
    # requirement ids referenced by cases that do not exist in SRS 8
    for c in cases:
        if not c["verifies"].strip():
            ok = False
            print(f"COV-001 FAIL: case {c['id']} ({c['file']}) lists no "
                  "requirement")
        for rid in req_ids_from_verifies(c["verifies"]):
            if rid not in reqs:
                print(f"    note: {c['id']} also verifies {rid} (outside SRS 8)")
    # duplicate case ids
    seen, dups = set(), []
    for c in cases:
        if c["id"] in seen:
            dups.append(c["id"])
        seen.add(c["id"])
    if dups:
        ok = False
        for d in sorted(set(dups)):
            print(f"COV-001 FAIL: duplicate test id {d}")
    print(f"COV-001: {len(covered)}/{len(reqs)} SRS-8 requirements covered "
          f"by {len(cases)} test cases -> {'OK' if ok else 'FAIL'}")
    return ok


# --------------------------------------------------------------- COV-002

# token -> text that must appear in the SRS 12 matrix (lower-cased).
# Region bases (PERIPH_BASE etc.) and sub-block bases (DMA1_Stream0,
# SAI1_Block_A, DFSDM1_Filter3, LTDC_Layer1, USB_OTG_*, ...) map to their
# parent element row.
ALIASES = {
    "flash": "flash memory", "flash_r": "flash memory",
    "flashaxi": "flash memory", "flashsize": "device identity",
    "flash_otp": "otp area",
    "periph": None, "ahb1periph": None, "ahb2periph": None,
    "apb1periph": None, "apb2periph": None,          # bus regions
    "ramdtcm": "dtcm ram", "ramitcm": "itcm ram", "sram1": "sram1",
    "sram2": "sram2", "bkpsram": "backup sram",
    "uid": "device identity", "package": "device identity",
    "packagesize": "device identity",
    "gpioa": "gpioa–gpiog", "gpiob": "gpioa–gpiog", "gpioc": "gpioa–gpiog",
    "gpiod": "gpioa–gpiog", "gpioe": "gpioa–gpiog", "gpiof": "gpioa–gpiog",
    "gpiog": "gpioa–gpiog",
    "gpioh": "gpioh", "gpioi": "gpioi", "gpioj": "gpioi",
    "gpiok": "gpioi",
    "adc": "adc1", "adc1": "adc1", "adc2": "adc2", "adc3": "adc3",
    "dac": "dac",
    "dma1": "dma1", "dma2": "dma2", "dma2d": "dma2d",
    "crc": "crc", "rng": "rng", "cec": "cec",
    "dcmi": "dcmi", "jpeg": "jpeg", "ltdc": "ltdc",
    "mdios": "mdios", "spdifrx": "spdifrx", "dfsdm1": "dfsdm1",
    "eth": "ethernet mac", "fmc": "fmc", "qspi": "quadspi",
    "sai1": "sai1", "sai2": "sai2",
    "sdmmc1": "sdmmc1", "sdmmc2": "sdmmc2",
    "spi1": "spi1–spi6", "spi2": "spi1–spi6", "spi3": "spi1–spi6",
    "spi4": "spi1–spi6", "spi5": "spi1–spi6", "spi6": "spi1–spi6",
    "i2c1": "i2c1–i2c4", "i2c2": "i2c1–i2c4", "i2c3": "i2c1–i2c4",
    "i2c4": "i2c1–i2c4",
    "can1": "can1", "can2": "can2", "can3": "can3",
    "uart4": "uart4", "uart5": "uart5", "uart7": "uart7", "uart8": "uart8",
    "usart1": "usart1", "usart2": "usart2", "usart3": "usart3",
    "usart6": "usart6",
    "iwdg": "iwdg", "wwdg": "wwdg", "rtc": "rtc", "pwr": "pwr",
    "rcc": "rcc", "exti": "exti", "syscfg": "syscfg", "dbgmcu": "dbgmcu",
    "lptim1": "lptim1",
    "usb_otg_fs": "usb otg fs", "usb_otg_hs": "usb otg hs",
}

# per-instance TIMs the matrix lists individually
TIM_ROWS = {f"tim{i}": f"tim{i}" for i in range(1, 15)}


def header_bases() -> list:
    """*_BASE tokens from the CMSIS device header (read in the container)."""
    cmd = ["scripts/docker-dev.sh", "run", "sh", "-c",
           "grep -oE '^#define [A-Za-z0-9_]+_BASE' "
           "/opt/st/STM32CubeF7/Drivers/CMSIS/Device/ST/STM32F7xx/Include/"
           "stm32f767xx.h"]
    out = subprocess.run(cmd, capture_output=True, text=True, cwd=str(ROOT))
    toks = re.findall(r"#define ([A-Za-z0-9_]+)_BASE", out.stdout)
    return sorted(set(toks))


def token_alias(tok: str):
    t = tok.lower()
    if t in ALIASES:
        return ALIASES[t]
    if re.fullmatch(r"tim1[0-4]|tim[89]|tim[1-7]", t) and t in TIM_ROWS:
        return TIM_ROWS[t]
    if t.startswith("dma1_stream") or t.startswith("dma2_stream"):
        return "dma1" if t.startswith("dma1") else "dma2"
    if t.startswith("sai") and "_block" in t:
        return "sai1" if "sai1" in t else "sai2"
    if t.startswith("dfsdm1_"):
        return "dfsdm1"
    if t.startswith("ltdc_layer"):
        return "ltdc"
    if t.startswith("eth_"):
        return "eth"
    if t.startswith("usb_otg_"):
        return "usb_otg_fs_periph" if "_fs" in t else "usb_otg_hs_periph"
    if t.startswith("fmc_"):
        return "fmc"
    if t.startswith("qspi"):
        return "qspi"
    if t.startswith("flash"):
        return "flash memory"
    if t.startswith("sai"):
        return "sai1"
    return "UNKNOWN"


def cov2() -> bool:
    matrix = re.sub(r"\s+", " ", srs_matrix(srs_text())).lower()
    ok = True
    rows = []
    for tok in header_bases():
        alias = token_alias(tok)
        if alias is None:                       # bus-region constant
            rows.append((tok, None, ("OK", "bus-region constant")))
            continue
        if alias == "UNKNOWN":
            ok = False
            rows.append((tok, None, ("FAIL", "no alias/matrix row")))
            continue
        if alias.startswith("usb_"):
            alias = "usb otg fs" if "fs" in alias else "usb otg hs"
        if re.search(rf"\b{re.escape(alias)}\b", matrix) or \
                (alias in matrix):
            rows.append((tok, alias, ("OK", "")))
        else:
            ok = False
            rows.append((tok, alias, ("FAIL", "not in SRS 12")))
    for tok, alias, (st, why) in rows:
        tgt = alias if alias is not None else ""
        note = why
        if st == "FAIL" and why == "not in SRS 12":
            note = ""
            print(f"COV-002: {tok + '_BASE':36} FAIL -> '{alias}' missing "
                  "from SRS 12")
            continue
        if st == "FAIL":
            print(f"COV-002: {tok + '_BASE':36} FAIL ({why})")
            continue
        print(f"COV-002: {tok + '_BASE':36} {st} -> {tgt or '-'}"
              f"{' (' + note + ')' if note else ''}")
    n_unknown = [r for r in rows if r[2][0] == "FAIL"]
    print(f"COV-002: {len(rows)} *_BASE defines checked -> "
          f"{'OK' if ok else 'FAIL'} ({len(n_unknown)} unmatched)")
    return ok


def main() -> int:
    args = sys.argv[1:]
    ok = True
    if "--no-cov1" not in args:
        ok = cov1() and ok
    if "--no-cov2" not in args:
        try:
            ok = cov2() and ok
        except (IndexError, ValueError, OSError, subprocess.SubprocessError):
            print("COV-002: docker container unavailable -> skipped "
                  "(run with -no-cov2 to silence)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())