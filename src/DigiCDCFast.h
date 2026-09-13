/*

CDC Arduino Library by Ihsan Kehribar (kehribar.me) 
and Digistump LLC (digistump.com) 
- all changes made under the same license as V-USB

DigiCDCFast: fork of DigisparkCDC from the Digistump AVR core 1.7.5, without
its throughput limits (~200 bytes/s at best) and with its bugs fixed:
- write() no longer waits 5 ms after every byte; it queues the byte and
  services USB once. When the buffer is full it waits for room (a rejected
  byte is lost: Print skips it or stops printing). If the host takes nothing
  for 50 ms, bytes are rejected immediately (returning 0) until there is room.
- refresh() no longer waits 1 ms before servicing USB.
- A zero-length packet is sent only after a full 8-byte packet that ends a
  transfer, instead of after every packet.
- Receiving no longer stalls in sketches that don't write: input paused for
  flow control is resumed when the buffer has room, not when sending. And
  received packets are no longer discarded while sending (DigiCDC re-enabled
  input on every packet sent, which V-USB forbids while it is enabled).
  Input is paused only when another 8-byte packet wouldn't fit.
- flush() waits until the host has taken everything written (50 ms timeout),
  as in Arduino 1.0+. It used to discard received data; to do that now, read
  until available() returns 0.
- read() and peek() return -1 when no data is available (they returned 0).
- begin(unsigned long) is implemented (the baud rate is ignored).
- The transmit buffer is 64 bytes (32 in the 1.7.5 core).
- The buffers are defined in DigiCDCFast.cpp instead of as static variables
  in this header, so other files that include it no longer get unused copies.
Same API otherwise; include DigiCDCFast.h instead of DigiCDC.h.


 */
#ifndef __DigiCDCFast_h__
#define __DigiCDCFast_h__
#include "usbdrv.h"



#include "Stream.h"
#include "ringBuffer.h"


#define HW_CDC_TX_BUF_SIZE     64
#define HW_CDC_RX_BUF_SIZE     32
#define HW_CDC_BULK_OUT_SIZE     8
#define HW_CDC_BULK_IN_SIZE      8




class DigiCDCDevice  : public Stream {
    public:
        DigiCDCDevice();
        void begin(), begin(unsigned long x);
        void end();
        void refresh();
        void task();
        void delay(long milli);
        void setDtrPin(uint8_t dtrPin);
        virtual int available(void);
        virtual int peek(void);
        virtual int read(void);
        virtual void flush(void);
        virtual size_t write(uint8_t);
        using Print::write;
        operator bool();
    private:
        void usbBegin();
        void usbPollWrapper();
 };


extern DigiCDCDevice SerialUSB;


#endif // __DigiCDCFast_h__
