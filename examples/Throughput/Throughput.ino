/*
 * DigiCDCFast throughput test.
 *
 * Sends a continuous stream of bytes counting 0, 1, ..., 255, 0, 1, ... so
 * the host can measure the transfer rate and check that nothing is lost or
 * repeated. On the host, run
 *
 *   python3 extras/throughput.py /dev/ttyACM0
 *
 * (use the port your Digispark appears as). Any serial terminal works too,
 * but it will show binary garbage.
 *
 * write() returns 0 when the host has not read anything for 50 ms (for
 * example while no program has the port open). The same value is then tried
 * again, so the stream the host sees stays in sequence.
 */
#include <DigiCDCFast.h>

uint8_t counter = 0;

void setup() {
  SerialUSB.begin();
}

void loop() {
  // Discard anything the host sends (a terminal may echo), so the receive
  // buffer never fills up.
  while (SerialUSB.available()) {
    SerialUSB.read();
  }

  // write() also services USB, so this loop keeps the device alive.
  if (SerialUSB.write(counter)) {
    counter++;
  }
}
