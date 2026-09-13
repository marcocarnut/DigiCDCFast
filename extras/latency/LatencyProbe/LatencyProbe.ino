/*
  LatencyProbe: measures how late interrupts run on a Digispark using
  DigiCDCFast, i.e. how long V-USB keeps interrupts blocked while USB is idle,
  sending or receiving. Driven by extras/latency/latency.py.

  A timer the core doesn't use for millis() (Timer0 on the Digispark,
  Timer1 on the Digispark Pro) runs in CTC mode with prescaler 64 (3.88 us
  per tick at 16.5 MHz, 4 us at 16 MHz) and a pseudo-random period of
  128-255 ticks, so its phase doesn't lock to the host's 1 ms polling. In CTC
  mode the counter restarts from 0 at the compare match, so the value the
  interrupt reads on entry is how many ticks late it runs. Latencies go into
  a histogram of 1-tick bins (the last bin collects everything longer);
  interrupts delayed by a whole period or more, whose count has wrapped, are
  counted separately as "late".

  Commands (one character):
    'i'  idle: service USB only
    't'  send a counting sequence as fast as possible
    'r'  receive and discard everything the host sends
    'b'  both
  Each runs for RUN_MS, then reports
    H <mode> <tick ns> <samples> <late> <max ticks> <bin 0> ... <bin 63>
  Ctrl-C three times in a row jumps to the micronucleus bootloader.

  The timer is taken over, so analogWrite() on its pins doesn't work.
*/

#include <DigiCDCFast.h>

#define RUN_MS       20000
#define DRAIN_MS     500
#define BINS         64
#define MIN_PERIOD   128  // ticks; the period varies between 128 and 255
#define TICK_NS      (64000000000ULL / F_CPU)

#if defined(__AVR_ATtiny167__)  // Digispark Pro: millis() uses Timer0
#define PROBE_VECTOR  TIMER1_COMPA_vect
#define PROBE_COUNT   TCNT1
#define PROBE_TOP     OCR1A
#define PROBE_TIFR    TIFR1
#define PROBE_TIMSK   TIMSK1
#define PROBE_FLAG    OCF1A
#define PROBE_ENABLE  OCIE1A
#else                           // Digispark: millis() uses Timer1
#define PROBE_VECTOR  TIMER0_COMPA_vect
#define PROBE_COUNT   TCNT0
#define PROBE_TOP     OCR0A
#define PROBE_TIFR    TIFR
#define PROBE_TIMSK   TIMSK
#define PROBE_FLAG    OCF0A
#define PROBE_ENABLE  OCIE0A
#endif

static uint16_t hist[BINS];
static uint16_t samples, late;
static uint8_t maxTicks;
static uint8_t lfsr = 1;

ISR(PROBE_VECTOR)
{
  uint16_t ticks = PROBE_COUNT;
  if (PROBE_TIFR & _BV(PROBE_FLAG)) {  // matched again already: the count has wrapped
    PROBE_TIFR = _BV(PROBE_FLAG);
    late++;
  } else {
    if (ticks > maxTicks)
      maxTicks = ticks;  // < 256: the period is at most 255 ticks
    hist[ticks < BINS ? ticks : BINS - 1]++;
    samples++;
  }
  lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xB4);  // 8-bit maximal-length LFSR
  PROBE_TOP = MIN_PERIOD | lfsr;
}

static void startProbe()
{
  cli();
  memset(hist, 0, sizeof hist);
  samples = late = 0;
  maxTicks = 0;
#if defined(__AVR_ATtiny167__)
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);  // CTC, clk/64
#else
  TCCR0A = _BV(WGM01);                           // CTC
  TCCR0B = _BV(CS01) | _BV(CS00);                // clk/64
#endif
  PROBE_COUNT = 0;
  PROBE_TOP = 200;
  PROBE_TIFR = _BV(PROBE_FLAG);
  PROBE_TIMSK |= _BV(PROBE_ENABLE);
  sei();
}

static void stopProbe()
{
  cli();
  PROBE_TIMSK &= ~_BV(PROBE_ENABLE);
#if defined(__AVR_ATtiny167__)
  TCCR1B = 0;
#else
  TCCR0B = 0;
#endif
  sei();
}

static void enterBootloader()
{
  cli();
  // no sketch interrupts may fire once the bootloader runs
#if defined(__AVR_ATtiny167__)
  TIMSK0 = 0;
  TIMSK1 = 0;
  TCCR0B = 0;
  TCCR1B = 0;
#else
  TIMSK = 0;
  TCCR0B = 0;
  TCCR1 = 0;
#endif
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
  SerialUSB.print((unsigned long)TICK_NS);
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
