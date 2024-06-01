#!/usr/bin/env python3
#
# vcp_capture.py - capture VCP bytes for a while, then print them
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Date:    2026-11-02
# Usage:   python3 scripts/vcp_capture.py [seconds] [port] [baud]
#          defaults: 5 s, first /dev/ttyACM*, 115200
# Runs on the host (pyserial); used for boot-banner and test-output checks.
#

import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed on the host: pip install pyserial")


def main() -> int:
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 5.0
    port = sys.argv[2] if len(sys.argv) > 2 else None
    baud = int(sys.argv[3]) if len(sys.argv) > 3 else 115200

    if port is None:
        import glob
        ports = sorted(glob.glob("/dev/ttyACM*"))
        if not ports:
            sys.exit("no /dev/ttyACM* found")
        port = ports[0]

    with serial.Serial(port, baud, timeout=0.25) as ser:
        end = time.monotonic() + seconds
        out = bytearray()
        while time.monotonic() < end:
            out.extend(ser.read(4096))
    sys.stdout.buffer.write(bytes(out))
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())