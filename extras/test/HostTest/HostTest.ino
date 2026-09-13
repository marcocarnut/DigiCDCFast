/*
  HostTest: hardware test for DigiCDCFast, driven by extras/test/hosttest.py.

  Commands (one character each):
    'P'  report peek() and read() with no input pending
    'R'  receive 2000 bytes (0, 1, ..., 255, 0, ...) without writing anything,
         then report how many arrived and how many were wrong
    'E'  echo 2000 bytes back as they arrive, then report the count
    'T'  send 20000 bytes of a counting sequence, flush(), report the time
    'W'  wait 1 s (the host closes the port meanwhile), then time writing
         300 bytes and a flush() with nobody reading
    '?'  report the result of the last 'W'
    Ctrl-C three times in a row jumps to the micronucleus bootloader, so the
    board can be reflashed without replugging.

  Reads use available() before read(), so this also builds against the
  original DigisparkCDC (change the include to DigiCDC.h) for comparison.
*/

#include <DigiCDCFast.h>

#define PATTERN_BYTES 2000
#define STREAM_BYTES  20000UL
#define STALL_BYTES   300

static unsigned long stallWriteMs, stallFlushMs;
static uint16_t stallAccepted;

static void enterBootloader()
{
  cli();
  TIMSK = 0;   // no sketch interrupts may fire once the bootloader runs
  TCCR0B = 0;
  TCCR1 = 0;
  ((void (*)())0)();  // micronucleus points the reset vector at itself
}

static int waitByte(unsigned long timeoutMs)
{
  unsigned long start = millis();
  while (!SerialUSB.available()) {
    if (millis() - start > timeoutMs)
      return -1;
  }
  return SerialUSB.read();
}

static void report(char test, long a, long b, long c)
{
  SerialUSB.print(test);
  SerialUSB.print(' ');
  SerialUSB.print(a);
  SerialUSB.print(' ');
  SerialUSB.print(b);
  SerialUSB.print(' ');
  SerialUSB.print(c);
  SerialUSB.print("\r\n");
}

void setup()
{
  SerialUSB.begin(115200);  // baud rate ignored; exercises begin(unsigned long)
}

void loop()
{
  static uint8_t ctrlCs;
  int command = waitByte(1000);
  uint16_t count = 0, errors = 0;
  unsigned long start;

  if (command < 0)
    return;
  ctrlCs = command == 3 ? ctrlCs + 1 : 0;
  if (ctrlCs == 3)
    enterBootloader();

  switch (command) {
  case 'P':
    while (SerialUSB.available())
      SerialUSB.read();
    report('P', SerialUSB.peek(), SerialUSB.read(), 0);
    break;

  case 'R':
    for (int c; count < PATTERN_BYTES && (c = waitByte(3000)) >= 0; count++)
      errors += c != (int)(count & 0xFF);
    report('R', count, errors, 0);
    break;

  case 'E':
    for (int c; count < PATTERN_BYTES && (c = waitByte(3000)) >= 0; count++)
      SerialUSB.write(c);
    report('E', count, 0, 0);
    break;

  case 'T':
    start = millis();
    for (unsigned long i = 0; i < STREAM_BYTES; i++)
      SerialUSB.write(i & 0xFF);
    SerialUSB.flush();
    report('T', millis() - start, 0, 0);
    break;

  case 'W':
    SerialUSB.delay(1000);
    start = millis();
    stallAccepted = 0;
    for (uint16_t i = 0; i < STALL_BYTES; i++)
      stallAccepted += SerialUSB.write('w');
    stallWriteMs = millis() - start;
    start = millis();
    SerialUSB.flush();
    stallFlushMs = millis() - start;
    break;

  case '?':
    report('W', stallWriteMs, stallFlushMs, stallAccepted);
    break;
  }
}
