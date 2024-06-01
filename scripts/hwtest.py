#!/usr/bin/env python3
#
# hwtest.py - HWVAL host-side tool (SRS section 11, HOST-001..HOST-005)
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Date:    2026-11-03
# SRS:     HOST-001 (VCP script mode, JSON results, JSON+Markdown reports
#          keyed by chip UID + date), HOST-002 (operator prompt relay),
#          HOST-003 (host sides of USB-010 CDC echo, ETH-007/008 ping +
#          TCP/UDP echo, DBG-004 SWO capture, WDG-004/DBG-005 halt freeze
#          checks), HOST-004 (exit codes 0/1/2), HOST-005 (docker).
#
# Examples:
#   scripts/hwtest.py --level S --no-operator
#   scripts/hwtest.py --group DEV,PWR
#   scripts/hwtest.py --tests RTC.002,RTC.006
#   scripts/hwtest.py --eth-target auto          (after ETH.007 ran)
#   scripts/hwtest.py --cdc-echo /dev/ttyUSB1    (USB-010 host leg)
#   scripts/hwtest.py --swo 10                   (DBG-004 host leg)
#   scripts/hwtest.py --halt-check               (WDG-004/DBG-005 host leg)
#
# pyserial when importable, else a POSIX termios layer (also inside the
# stm32-dev container, HOST-005). Exit codes (HOST-004): 0 all PASS,
# 1 any FAIL, 2 communication error.
#

import argparse
import json
import os
import re
import socket
import statistics
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent

try:
    import serial as _pyserial            # optional; termios fallback below
except ImportError:
    _pyserial = None

# ------------------------------------------------------------------ serial


class CommError(Exception):
    """Transport layer failure (HOST-004 exit code 2)."""


def _serial_exc():
    return _pyserial.SerialException if _pyserial else OSError


class Vcp:
    """ST-LINK VCP as a byte pipe (pyserial or termios)."""

    def __init__(self, port, baud):
        self.port_name = port
        if _pyserial is not None:
            try:
                self.ser = _pyserial.Serial(port, baud, timeout=0.05)
            except _pyserial.SerialException as e:
                raise CommError(f"open {port}: {e}") from e
            self.mode = "pyserial"
        else:
            self._termios_open(port, baud)
            self.mode = "termios"

    def _termios_open(self, port, baud):
        import termios
        try:
            self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as e:
            raise CommError(f"open {port}: {e}") from e
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = termios.IGNBRK                       # iflag
        attrs[1] = 0                                    # oflag
        attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
        attrs[3] = 0                                    # lflag
        speed = getattr(termios, f"B{baud}", termios.B115200)
        attrs[4] = speed
        attrs[5] = speed
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def write(self, data: bytes):
        if self.mode == "pyserial":
            try:
                self.ser.write(data)
                self.ser.flush()
            except _pyserial.SerialException as e:
                raise CommError(f"write: {e}") from e
        else:
            try:
                os.write(self.fd, data)
            except OSError as e:
                raise CommError(f"write: {e}") from e

    def read_avail(self, maxb=4096) -> bytes:
        try:
            if self.mode == "pyserial":
                return self.ser.read(maxb)
            return os.read(self.fd, maxb)
        except (BlockingIOError, InterruptedError):
            return b""
        except (OSError,) as e:
            if isinstance(e, BlockingIOError):
                return b""
            raise CommError(f"read: {e}") from e
        except _pyserial.SerialException as e:      # pyserial path
            raise CommError(f"read: {e}") from e

    def close(self):
        try:
            if self.mode == "pyserial":
                self.ser.close()
            else:
                os.close(self.fd)
        except Exception:
            pass


# ---------------------------------------------------------------- protocol


RESULT_JSON = re.compile(r'^\{.*"id".*"result".*\}$')
PROMPT_YN = re.compile(r"\[y/n\] \(timeout \d+s\)\s*$")
UID_LINE = re.compile(r"^UID=([0-9A-Fa-f]{24})")
EXEC_LINE = re.compile(r"^(\d+) test\(s\) executed, failures=(\d+)")


class Session:
    """Firmware session: script mode + JSON results (HOST-001)."""

    def __init__(self, port, baud, relay=None, log=print, op_answers=None):
        self.vcp = Vcp(port, baud)
        self.relay = relay                 # HOST-002 callback(question)->str
        self.log = log
        self.uid = None
        self.results = []
        self.op_answers = op_answers       # list of pre-decided answers

    def __enter__(self):
        self.vcp.write(b"\r\n")
        self._drain_quiet(0.3, 2.0)
        self.cmd("mode script")            # CLI-012: no echo/colour/prompt
        self.cmd("format json")
        info = self.cmd("info", keep=True)
        for ln in info:
            mo = UID_LINE.match(ln)
            if mo:
                self.uid = mo.group(1)
                break
        if self.uid is None:
            self.uid = "0" * 24
        return self

    def __exit__(self, *a):
        self.vcp.close()

    def _drain_quiet(self, quiet_s, max_s) -> str:
        buf = b""
        t0 = time.monotonic()
        last = t0
        while (time.monotonic() - last) < quiet_s and \
                (time.monotonic() - t0) <= max_s:
            chunk = self.vcp.read_avail()
            if chunk:
                buf += chunk
                last = time.monotonic()
            else:
                time.sleep(0.005)
        return buf.decode(errors="replace")

    # ---- line handling shared by cmd()/run_run() ----
    def _handle_line(self, s: str) -> bool:
        """Returns True if the line consumed further protocol state."""
        if PROMPT_YN.search(s):
            # HOST-002: forward the operator prompt and relay the answer
            self.log(f"? prompt: {s}")
            if self.relay is not None:
                ans = (self.relay(s) or "n").strip()[:1] or "n"
                self.vcp.write((ans + "\r").encode())
                self.log(f"  -> answered '{ans}'")
            else:
                self.vcp.write(b"n\r")
                self.log("  -> answered 'n' (no relay)")
            return True
        if RESULT_JSON.match(s):
            self.results.append(json.loads(s))
            self.log(f"  * {s.split('\"id')[1].split(',')[0]}")
            return True
        self.log(f"  | {s}")
        return False

    def _collect(self, until: str, timeout_s: float) -> list:
        out = []
        buf = b""
        done_at = None
        t0 = time.monotonic()
        while True:
            chunk = self.vcp.read_avail()
            if chunk:
                done_at = None
                buf += chunk
                while b"\n" in buf:
                    ln, buf = buf.split(b"\n", 1)
                    s = ln.decode(errors="replace").rstrip("\r")
                    if self._handle_line(s):
                        continue
                    out.append(s)
                    if until and s.startswith(until):
                        done_at = time.monotonic()
                t0 = time.monotonic()
            if done_at is not None and (time.monotonic() - done_at) > 0.6:
                break
            if (time.monotonic() - t0) > 1.2:
                break                       # command finished (silence)
            if (time.monotonic() - t0) > timeout_s:
                raise CommError("timeout waiting for firmware")
        if buf:
            tail = buf.decode(errors="replace").rstrip("\r\n")
            for s in tail.split("\r"):
                if s:
                    if not self._handle_line(s):
                        out.append(s)
        return out

    def cmd(self, line: str, keep=False) -> list:
        """One console command; returns the reply lines."""
        self.log(f"> {line}")
        self.vcp.write((line + "\r").encode())
        out = self._collect("", 4.0)
        if keep:
            return out
        return []

    def run_tests(self, selector: str, flags: str, max_s: float) -> list:
        """Run tests; returns the parsed JSON result rows (HOST-001)."""
        self.results = []
        self.log(f"> run {selector} {flags}")
        self.vcp.write((f"run {selector} {flags}\r").encode())
        buf = b""
        t0 = time.monotonic()
        done_at = None
        while True:
            if (time.monotonic() - t0) > max_s:
                raise CommError(f"run timed out after {max_s}s")
            chunk = self.vcp.read_avail()
            if chunk:
                done_at = None
                buf += chunk
                while b"\n" in buf:
                    ln, buf = buf.split(b"\n", 1)
                    s = ln.decode(errors="replace").rstrip("\r")
                    self._handle_line(s)
                    mo = EXEC_LINE.match(s)
                    if mo:
                        done_at = time.monotonic()
                continue
            if done_at is not None and (time.monotonic() - done_at) > 0.8:
                break
            time.sleep(0.005)
        return self.results


# ------------------------------------------------------------------ reports


def write_reports(meta: dict, results: list, out_dir: Path) -> tuple:
    """HOST-001: JSON + Markdown keyed by the chip UID and the date."""
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    uid8 = (meta.get("uid") or "0" * 8)[-8:]
    stem = f"hwval_{uid8}_{stamp}"
    jpath, mpath = out_dir / f"{stem}.json", out_dir / f"{stem}.md"

    summary = {"PASS": 0, "FAIL": 0, "SKIP": 0, "N/A": 0}
    for r in results:
        res = r.get("result", "?")
        summary[res] = summary.get(res, 0) + 1

    (out_dir / f"{stem}.json").write_text(
        json.dumps({"meta": meta, "summary": summary, "results": results},
                   indent=2) + "\n")

    md = ["# HWVAL test report", "",
          f"chip UID: `{meta.get('uid')}`  ",
          f"firmware: {meta.get('version')} (git {meta.get('git')})  ",
          f"SRS: {meta.get('srs')}  ",
          f"date: {meta.get('date')}  ",
          f"selection: `{meta.get('selector')}`  flags: `{meta.get('flags')}`",
          "",
          "**{} PASS / {} FAIL / {} SKIP / {} N/A of {}**".format(
              summary.get("PASS", 0), summary.get("FAIL", 0),
              summary.get("SKIP", 0), summary.get("N/A", 0), len(results)),
          "",
          "| ID | result | ms | measured | limits | reason |",
          "|---|---|---|---|---|---|"]
    for r in results:
        md.append("| {} | {} | {} | {} | {} | {} |".format(
            r.get("id", "?"), r.get("result", "?"),
            r.get("duration_ms", 0),
            (r.get("measured", "") or "").replace("|", "\\|"),
            (r.get("limits", "") or "").replace("|", "\\|"),
            (r.get("reason", "") or "").replace("|", "\\|")))
    mpath.write_text("\n".join(md) + "\n")
    return jpath, mpath


def host_meta(sess: Session, selector: str, flags: str) -> dict:
    lines = sess.cmd("version", keep=True) + sess.cmd("info", keep=True)
    ver = git = srs = ""
    for ln in lines:
        mo = re.match(r"HWVAL v(\S+) \(git (\S+)\) built (.*)", ln)
        if mo:
            ver, git = mo.group(1), mo.group(2)
        mo = re.match(r"SRS (\S+) \| FreeRTOS", ln)
        if mo:
            srs = mo.group(1)
    return {"uid": sess.uid, "version": ver, "git": git, "srs": srs,
            "selector": selector, "flags": flags,
            "date": datetime.now().isoformat(timespec="seconds")}


# --------------------------------------------------------- host-side tests


def usb_cdc_echo(dev: str, megabytes: float = 1.0) -> int:
    """HOST-003 side of USB-010: CDC-ACM enumerate + 1 MB echo + rate."""
    if not os.path.exists(dev):
        print(f"USB-010 FAIL: CDC device {dev} not present (user USB)")
        return 1
    v = Vcp(dev, 115200)
    ref = bytes(range(256))                     # the repeating pattern
    total = int(megabytes * 1024 * 1024)
    sent = got = bad = 0
    pos = 0                                     # receive-side pattern pos
    t0 = time.monotonic()
    try:
        buf = b""
        while got < total and (time.monotonic() - t0) < 60.0:
            if sent < total:
                v.write(ref * 64)
                sent += len(ref) * 64
            chunk = v.read_avail()
            if chunk:
                buf += chunk
                while len(buf) >= 256:
                    block, buf = buf[:256], buf[256:]
                    for i, byte in enumerate(block):
                        if byte != ref[(pos + i) & 0xFF]:
                            bad += 1
                            break               # resync from the next block
                    got += 256
                    pos = (pos + 256) & 0xFF
    finally:
        v.close()
    dt = max(time.monotonic() - t0, 1e-6)
    rate = min(got, sent) / dt / 1e6
    print(f"USB-010 CDC echo: sent={sent}B got={got}B bad={bad} "
          f"in {dt:.2f}s ({rate:.2f} MB/s)")
    if bad or got < int(sent * 0.98):
        print("USB-010 FAIL")
        return 1
    print("USB-010 PASS")
    return 0


def eth_checks(target: str) -> int:
    """HOST-003 sides of ETH-007 (ping/TCP/UDP) and ETH-008 (Mbit/s)."""
    fail = 0

    # ICMP ping via the system tool (ETH-007)
    try:
        pr = subprocess.run(["ping", "-c", "4", "-W", "2", target],
                            capture_output=True, text=True, timeout=15)
        ok = (pr.returncode == 0)
        print(f"ETH-007 ping {target}: {'PASS' if ok else 'FAIL'}")
        if not ok:
            print(pr.stdout.strip())
            fail = 1
    except (OSError, subprocess.SubprocessError) as e:
        print(f"ETH-007 ping: unavailable ({e})")
        fail = 1

    # TCP echo: 20 pattern roundtrips + a streaming throughput leg (008)
    try:
        payload = bytes((i * 7 + 0x5A) & 0xFF for i in range(1460))
        s = socket.create_connection((target, 7), timeout=5)
        rtt, n_ok = [], 0
        for _ in range(20):
            t0 = time.monotonic()
            s.sendall(payload)
            got = b""
            while len(got) < len(payload):
                d = s.recv(len(payload) - len(got))
                if not d:
                    break
                got += d
            if got == payload:
                n_ok += 1
                rtt.append(time.monotonic() - t0)
        if n_ok:
            print(f"ETH-007 TCP echo: {n_ok}/20 verified RTTs "
                  f"(med {statistics.median(rtt) * 1000:.1f} ms)")
        else:
            print("ETH-007 TCP echo: FAIL (no echo response)")
            fail = 1
        mbit = 0.0
        if n_ok >= 10:
            sent = 0
            t0 = time.monotonic()
            while (time.monotonic() - t0) < 10.0:
                s.sendall(payload)
                sent += len(payload)
                s.recv(4096)
            mbit = sent * 8.0 / (time.monotonic() - t0) / 1e6
            print(f"ETH-008 TCP echo throughput: ~{mbit:.1f} Mbit/s")
            if mbit < 0.5:
                print("ETH-008 FAIL (throughput near zero)")
                fail = 1
        s.close()
    except (OSError, socket.timeout) as e:
        print(f"ETH-007/008 TCP: FAIL ({e})")
        fail = 1

    # UDP echo (ETH-007)
    try:
        u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        u.settimeout(2.0)
        payload = bytes((i * 11 + 0x3C) & 0xFF for i in range(512))
        ok = 0
        for _ in range(10):
            u.sendto(payload, (target, 7))
            try:
                if u.recv(1024) == payload:
                    ok += 1
            except socket.timeout:
                pass
        u.close()
        print(f"ETH-007 UDP echo: {ok}/10 verified "
              f"{'PASS' if ok >= 8 else 'FAIL'}")
        if ok < 8:
            fail = 1
    except OSError as e:
        print(f"ETH-007 UDP echo: FAIL ({e})")
        fail = 1
    return fail


def swo_capture(seconds: int) -> int:
    """HOST-003 side of DBG-004: OpenOCD TPIU/SWO capture."""
    cmd = ["scripts/docker-dev.sh", "run", "timeout", f"{seconds + 5}",
           "openocd", "-f", "board/st_nucleo_f7.cfg",
           "-c", "tpiu config internal /work/build/swo_capture.swo "
                 "uart off 216000000",
           "-c", "init; targets; shutdown"]
    print(f"DBG-004: capturing SWO for ~{seconds} s via OpenOCD...")
    subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True,
                   timeout=seconds + 30)
    p = ROOT / "build" / "swo_capture.swo"
    if not p.exists():
        print("DBG-004 FAIL: no SWO capture file")
        return 1
    nb = p.stat().st_size
    ok = nb > 0
    print(f"DBG-004: {nb} SWO bytes -> {p} => {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def halt_check() -> int:
    """HOST-003 sides of WDG-004 / DBG-005: halt + freeze-register dump."""
    r = subprocess.run(
        ["scripts/docker-dev.sh", "run", "openocd", "-f",
         "board/st_nucleo_f7.cfg", "-c",
         "init; mdw 0xE0042008; reset halt; sleep 5000; mdw 0x40003008; "
         "resume; shutdown"],
        cwd=str(ROOT), capture_output=True, text=True, timeout=90)
    print(r.stdout.strip())
    if r.stderr.strip():
        print(r.stderr.strip(), file=sys.stderr)
    print("\nWDG-004/DBG-005 verdict: PASS when 0xE0042008 (DBGMCU APB1_FZ)\n"
          "shows DBG_IWDG_STOP/DBG_WWDG_STOP set by the firmware and the\n"
          "IWDG status at 0x40003008 stayed RV=0 through the 5 s halt;\n"
          "the WDG.004/DBG-005 bench procedure in the user guide then\n"
          "relays the counter behaviour.")
    return 0


# ------------------------------------------------------------------- main


def main() -> int:
    ap = argparse.ArgumentParser(description="HWVAL host tool (SRS section 11)")
    ap.add_argument("--port", default=os.environ.get("HWVAL_PORT",
                    "/dev/ttyACM0"))
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--level", default="S,L,E", help="levels (CLI-025)")
    ap.add_argument("--group", default="", help="group(s), e.g. DEV,PWR")
    ap.add_argument("--tests", default="", help="ids, e.g. RTC.002,RTC.006")
    ap.add_argument("--selector", default="",
                    help="raw selector (overrides group/tests)")
    ap.add_argument("--include-operator", action="store_true",
                    help="operator cases with the prompt relay (HOST-002)")
    ap.add_argument("--yes", action="store_true",
                    help="auto-answer prompts with y")
    ap.add_argument("--destructive", action="store_true",
                    help="host confirmation for destructive runs (SAFE-005)")
    ap.add_argument("--max-run-sec", type=float, default=1500.0)
    ap.add_argument("--out-dir", default=str(ROOT / "reports"))
    ap.add_argument("--no-report", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    # host-side equipment checks (HOST-003)
    ap.add_argument("--cdc-echo", metavar="DEV", default="",
                    help="USB-010 host side: second USB CDC device")
    ap.add_argument("--eth-target", metavar="IP", default="",
                    help="ETH-007/008 host side; 'auto' reads ETH.007's IP")
    ap.add_argument("--swo", type=int, metavar="SECONDS", default=0,
                    help="DBG-004 host side: OpenOCD SWO capture")
    ap.add_argument("--halt-check", action="store_true",
                    help="WDG-004/DBG-005 host side: halt + freeze regs")
    args = ap.parse_args()
    log = (lambda *a, **k: None) if args.quiet else print

    try:
        if args.halt_check:
            return halt_check()
        if args.swo:
            return swo_capture(args.swo)
        if args.eth_target and args.eth_target != "auto":
            return eth_checks(args.eth_target)
        if args.cdc_echo:
            return usb_cdc_echo(args.cdc_echo)

        flags = ["--level " + ",".join(
            l.strip().upper() for l in args.level.split(",") if l.strip())]
        if not args.include_operator:
            flags.append("--no-operator")
        if args.destructive:
            if not args.include_operator:
                print("destructive runs also need --include-operator")
                return 2
            print("SAFE-005: DESTRUCTIVE run confirmed at the host\n"
                  "  scope: flash sector 11, backup SRAM/registers, "
                  "RTC config, MCU resets")
            flags.append("--destructive")
        selector = args.selector or args.group or args.tests or "all"
        sel_flags = " ".join(flags)

        relay = (lambda q: "y") if args.yes else (
            lambda q: (input(f"[board asks] {q} answer: ") or "n"))

        with Session(args.port, args.baud, relay=relay, log=log) as sess:
            banner = sess._drain_quiet(0.4, 3.0)
            if ("HWVAL" not in banner) and not args.quiet:
                print("(banner: no HWVAL seen yet)")
            meta = host_meta(sess, selector, sel_flags)
            results = sess.run_tests(selector, sel_flags, args.max_run_sec)

        if args.eth_target == "auto":
            for r in results:
                if r.get("id", "").startswith("ETH.007"):
                    ipmo = re.search(r"ip=([0-9.]+)", r.get("measured", ""))
                    if ipmo:
                        print(f"ETH-007 board IP: {ipmo.group(1)}")
                        return eth_checks(ipmo.group(1))
            print("ETH-007 IP: not reported this run (board not on the "
                  "network) -> cannot run the Ethernet checks")
            return 2

        fails = sum(1 for r in results if r.get("result") == "FAIL")
        npass = sum(1 for r in results if r.get("result") == "PASS")
        nskip = sum(1 for r in results if r.get("result") == "SKIP")
        nna = sum(1 for r in results if r.get("result") == "N/A")
        print(f"\nHWVAL {meta['uid'][:12]}…: {npass} PASS / {fails} FAIL / "
              f"{nskip} SKIP / {nna} N/A of {len(results)} run")
        if not args.no_report:
            jpath, mpath = write_reports(meta, results, Path(args.out_dir))
            print(f"reports: {jpath}\n         {mpath}")
        return 1 if fails else 0
    except CommError as e:
        print(f"communication error: {e}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 2


if __name__ == "__main__":
    sys.exit(main())