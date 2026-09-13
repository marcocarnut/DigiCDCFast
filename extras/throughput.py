#!/usr/bin/env python3
"""Measure the throughput of the DigiCDCFast Throughput example.

Reads from a serial port (default /dev/ttyACM0) and prints the rate in
bytes/s once per interval and on exit. The Throughput sketch sends bytes
counting 0..255 repeatedly; the script also counts places where a byte does
not follow the previous one, which would mean lost or repeated data.

Uses only the Python standard library and POSIX terminal calls, so it runs on
Linux and macOS but not on Windows.

    python3 throughput.py [PORT] [--seconds N] [--interval S] [--no-check]
"""

import argparse
import os
import select
import sys
import termios
import time
import tty


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("port", nargs="?", default="/dev/ttyACM0",
                        help="serial port (default: %(default)s)")
    parser.add_argument("--seconds", type=float, default=0,
                        help="stop after this many seconds (default: run until Ctrl-C)")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="seconds between reports (default: %(default)s)")
    parser.add_argument("--no-check", action="store_true",
                        help="do not check the 0..255 counting sequence")
    args = parser.parse_args()

    fd = os.open(args.port, os.O_RDONLY | os.O_NOCTTY)
    saved = termios.tcgetattr(fd)
    try:
        # Raw mode: no echo back to the device, no line editing or CR/LF
        # translation. Discard whatever arrived before we started.
        tty.setraw(fd)
        termios.tcflush(fd, termios.TCIFLUSH)
        measure(fd, args)
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, saved)
        os.close(fd)


def measure(fd, args):
    start = last_report = time.monotonic()
    total = interval_bytes = 0
    breaks = 0
    expected = None
    try:
        while True:
            now = time.monotonic()
            if args.seconds and now - start >= args.seconds:
                break
            ready, _, _ = select.select([fd], [], [], 0.1)
            if ready:
                data = os.read(fd, 4096)
                if not data:
                    print("port closed", file=sys.stderr)
                    break
                total += len(data)
                interval_bytes += len(data)
                if not args.no_check:
                    for b in data:
                        if expected is not None and b != expected:
                            breaks += 1
                        expected = (b + 1) & 0xFF
            now = time.monotonic()
            if now - last_report >= args.interval:
                rate = interval_bytes / (now - last_report)
                line = "%8.0f bytes/s  total %d bytes" % (rate, total)
                if not args.no_check:
                    line += "  sequence breaks %d" % breaks
                print(line, flush=True)
                last_report = now
                interval_bytes = 0
    finally:
        elapsed = time.monotonic() - start
        if elapsed > 0:
            line = "average %.0f bytes/s over %.1f s, %d bytes" % (
                total / elapsed, elapsed, total)
            if not args.no_check:
                line += ", %d sequence breaks" % breaks
            print(line)


if __name__ == "__main__":
    main()
