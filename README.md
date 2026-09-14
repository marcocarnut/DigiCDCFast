# DigiCDCFast

A USB CDC serial port (`SerialUSB`) for the [Digispark](http://digistump.com/)
(ATtiny85), built on [V-USB](https://www.obdev.at/vusb/). It is a fork of the
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

Also:

10. `availableForWrite()` returns the free space in the transmit buffer, so a
    sketch can avoid blocking in `write()`.
11. The ring buffers used to be `static` variables defined in the header, so
   every file that included it got its own unused copy. They are now defined
   in the `.cpp`.
12. The transmit buffer is 64 bytes instead of 32.

The header and source were renamed `DigiCDCFast.h` / `DigiCDCFast.cpp` so the
library can be installed next to the core's DigisparkCDC. The class is still
`DigiCDCDevice` and the object is still `SerialUSB`. V-USB itself is
unchanged.

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

## Known issue: lost packets under heavy two-way traffic

The `flood` tests in `extras/test/hosttest.py` found two ways a packet sent
to the host can be lost (Digispark Pro, Linux/xHCI):

- **The first packet after heavy receiving.** When the host sends a lot of
  data and the sketch sends nothing for a while, the first packet the sketch
  sends afterwards was dropped every time while the host kept sending, and in
  8 of 40 tries after it had stopped. It behaves like a data toggle mismatch:
  the host resets its side of the endpoint after transaction errors without
  telling the device. DigiCDCFast now sends an empty packet ahead of new data
  after 10 ms without sending, which absorbs the loss: 0 of 40 tries lost data
  after the host stopped.
- **Occasional packets while both directions are saturated.** With the host
  flooding the sketch while it sends, about 1 round in 20 still lost one
  8-byte packet. V-USB marks a packet as delivered as soon as it sends it,
  without waiting for the host's acknowledgement ("the rest of the driver
  assumes error-free transfers anyway", `src/asmcommon.inc`), so a packet the
  host didn't receive correctly is never sent again.

If a sketch receives and sends heavily at the same time, check data at the
application level.

## Not tested

- **Windows and macOS.** Only Linux has been tested. Hosts that follow the
  USB specification strictly may refuse DigiCDC's low-speed bulk endpoints
  (this is true of DigisparkCDC as well), and their polling intervals, and so
  the throughput, may differ.
- **Digispark Pro (ATtiny167).** `src/usbboardconfig.h` has settings for it,
  inherited from DigisparkCDC, but it has not been built or tried.
- Other clock settings, older USB host controllers (EHCI/OHCI/UHCI), hubs.
- `setDtrPin()` (the `CDC_DTR_LED` example).
- The `Throughput` example and `extras/throughput.py` have not been run on
  hardware; `HostTest` measured the throughput above.

Reports, good or bad, are welcome.

## Installing

Install the Digistump AVR core first (DigiCDCFast needs it). Then either
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

## Repository layout

```
library.properties     Arduino library metadata
src/                   DigiCDCFast.{h,cpp} and V-USB (with V-USB's Readme.txt and Changelog.txt)
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
