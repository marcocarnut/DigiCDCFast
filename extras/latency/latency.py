#!/usr/bin/env python3
"""Measure interrupt latency on a Digispark running LatencyProbe.

usage: latency.py [PORT] [MODES]   (default /dev/ttyACM0, modes "itrb")
  i idle, t board sends, r board receives, b both

For each mode, prints the latency distribution and, for common UART bit
rates, how often an interrupt ran later than half a bit (plus the smallest
latency, which is constant and could be compensated): the budget for
sampling a received bit in its middle after catching the start bit.
"""
import os
import re
import select
import sys
import termios
import time
import tty

F_CPU = 16_500_000
TICK_US = 64 / F_CPU * 1e6
RUN_S = 20
NAMES = {"i": "idle", "t": "sending", "r": "receiving", "b": "sending+receiving"}
BAUDS = (4800, 9600, 19200, 38400, 57600, 115200)


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    time.sleep(0.3)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def run(fd, mode):
    report = re.compile(rb"H %c ([\d ]+)\r\n" % ord(mode))
    os.write(fd, mode.encode())
    received = sent = 0
    data = b""
    chunk = bytes(range(64))
    end = time.time() + RUN_S
    while time.time() < end:
        want_write = [fd] if mode in "rb" else []
        readable, writable, _ = select.select([fd], want_write, [], 0.05)
        if readable:
            got = os.read(fd, 4096)
            received += len(got)
            data = (data + got)[-4096:]
        if writable:
            try:
                sent += os.write(fd, chunk)
            except BlockingIOError:
                pass
    end = time.time() + 15
    while time.time() < end:
        m = report.search(data)
        if m:
            nums = [int(x) for x in m.group(1).split()]
            return nums, received / RUN_S, sent / RUN_S
        if select.select([fd], [], [], 0.2)[0]:
            got = os.read(fd, 4096)
            received += len(got)
            data = (data + got)[-8192:]
    return None, received / RUN_S, sent / RUN_S


def show(mode, nums, rx_rate, tx_rate):
    print(f"\n== {NAMES[mode]}: host received {rx_rate:.0f} bytes/s, sent {tx_rate:.0f} bytes/s")
    if nums is None:
        print("   no report")
        return
    samples, late, max_ticks, hist = nums[0], nums[1], nums[2], nums[3:]
    total = samples + late
    base = next(i for i, n in enumerate(hist) if n)

    def us(ticks):
        return ticks * TICK_US

    def percentile(p):
        target, acc = p * samples, 0
        for i, n in enumerate(hist):
            acc += n
            if acc >= target:
                return i
        return len(hist) - 1

    print(f"   {total} interrupts, {late} delayed by a whole period (>= {us(128):.0f} us)")
    print(f"   latency: min {us(base):.1f} us, median {us(percentile(0.5)):.1f},"
          f" 99% {us(percentile(0.99)):.1f}, 99.9% {us(percentile(0.999)):.1f},"
          f" max {'>= ' + format(us(128), '.0f') if late else format(us(max_ticks), '.1f')} us"
          f"{' (last bin is >= ' + format(us(len(hist) - 1), '.0f') + ' us)' if max_ticks >= len(hist) - 1 else ''}")
    print("   P(latency > min + half a bit):")
    for baud in BAUDS:
        half_bit = 0.5e6 / baud
        limit = base + int(half_bit / TICK_US)  # bins at or below are within budget
        over = sum(hist[limit + 1:]) + late
        print(f"     {baud:6d} bps (half bit {half_bit:6.1f} us): {over / total:9.2e}  ({over} of {total})")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    modes = sys.argv[2] if len(sys.argv) > 2 else "itrb"
    fd = open_port(path)
    print(f"tick {TICK_US:.2f} us, {RUN_S} s per mode")
    for mode in modes:
        show(mode, *run(fd, mode))
        termios.tcflush(fd, termios.TCIOFLUSH)
        time.sleep(1)
    os.close(fd)


if __name__ == "__main__":
    main()
