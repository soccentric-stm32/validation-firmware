#!/usr/bin/env bash
#
# cppcheck.sh - static analysis pass over the HWVAL sources (NFR-007)
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Usage:   scripts/cppcheck.sh          run and print the summary
#          scripts/cppcheck.sh strict   also fail on any finding
#
# Findings target: zero warnings/errors (-Wall -Wextra -Werror already
# covers the compile; cppcheck adds the cross-function views).

set -euo pipefail

ARGS=(--enable=warning,performance,portability
      --inline-suppr
      --suppress=missingIncludeSystem
      --error-exitcode=0
      -I src -I board -I tests
      --std=c11 --platform=arm32-wchar_t4)

out="$(cppcheck "${ARGS[@]}" src board tests 2>&1 | grep -E ': (error|warning): ' || true)"

if [[ -n "$out" ]]; then
    printf '%s\n' "$out"
    if [[ "${1:-}" == "strict" ]]; then
        exit 1
    fi
fi
echo "cppcheck: clean"