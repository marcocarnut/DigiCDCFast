#!/usr/bin/env python3
"""Run the DigiCDCFast hardware tests against a board running HostTest.

usage: hosttest.py [PORT] [FLOOD_ROUNDS]   (default /dev/ttyACM0; Linux/POSIX only)

With FLOOD_ROUNDS, runs only the flood-then-send test, that many rounds per
variant, and prints which bytes each round lost.
"""
import os
import re
import select
import sys
import termios
import time
import tty

PATTERN = bytes(i & 0xFF for i in range(2000))
STREAM = bytes(i & 0xFF for i in range(20000))
FLOOD_CHUNK = b"U" * 64  # not a HostTest command
FLOOD_PATTERN = bytes(0x80 + i for i in range(120))


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    time.sleep(0.3)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def write_all(fd, data, timeout=5):
    """Write data, giving up after timeout seconds without progress (a device
    that stops accepting input would otherwise block forever)."""
    sent = 0
    while sent < len(data):
        _, ready, _ = select.select([], [fd], [], timeout)
        if not ready:
            return sent
        sent += os.write(fd, data[sent:sent + 64])
    return sent


def read_some(fd, timeout):
    ready, _, _ = select.select([fd], [], [], timeout)
    return os.read(fd, 4096) if ready else b""


def read_report(fd, test, timeout, data=b""):
    """Read until a line 'test a b c' arrives; return (numbers, bytes before it)."""
    pattern = re.compile(rb"%c (-?\d+) (-?\d+) (-?\d+)\r\n" % ord(test))
    end = time.time() + timeout
    while time.time() < end:
        m = pattern.search(data)
        if m:
            return [int(x) for x in m.groups()], data[:m.start()]
        data += read_some(fd, 0.2)
    return None, data


def drain_input(fd):
    while read_some(fd, 0.3):
        pass


def check(name, ok, detail):
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}", flush=True)
    return ok


def test_peek_read(fd):
    os.write(fd, b"P")
    nums, _ = read_report(fd, "P", 5)
    if nums is None:
        return check("peek/read when empty", False, "no report")
    return check("peek/read when empty", nums[:2] == [-1, -1],
                 f"peek() = {nums[0]}, read() = {nums[1]} (want -1, -1)")


def test_receive_only(fd):
    os.write(fd, b"R")
    time.sleep(0.1)
    sent = write_all(fd, PATTERN, timeout=1)
    if sent < len(PATTERN):
        # don't let the rest reach the sketch later as bogus commands
        termios.tcflush(fd, termios.TCOFLUSH)
    nums, _ = read_report(fd, "R", 10)
    if nums is None:
        return check("receive without writing", False,
                     f"no report; host could send only {sent} of {len(PATTERN)} bytes")
    return check("receive without writing", nums[:2] == [len(PATTERN), 0],
                 f"{nums[0]} of {len(PATTERN)} bytes arrived, {nums[1]} wrong")


def test_echo(fd):
    os.write(fd, b"E")
    time.sleep(0.1)
    echoed = b""
    for i in range(0, len(PATTERN), 64):
        if write_all(fd, PATTERN[i:i + 64], timeout=1) < len(PATTERN[i:i + 64]):
            termios.tcflush(fd, termios.TCOFLUSH)
            break
        echoed += read_some(fd, 0.01)
    nums, echoed = read_report(fd, "E", 10, echoed)
    if nums is None:
        return check("echo while receiving", False,
                     f"no report; {len(echoed)} bytes echoed")
    return check("echo while receiving", nums[0] == len(PATTERN) and echoed == PATTERN,
                 f"device got {nums[0]} bytes, host got {len(echoed)} back,"
                 f" {'identical' if echoed == PATTERN else 'DIFFERENT'}")


def test_throughput(fd):
    os.write(fd, b"T")
    data, start = b"", None
    end = time.time() + 30
    while len(data) < len(STREAM) and time.time() < end:
        chunk = read_some(fd, 0.5)
        if chunk and start is None:
            start = time.time()
        data += chunk
    elapsed = time.time() - (start or time.time())
    nums, rest = read_report(fd, "T", 5, data[len(STREAM):])
    data = data[:len(STREAM)]
    rate = len(data) / elapsed if elapsed > 0 else 0
    return check("throughput", data == STREAM and nums is not None,
                 f"{len(data)} bytes {'intact' if data == STREAM else 'CORRUPTED/INCOMPLETE'},"
                 f" {rate:.0f} bytes/s")


def test_host_stalled(path, fd):
    os.write(fd, b"W")
    time.sleep(0.2)
    os.close(fd)  # nobody reads while the board writes 300 bytes
    time.sleep(3)
    fd = open_port(path)
    drain_input(fd)
    os.write(fd, b"?")
    nums, _ = read_report(fd, "W", 5)
    if nums is None:
        return check("write with nobody reading", False, "no report"), fd
    write_ms, flush_ms, accepted = nums
    ok = write_ms < 500 and flush_ms < 200
    return check("write with nobody reading", ok,
                 f"300 writes took {write_ms} ms ({accepted} accepted),"
                 f" flush() took {flush_ms} ms"), fd


def flood_round(fd, keep_flooding):
    """One 'F' round; returns (missing pattern indexes, extra bytes, accepted)
    or None if no report arrived."""
    report = re.compile(rb"F (-?\d+) (-?\d+) (-?\d+)\r\n")
    os.write(fd, b"F")
    start = time.time()
    data = b""
    stopped = False
    while True:
        elapsed = time.time() - start
        flooding = elapsed < 1.2 or keep_flooding
        if not flooding and not stopped:
            termios.tcflush(fd, termios.TCOFLUSH)  # quiet well before the board sends
            stopped = True
        readable, writable, _ = select.select([fd], [fd] if flooding else [], [], 0.02)
        if readable:
            data += os.read(fd, 4096)
        if writable:
            try:
                os.write(fd, FLOOD_CHUNK)
            except BlockingIOError:
                pass
        m = report.search(data)
        if m or elapsed > 12:
            break
    if keep_flooding:
        termios.tcflush(fd, termios.TCOFLUSH)
    if not m:
        return None
    got = data[:m.start()]
    missing = [i for i, b in enumerate(FLOOD_PATTERN) if b not in got]
    return missing, len(got) - (len(FLOOD_PATTERN) - len(missing)), int(m.group(1))


def test_flood_then_send(fd, keep_flooding, rounds, verbose=False):
    name = ("send while the host floods" if keep_flooding
            else "send after the host flooded")
    lossy, lost, bad = 0, 0, []
    for r in range(rounds):
        result = flood_round(fd, keep_flooding)
        time.sleep(0.2)
        drain_input(fd)
        if result is None:
            bad.append(r)
            if verbose:
                print(f"   round {r}: no report", flush=True)
            continue
        missing, extra, accepted = result
        if missing or extra or accepted != len(FLOOD_PATTERN):
            lossy += 1
            lost += len(missing)
        if verbose and (missing or extra or accepted != len(FLOOD_PATTERN)):
            print(f"   round {r}: missing {missing}, {extra} extra bytes,"
                  f" write() accepted {accepted}", flush=True)
    return check(name, lossy == 0 and not bad,
                 f"{rounds} rounds, {lossy} with losses ({lost} bytes),"
                 f" {len(bad)} without report")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    fd = open_port(path)
    drain_input(fd)
    if len(sys.argv) > 2:
        rounds = int(sys.argv[2])
        results = [test_flood_then_send(fd, True, rounds, verbose=True),
                   test_flood_then_send(fd, False, rounds, verbose=True)]
        os.close(fd)
        sys.exit(0 if all(results) else 1)
    results = [test_peek_read(fd), test_throughput(fd)]
    ok, fd = test_host_stalled(path, fd)
    results.append(ok)
    # last: with the original DigisparkCDC these can leave the board deaf
    results += [test_echo(fd), test_receive_only(fd),
                test_flood_then_send(fd, True, 5), test_flood_then_send(fd, False, 5)]
    os.close(fd)
    print(f"{sum(results)} of {len(results)} tests passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
