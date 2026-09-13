/*

CDC Arduino Library by Ihsan Kehribar (kehribar.me) 
and Digistump LLC (digistump.com) 
- all changes made under the same license as V-USB

DigiCDCFast: fork of DigisparkCDC from the Digistump AVR core 1.7.5, without
its throughput limits (~200 bytes/s at best):
- write() no longer waits 5 ms after every byte; it queues the byte and
  services USB once. When the buffer is full it waits for room (Print gives
  up on the first rejected byte), returning 0 only after 50 ms without the
  host reading.
- refresh() no longer waits 1 ms before servicing USB.
- A zero-length packet is sent only after a full 8-byte packet that ends a
  transfer, instead of after every packet.
- drain() waits until the host has taken everything written.
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
        void drain();
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
