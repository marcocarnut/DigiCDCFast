# DigiCDCFast

A USB CDC serial port (`SerialUSB`) for the [Digispark](http://digistump.com/)
(ATtiny85), built on [V-USB](https://www.obdev.at/vusb/). It is a fork of the
DigisparkCDC library (`DigiCDC.h`) that ships with the Digistump AVR core
1.7.5, with the limits that held it to about 200 bytes/s removed. The API is
the same.

On Linux, a sketch that dumps data continuously went from about **115
bytes/s** with DigisparkCDC to about **3750 bytes/s** with DigiCDCFast.

## What was slow, and what changed

All the changes are in `src/DigiCDCFast.cpp`. The first three are the
throughput limits:

1. **`write()` waited 5 ms after every byte.** It called
   `SerialUSB.delay(5)` after queuing each byte, so no sketch could send more
   than 200 bytes/s, whatever the host did. Now `write()` queues the byte and
   services USB once. When the 64-byte transmit buffer is full it keeps
   servicing USB until there is room, and returns 0 only if the host has not
   read anything for 50 ms. The waiting matters: a byte that `write()`
   rejects is lost, since Arduino's `Print` functions either skip it or stop
   printing. The old 5 ms delay was, in effect, crude flow control that kept
   `println()` from losing data.
2. **`refresh()` waited 1 ms before every USB poll.** `available()`, `read()`
   and `delay()` all call `refresh()`, so every call cost at least 1 ms. The
   delay is gone.
3. **A zero-length packet followed every data packet.** A zero-length packet
   is only needed after a full 8-byte packet, to tell the host the transfer
   is over; sending one after every packet halved the number of data packets
   the host could take. Now it is sent only after full packets.

Also:

4. **New `drain()`** waits until the host has taken everything written so
   far.
5. The ring buffers used to be `static` variables defined in the header, so
   every file that included it got its own unused copy. They are now defined
   in the `.cpp`.
6. The transmit buffer is 64 bytes instead of 32.

The header and source were renamed `DigiCDCFast.h` / `DigiCDCFast.cpp` so the
library can be installed next to the core's DigisparkCDC. The class is still
`DigiCDCDevice` and the object is still `SerialUSB`. V-USB itself is
unchanged.

The git history starts with the unmodified DigisparkCDC files from the
1.7.5 core, so the second commit shows exactly what changed
(`git show -M20% $(git rev-list --reverse HEAD | sed -n 2p)`).

## Measurements

| Library      | Continuous dump |
|--------------|-----------------|
| DigisparkCDC | ~115 bytes/s    |
| DigiCDCFast  | ~3750 bytes/s   |

Test conditions:

- Digispark (ATtiny85) at 16.5 MHz (`digistump:avr:digispark-tiny:clock=clock165`),
  Digistump AVR core 1.7.5.
- Linux 6.8 on an xHCI (USB 3) host controller. DigiCDC declares bulk
  endpoints, which the USB specification does not allow on low-speed devices;
  Linux converts them to interrupt endpoints polled every 1 ms with 8-byte
  packets (the kernel logs "endpoint 0x81 is Bulk; changing to Interrupt").
- A sketch that sends binary data as fast as it can, read on the host from
  `/dev/ttyACM0`. No USB errors were logged during the test.

With 8-byte packets every 1 ms the ceiling would be 8000 bytes/s; what limits
the rate to about half of that has not been investigated.

The `Throughput` example and `extras/throughput.py` in this repository
reproduce the test, but they were written after the measurement above and
have not yet been run on hardware.

## Not tested

- **Windows and macOS.** Only Linux has been tested. Hosts that follow the
  USB specification strictly may refuse DigiCDC's low-speed bulk endpoints
  (this is true of DigisparkCDC as well).
- **Digispark Pro (ATtiny167).** `src/usbboardconfig.h` has settings for it,
  inherited from DigisparkCDC, but it has not been built or tried.
- Other clock settings, older USB host controllers (EHCI/OHCI/UHCI), hubs.
- `setDtrPin()` (the `CDC_DTR_LED` example).

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

Nothing else needs to change. Don't include both in the same sketch: they
define the same class, object and USB callbacks.

Things that behave differently:

- **Printing is no longer paced at 5 ms per byte.** If your host program
  relied on that (for example a slow reader with no flow control), it now
  gets data much faster.
- **`write()` can block for up to 50 ms per byte when nobody is reading.** On
  Linux the host stops reading when no program has the port open, so a
  sketch that prints with no terminal attached slows down: once the buffer
  is full, each byte of `print("...")` waits 50 ms before being dropped
  (`print(F("..."))` gives up after the first dropped byte). DigisparkCDC
  took about 1 ms per dropped byte instead. If this matters, print only
  when there is something to read the output, or less often.
- **`drain()` is new.** Call it before something that stops servicing USB
  (sleeping, a long computation, `end()`) if the data already written must
  arrive. It waits as long as it takes; it does not time out.

## Usage notes

These are unchanged from DigisparkCDC:

- **Call a `SerialUSB` function at least every ~10 ms.** V-USB has no
  background task: USB is serviced only inside `write()`, `print()`,
  `read()`, `available()`, `refresh()`, `delay()` and `drain()`. If your
  sketch does anything longer, call `SerialUSB.refresh()` in between, and
  use `SerialUSB.delay(ms)` instead of `delay(ms)`. Otherwise the host may
  reset or drop the device.
- `SerialUSB.begin()` takes no baud rate (`begin(unsigned long)` is declared
  but not implemented) and waits 500 ms for enumeration.
- `read()` and `peek()` return 0, not -1, when there is nothing to read;
  check `available()` first.
- `flush()` discards received data; it does not wait for output. Use
  `drain()` for that.
- Output sent before a program opens the port on the host is lost, except
  for what fits in the buffer.

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
library.properties   Arduino library metadata
src/                 DigiCDCFast.{h,cpp} and V-USB (with V-USB's Readme.txt and Changelog.txt)
examples/            example sketches
extras/throughput.py host-side throughput meter
License.txt          V-USB license (GPL-2.0 or GPL-3.0)
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
