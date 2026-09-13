/*
  LatencyProbe: measures how late interrupts run on a Digispark using
  DigiCDCFast, i.e. how long V-USB keeps interrupts blocked while USB is idle,
  sending or receiving. Driven by extras/latency/latency.py.

  Timer0 runs in CTC mode with prescaler 64 (3.88 us per tick at 16.5 MHz)
  and a pseudo-random period of 128-255 ticks, so its phase doesn't lock to
  the host's 1 ms polling. In CTC mode TCNT0 restarts from 0 at the compare
  match, so the value the interrupt reads on entry is how many ticks late it
  runs. Latencies go into a histogram of 1-tick bins (the last bin collects
  everything longer); interrupts delayed by a whole period or more, whose
  count has wrapped, are counted separately as "late".

  Commands (one character):
    'i'  idle: service USB only
    't'  send a counting sequence as fast as possible
    'r'  receive and discard everything the host sends
    'b'  both
  Each runs for RUN_MS, then reports
    H <mode> <samples> <late> <max ticks> <bin 0> ... <bin 63>
  Ctrl-C three times in a row jumps to the micronucleus bootloader.

  Timer0 is taken over, so analogWrite() on pins 0 and 1 doesn't work.
*/

#include <DigiCDCFast.h>

#define RUN_MS       20000
#define DRAIN_MS     500
#define BINS         64
#define MIN_PERIOD   128  // ticks; OCR0A varies between 128 and 255

static uint16_t hist[BINS];
static uint16_t samples, late;
static uint8_t maxTicks;
static uint8_t lfsr = 1;

ISR(TIMER0_COMPA_vect)
{
  uint8_t ticks = TCNT0;
  if (TIFR & _BV(OCF0A)) {  // matched again already: the count has wrapped
    TIFR = _BV(OCF0A);
    late++;
  } else {
    if (ticks > maxTicks)
      maxTicks = ticks;
    hist[ticks < BINS ? ticks : BINS - 1]++;
    samples++;
  }
  lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xB4);  // 8-bit maximal-length LFSR
  OCR0A = MIN_PERIOD | lfsr;
}

static void startProbe()
{
  cli();
  memset(hist, 0, sizeof hist);
  samples = late = 0;
  maxTicks = 0;
  TCCR0A = _BV(WGM01);            // CTC
  TCCR0B = _BV(CS01) | _BV(CS00); // clk/64
  TCNT0 = 0;
  OCR0A = 200;
  TIFR = _BV(OCF0A);
  TIMSK |= _BV(OCIE0A);
  sei();
}

static void stopProbe()
{
  cli();
  TIMSK &= ~_BV(OCIE0A);
  TCCR0B = 0;
  sei();
}

static void enterBootloader()
{
  cli();
  TIMSK = 0;   // no sketch interrupts may fire once the bootloader runs
  TCCR0B = 0;
  TCCR1 = 0;
  ((void (*)())0)();  // micronucleus points the reset vector at itself
}

static void run(char mode)
{
  bool send = mode == 't' || mode == 'b';
  bool receive = mode == 'r' || mode == 'b';
  uint8_t counter = 0;

  startProbe();
  unsigned long start = millis();
  while (millis() - start < RUN_MS) {
    if (send)
      SerialUSB.write(counter++);
    if (receive) {
      while (SerialUSB.available())
        SerialUSB.read();
    } else {
      SerialUSB.refresh();
    }
  }
  stopProbe();

  SerialUSB.flush();
  start = millis();
  while (millis() - start < DRAIN_MS)  // input the host sent near the end
    SerialUSB.read();

  SerialUSB.print('H');
  SerialUSB.print(' ');
  SerialUSB.print(mode);
  SerialUSB.print(' ');
  SerialUSB.print(samples);
  SerialUSB.print(' ');
  SerialUSB.print(late);
  SerialUSB.print(' ');
  SerialUSB.print(maxTicks);
  for (uint8_t i = 0; i < BINS; i++) {
    SerialUSB.print(' ');
    SerialUSB.print(hist[i]);
  }
  SerialUSB.print("\r\n");
}

void setup()
{
  SerialUSB.begin();
}

void loop()
{
  static uint8_t ctrlCs;
  int c = SerialUSB.read();
  if (c < 0)
    return;
  ctrlCs = c == 3 ? ctrlCs + 1 : 0;
  if (ctrlCs == 3)
    enterBootloader();
  if (c == 'i' || c == 't' || c == 'r' || c == 'b')
    run(c);
}
