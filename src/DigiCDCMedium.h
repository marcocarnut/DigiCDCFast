/*
  DigiCDCMedium.h -- DigiCDCFast with 2-byte USB packets.

  Include this instead of DigiCDCFast.h (in one file of the sketch only).
  Each USB transaction then keeps V-USB's interrupts off for ~73 us instead of
  ~110 us, at the cost of throughput: at most ~2000 bytes/s each way instead
  of ~8000. Only for sketches whose own timing competes with V-USB, such as a
  software UART transmitting at 9600 bps; most sketches want DigiCDCFast.h.

  It defines the USB configuration descriptor with 2-byte data packets, which
  the linker takes over the library's own 8-byte one. For other sizes (1 to 8
  bytes), copy this file and change the numbers.
*/
#ifndef __DigiCDCMedium_h__
#define __DigiCDCMedium_h__

#include "DigiCDCFast.h"
#include "DigiCDCDescriptor.h"

#define DIGICDC_MEDIUM

const uchar digiCdcConfigDescriptor[DIGICDC_DESCRIPTOR_SIZE] PROGMEM =
    DIGICDC_CONFIG_DESCRIPTOR(2, 2);

#endif
