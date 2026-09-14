# DigiCDCFast

A USB CDC serial port (`SerialUSB`) for the [Digispark](http://digistump.com/)
(ATtiny85) and Digispark Pro (ATtiny167), built on
[V-USB](https://www.obdev.at/vusb/). It is a fork of the
DigisparkCDC library (`DigiCDC.h`) that ships with the Digistump AVR core
1.7.5, with the limits that held it to about 160 bytes/s removed and its bugs
fixed. The API is the same, apart from the fixes listed under
[Migrating from DigiCDC](#migrating-from-digicdc).

On Linux, sending 20000 bytes went from **161 bytes/s** with DigisparkCDC to
**8000 bytes/s** with DigiCDCFast, the most a low-speed USB interrupt endpoint
can carry (8 bytes every millisecond).

## What was slow, and what changed

All the changes are in `src/DigiCDCFast.cpp`. The throughput limits:

1. **`write()` waited 5 ms after every byte.** It called
   `SerialUSB.delay(5)` after queuing each byte, so no sketch could send more
   than 200 bytes/s, whatever the host did. Now `write()` queues the byte and
   services USB once. When the 64-byte transmit buffer is full it keeps
   servicing USB until there is room: a byte that `write()` rejects is lost
   (`print("...")` skips it, `print(F("..."))` stops printing). If the host
   takes nothing for 50 ms, as when no program has the port open, `write()`
   returns 0 immediately from then on until there is room again, so a sketch
   printing to nobody doesn't slow down.
2. **`refresh()` waited 1 ms before every USB poll.** `available()`, `read()`
   and `delay()` all call `refresh()`, so every call cost at least 1 ms. The
   delay is gone.
3. **A zero-length packet followed every data packet.** A zero-length packet
   is only needed after a full 8-byte packet, to tell the host the transfer
   is over; sending one after every packet halved the number of data packets
   the host could take. Now it is sent only after full packets.

The bugs:

4. **Receiving stalled in sketches that don't write.** When 8 bytes were
   waiting in the receive buffer, DigisparkCDC paused input (the host gets
   NAKs), but resumed it only when the sketch *sent* a packet. A sketch that
   only reads received 8 bytes and then nothing, forever; V-USB also refuses
   control requests while input is paused. Now input resumes as soon as a
   full packet fits in the receive buffer again, and is paused only when the
   32-byte buffer can't take another 8-byte packet.
5. **Received data could be lost while sending.** DigisparkCDC re-enabled
   input after every packet sent, but V-USB allows that only while input is
   paused: re-enabling it otherwise discards a received packet that is still
   waiting to be processed. Now it is re-enabled only when paused.
6. **`flush()` discarded received data.** It now waits until the host has
   taken everything written, as Arduino's `Stream` has done since 1.0, with
   the same 50 ms timeout as `write()`.
7. **`read()` and `peek()` returned 0 when there was nothing to read**, which
   is indistinguishable from a received zero byte. They now return -1.
8. **`begin(unsigned long)` was declared but not implemented**, so calling
   `SerialUSB.begin(9600)` failed to link. It now works; the baud rate is
   ignored, as it means nothing over USB.
9. **The host's line settings were thrown away**, and `GET_LINE_CODING`
   was answered with 7 bytes that were never filled in. They are now kept
   and reported back, and `SerialUSB.baud()` returns the bit rate the host
   set (for example with `stty`), which a USB-serial bridge needs.

10. **Output starved while the host sent continuously.** V-USB answered the
    host's IN polls with NAK whenever a received packet was still waiting for
    `usbPoll()`, and the host polls IN right after each OUT transaction. For
    endpoint 0 that is needed (the reply depends on the request), but the
    data endpoints don't depend on received data. `handleIn` in
    `src/asmcommon.inc` now checks only for endpoint 0; its timing is
    unchanged, and the data endpoints reach their reply 4 cycles sooner. In
    a USB-UART bridge on a Digispark Pro at 57600 bps in both directions at
    once, data to the host went from 41% lost to 0.33% (the rest being UART
    receive overruns).

Also:

11. `availableForWrite()` returns the free space in the transmit buffer, so a
    sketch can avoid blocking in `write()`.
12. The ring buffers used to be `static` variables defined in the header, so
   every file that included it got its own unused copy. They are now defined
   in the `.cpp`.
13. The transmit buffer is 64 bytes instead of 32.

14. `DigiCDCMedium.h` offers the same library with 2-byte USB packets, for
    sketches with tight timing of their own (see
    [Shorter USB packets](#shorter-usb-packets-digicdcmediumh)).

The header and source were renamed `DigiCDCFast.h` / `DigiCDCFast.cpp` so the
library can be installed next to the core's DigisparkCDC. The class is still
`DigiCDCDevice` and the object is still `SerialUSB`. V-USB is changed in one
place (item 10).

The git history starts with the unmodified DigisparkCDC files from the
1.7.5 core, so the later commits show exactly what changed
(`git show -M20% $(git rev-list --reverse HEAD | sed -n 2p)` for the first
set of changes).

## Tests

`extras/test/HostTest` is a sketch that runs tests on command from
`extras/test/hosttest.py` on the host. It builds against either library (see
its header comment). Results with the same board, host and test program:

| Test | DigisparkCDC | DigiCDCFast |
|------|--------------|-------------|
| `peek()` / `read()` with nothing to read | 0 / 0 | -1 / -1 |
| Send 20000 bytes | 161 bytes/s (incomplete after 30 s) | 8000 bytes/s, intact |
| Receive 2000 bytes without writing | 8 arrived, then stalled | 2000 arrived, intact |
| Echo 2000 bytes while receiving | failed* | 2000 echoed, identical |
| Write 300 bytes with nobody reading | not measured* | 54 ms, then `flush()` returns at once |

\* DigisparkCDC was still sending the 20000 bytes long after the host had
moved on to these tests, so their results say nothing.

With DigisparkCDC the board also became permanently deaf once: bytes were
left in the receive buffer, the sketch read them without writing anything,
and input was never resumed (bug 4).

Test conditions:

- Digispark (ATtiny85) at 16.5 MHz (`digistump:avr:digispark-tiny:clock=clock165`),
  Digistump AVR core 1.7.5, micronucleus 2.6 bootloader.
- Linux 6.8 on an xHCI (USB 3) host controller. DigiCDC declares bulk
  endpoints, which the USB specification does not allow on low-speed devices;
  Linux converts them to interrupt endpoints polled every 1 ms with 8-byte
  packets (the kernel logs "endpoint 0x81 is Bulk; changing to Interrupt").
- No USB errors were logged.
- The same tests pass on a Digispark Pro (ATtiny167, 16 MHz crystal) with
  the core fix described below.
- They also pass, with the same throughput and no USB errors, on a
  Raspberry Pi 5 (Raspberry Pi OS, Linux 6.12, RP1 xHCI controller), in a
  USB 2 port and both USB 3 ports, and with `DigiCDCMedium.h` (2000 bytes/s). Once a USB 3 port couldn't enumerate the
  board at all, the bootloader included (error -71); plugged in again, it
  worked. The Digispark's bare PCB plug doesn't always make good contact.
- And on a Raspberry Pi 3 Model B (Raspbian 10, Linux 4.19), where the board
  sits behind the Pi's built-in USB 2 hub on the older `dwc_otg` controller:
  both variants, 8000 and 2000 bytes/s, no USB errors.

## Interrupt latency

V-USB handles every USB transaction inside an interrupt that can't be
interrupted, so it delays every other interrupt in the sketch. Even when idle
the host polls the IN endpoint every millisecond, and each poll runs the
handler. `extras/latency/LatencyProbe` measures how late a timer interrupt
runs (3.88 us resolution, 20 s per case, `extras/latency/latency.py` on the
host); same board and host as above:

| USB activity | Delayed at all | 99% | 99.9% | Max |
|--------------|----------------|-----|-------|-----|
| Idle | 3% | 27 us | 39 us | 39 us |
| Sketch sending 8000 bytes/s | 10% | 82 us | 93 us | 97 us |
| Sketch receiving 5500 bytes/s | 10% | 93 us | 105 us | 109 us |
| Both | 14% | 97 us | 105 us | 198 us |

For timing-sensitive code this is the budget. For example, a software UART
that samples each bit in its middle after catching the start bit misreads a
byte whenever the start bit is caught more than half a bit late. That
happens to 1.4% of interrupts at 19200 bps with USB idle, and 4-6% at
9600 bps while data is flowing.

### Shorter USB packets: DigiCDCMedium.h

Most of that time is the packet itself: V-USB receives or sends every bit
with interrupts off. A transaction carrying 8 bytes keeps them off for
~110 us, one carrying 2 bytes for ~73 us. For a sketch whose own timing can't
wait that long, include `DigiCDCMedium.h` instead of `DigiCDCFast.h`:

```cpp
#include <DigiCDCMedium.h>  // 2-byte packets: at most ~2000 bytes/s each way
```

Everything else is the same. Include it in one file of the sketch only (it
defines the USB configuration descriptor, which the linker then takes over
the library's 8-byte one). For other packet sizes, 1 to 8 bytes, copy
`src/DigiCDCMedium.h` into the sketch and change the numbers. Most sketches
want `DigiCDCFast.h`; this is for cases like a software UART sharing the CPU
with V-USB. `HostTest` passes all its tests either way (2000 bytes/s with
2-byte packets).

## Digispark Pro: the core's millis interrupt must not block

On the Digispark Pro, packets sent to the host get lost unless the core is
fixed. The Digistump AVR core 1.7.5 runs `millis()` on Timer0 with a
blocking interrupt handler, `SIGNAL(TIMER0_OVF_vect)` in `cores/pro/wiring.c`.
It overflows at 976.56 Hz, beating against the host's 1000 Hz USB frames, so
every 42.7 ms it delays V-USB just as the host polls, and the transaction
fails. V-USB has already counted the data as sent, so it is lost. (The
ATtiny85 core uses a non-blocking handler for this reason.)

Change that line to

```c
ISR(TIMER0_OVF_vect, ISR_NOBLOCK)
```

Measured with usbmon while the sketch sent one byte per millisecond and the
host flooded it: 259 failed IN transactions and 1.5% of the bytes lost with
the original core, none of either with the change.

The same applies to a sketch's own interrupt handlers: any handler that
keeps interrupts off for more than a few microseconds when USB traffic
arrives makes transactions fail. Handlers that run often should mask their
own interrupt and re-enable interrupts, as the USB-UART bridges built on this
library do.

## Heavy two-way traffic

The `flood` tests in `extras/test/hosttest.py` send data to the host while
the host floods the sketch. On the Digispark Pro with the original core, the
first packet sent after heavy receiving was lost in 8 to 40 of 40 rounds, and
about 1 round in 20 lost a packet while both directions were saturated. With
the core fix above, both variants lost nothing in 40 of 40 rounds, as on an
ATtiny85 Digispark.

V-USB still marks a packet as delivered as soon as it sends it, without
waiting for the host's acknowledgement ("the rest of the driver assumes
error-free transfers anyway", `src/asmcommon.inc`), so a transaction that
fails for any reason loses its data. An attempt to wait for the ACK instead
made things worse on this host (duplicated data), so it is not included.

## Windows: does not work

Windows 10 refuses a USB CDC serial port on a low-speed device, whichever way
its data endpoints are declared. This applies to DigisparkCDC and other V-USB
serial devices as well. Tested in a Windows 10 virtual machine (VMware, USB
passthrough), with usbmon on the Linux host recording every request:

| Data endpoints | What Windows does |
|----------------|-------------------|
| Bulk (as here; the USB specification doesn't allow them at low speed) | Reads the device and configuration descriptors correctly, then rejects the configuration and resets the device, over and over: "USB device not recognized". |
| Interrupt (1 ms; an experiment, not in the library) | Configures the device and creates a COM port, but its serial driver, `usbser.sys`, refuses to start (Code 10) without sending a single request to the device. |

Every request reached the board and was answered correctly, so these are
Windows' decisions about the descriptors, not transfer problems. (Linux
treats both versions the same: it turns bulk endpoints into interrupt
endpoints itself, and all the tests pass either way.) V-USB serial projects
used a third-party filter driver on Windows: Digistump's driver package
installs `lowcdc.sys` (Osamu Tamura's Low Speed CDC Driver, 2009, signed,
64-bit) below `usbser.sys`, meant for interrupt data endpoints. On
Windows 10 it crashed the system (blue screen) as soon as the device with
interrupt endpoints was attached, with the virtual machine emulating a USB 3
controller and again with a USB 2 controller; with bulk endpoints Windows
rejects the device before the driver loads.

## Not tested

- **macOS.** Hosts that follow the USB specification strictly may refuse
  DigiCDC's low-speed bulk endpoints, as Windows does, and polling intervals,
  and so the throughput, may differ.
- Other clock settings, older USB host controllers (EHCI/OHCI/UHCI), hubs.
- `setDtrPin()` (the `CDC_DTR_LED` example).
- The `Throughput` example and `extras/throughput.py` have not been run on
  hardware; `HostTest` measured the throughput above.

Reports, good or bad, are welcome.

## Installing

Install the Digistump AVR core first (DigiCDCFast needs it; on the Digispark
Pro, with the fix above). Then either
install DigiCDCFast from the Arduino Library Manager, or download a release
ZIP and use *Sketch > Include Library > Add .ZIP Library*, or clone this
repository into your `Arduino/libraries` folder.

## Migrating from DigiCDC

Change the include:

```cpp
#include <DigiCDC.h>      // before
#include <DigiCDCFast.h>  // after
```

Don't include both in the same sketch: they define the same class, object
and USB callbacks.

Things that behave differently:

- **Printing is no longer paced at 5 ms per byte.** If your host program
  relied on that (for example a slow reader with no flow control), it now
  gets data much faster.
- **`read()` and `peek()` return -1 when there is nothing to read.** Code
  that treated 0 as "nothing" needs to check for -1, or call `available()`
  first (which works with both libraries).
- **`flush()` waits for output instead of discarding input.** To discard
  received data, use `while (SerialUSB.available()) SerialUSB.read();`.

## Usage notes

- **Call a `SerialUSB` function at least every ~10 ms.** V-USB has no
  background task: USB is serviced only inside `write()`, `print()`,
  `read()`, `peek()`, `available()`, `flush()`, `refresh()` and `delay()`. If
  your sketch does anything longer, call `SerialUSB.refresh()` in between,
  and use `SerialUSB.delay(ms)` instead of `delay(ms)`. Otherwise the host may
  reset or drop the device. (Unchanged from DigisparkCDC.)
- Call `flush()` before something that stops servicing USB (sleeping, a long
  computation, `end()`) if the data already written must arrive.
- `SerialUSB.begin()` waits 500 ms for enumeration.
- Output sent while no program has the port open on the host is lost, except
  for what fits in the buffer, which arrives when a program opens it.
- A sketch that doesn't read what the host sends eventually pauses input,
  and the host blocks writing (and opening or closing the port) until the
  sketch reads again.

## Examples

- `Echo`: sends back everything it receives.
- `Print`: prints a line over and over.
- `CDC_LED`: turns the LED on pin 1 on and off when it receives `1` or `0`.
- `CDC_DTR_LED`: drives pin 1 from the host's DTR line.
- `Throughput`: sends a continuous counting sequence; run
  `python3 extras/throughput.py /dev/ttyACM0` on the host to see bytes/s and
  check for lost bytes. The script uses only the Python standard library and
  runs on Linux (and probably macOS), not Windows.

## Projects using DigiCDCFast

- [DigisparkProBridge](https://github.com/marcocarnut/DigisparkProBridge):
  a USB-to-UART bridge for the Digispark Pro, using its hardware UART.
  Lossless up to 38400 bps in both directions at once.
- [DigisparkBridge](https://github.com/marcocarnut/DigisparkBridge): an
  experimental USB-to-UART bridge for the original Digispark, which has no
  UART: hardware oversampling with USI to receive, timer-driven bit edges to
  transmit, and `DigiCDCMedium.h`. Up to 9600 bps.

## Repository layout

```
library.properties     Arduino library metadata
src/                   DigiCDCFast.{h,cpp}, DigiCDCMedium.h, DigiCDCDescriptor.h,
                       and V-USB (with V-USB's Readme.txt and Changelog.txt)
examples/              example sketches
extras/throughput.py   host-side throughput meter
extras/test/           hardware test: HostTest sketch and hosttest.py
extras/latency/        interrupt latency probe: LatencyProbe sketch and latency.py
License.txt            V-USB license (GPL-2.0 or GPL-3.0)
CommercialLicense.txt  V-USB's commercial license terms, as distributed with V-USB
```

## License and credits

DigiCDCFast is free software under the same license as V-USB: the GNU General
Public License version 2 or version 3, at your choice. See
[License.txt](License.txt), which also has Objective Development's requests
for projects using V-USB. The commercial license in `CommercialLicense.txt` is
offered by Objective Development for V-USB only; it is kept because it is part
of the V-USB distribution.

- **V-USB** by OBJECTIVE DEVELOPMENT Software GmbH,
  <https://www.obdev.at/vusb/>.
- **DigiCDC** (DigisparkCDC) by Ihsan Kehribar (kehribar.me) and Digistump
  LLC (digistump.com), from the Digistump AVR core
  (<https://github.com/digistump/DigistumpArduino>, version 1.7.5 as
  distributed by <https://github.com/ArminJo/DigistumpArduino>).
- **DigiCDCFast** changes by Marco Carnut.
