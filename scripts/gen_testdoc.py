#!/usr/bin/env python3
#
# gen_testdoc.py - NFR-010: generate the test catalogue from the test
# tables (single source of truth: the firmware tables)
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Date:    2026-11-03
# SRS:     NFR-010 ("a user guide shall describe the CLI, every test
#          (generated from the test table), the harness wiring and the
#          external equipment setup") + TFW-002.
#
# Parses the same source files the build uses (tests/test_*.c from
# CMakeLists.txt), joins string literals, and regenerates
# docs/TEST_CATALOGUE.md. Run after adding tests:
#
#   python3 scripts/gen_testdoc.py
#

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "TEST_CATALOGUE.md"

LEVEL = {"S": "S", "L": "L", "E": "E"}


def cmake_test_sources():
    srcs = []
    for mo in re.finditer(r"tests/(test_\w+\.c)",
                          (ROOT / "CMakeLists.txt").read_text()):
        if mo.group(1) not in srcs:
            srcs.append(mo.group(1))
    return srcs


def parse_cases(path: Path):
    txt = re.sub(r'/\*.*?\*/', "", path.read_text(errors="replace"),
                 flags=re.S)                                # C comments
    txt = re.sub(r'"\s*"', "", txt)                        # literals
    out = []
    for mo in re.finditer(r'\.id\s*=\s*"([^"]+)"', txt):
        tail = txt[mo.end():]
        mv = re.search(r'\.verifies\s*=\s*"([^"]*)"', tail[:1200])
        ml = re.search(r"\.level\s*=\s*TFW_LEVEL_([SLE])", tail[:400])
        mt = re.search(r'\.title\s*=\s*"([^"]+)"', tail[:500])
        mg = re.search(r'\.group\s*=\s*"([^"]+)"', tail[:500])
        mop = re.search(r"\.needs_operator\s*=\s*1", tail[:400])
        mde = re.search(r"\.destructive\s*=\s*1", tail[:400])
        mrst = re.search(r"\.resets_mcu\s*=\s*1", tail[:400])
        mto = re.search(r"\.timeout_ms\s*=\s*(\d+)", tail[:400])
        out.append({
            "id": mo.group(1),
            "group": (mg.group(1) if mg else ""),
            "title": (mt.group(1) if mt else "").replace("|", "/").strip(),
            "level": LEVEL.get(ml.group(1) if ml else "S", "?"),
            "operator": bool(mop),
            "destructive": bool(mde),
            "resets": bool(mrst),
            "timeout": int(mto.group(1)) if mto else 0,
            "verifies": mv.group(1) if mv else "",
            "file": path.name,
        })
    return out


def main() -> int:
    cases = []
    groups = []
    for name in cmake_test_sources():
        p = ROOT / "tests" / name
        if p.exists():
            cs = parse_cases(p)
            cases += cs
            for c in cs:
                if c["group"] not in groups:
                    groups.append(c["group"])

    lines = [
        "# HWVAL test catalogue", "",
        "Generated from the firmware test tables (`tests/test_*.c`)",
        "by `scripts/gen_testdoc.py` (NFR-010, TFW-002). Do not edit",
        "by hand; rerun the generator after changing the tables.", "",
        f"{len(cases)} test cases.", "",
    ]
    for g in sorted(groups):
        sub = [c for c in cases if c["group"] == g]
        lines += [f"## {g} ({len(sub)} cases)", "",
                  "| ID | title | level | operator | destructive | "
                  "resets | timeout | verifies |",
                  "|---|---|---|---|---|---|---|---|"]
        for c in sub:
            lines.append(
                "| {} | {} | {} | {} | {} | {} | {} ms | {} |".format(
                    c["id"], c["title"], c["level"],
                    "op" if c["operator"] else "-",
                    "DESTRUCTIVE" if c["destructive"] else "-",
                    "reset" if c["resets"] else "-",
                    c["timeout"], c["verifies"]))
        lines.append("")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines))
    print(f"wrote {OUT.relative_to(ROOT)} ({len(cases)} cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())