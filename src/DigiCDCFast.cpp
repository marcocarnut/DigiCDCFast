/*

CDC Arduino Library by Ihsan Kehribar (kehribar.me) 
and Digistump LLC (digistump.com) 
- all changes made under the same license as V-USB


*/

#include "DigiCDCFast.h"
#include "DigiCDCDescriptor.h"
#include <stdint.h>
#include <Arduino.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>

/* library functions and variables start */
static uint8_t tmp[8];
static uint8_t outPacketSize, inPacketSize;  /* from the descriptor, in usbBegin() */
static uint8_t index = 0;

static RingBuffer_t rxBuf;
static RingBuffer_t txBuf;

/* The buffers: 64 and 32 bytes (DigiCDCBuffers.S), unless the sketch defines
   them too (see DigiCDCDescriptor.h) */

static bool         hostStalled;    /* host stopped reading: don't wait for room */
/* CDC line coding set by the host: bit rate (little endian), stop bits,
   parity, data bits. Starts as 9600 8N1. */
static uchar        lineCoding[7] = {0x80, 0x25, 0, 0, 0, 0, 8};

uchar              sendEmptyFrame;
static uchar       intr3Status;    /* used to control interrupt endpoint transmissions */
static uchar       portB_dtr_bit;

DigiCDCDevice::DigiCDCDevice(void){}


void DigiCDCDevice::delay(long milli) {
  unsigned long start = millis();
  while ((long)(millis() - start) < milli)
    refresh();
}

/* How long write() and flush() wait for the host to take data before
   deciding it has stopped reading (e.g. no program has the port open). */
#define HOST_TIMEOUT_MS 50

static uint8_t txPending()
{
    return RingBuffer_GetCount(&txBuf) + index;
}

/* Wait until the host has taken everything written, as in Arduino 1.0 and
   later (DigiCDC's flush() discarded received data instead). Gives up after
   HOST_TIMEOUT_MS without progress. */
void DigiCDCDevice::flush(){
    waitForHost(true);
}

/* Polls until the host has taken everything (all) or there is room for a
   byte. False when the host takes nothing for HOST_TIMEOUT_MS, or has
   already been found stalled. */
bool DigiCDCDevice::waitForHost(bool all)
{
    uint16_t start = millis();
    uint8_t pending = txPending();
    while(all ? pending > 0 || sendEmptyFrame || !usbInterruptIsReady()
              : RingBuffer_IsFull(&txBuf))
    {
        refresh();
        if(txPending() < pending)
        {
            pending = txPending();
            start = millis();
            hostStalled = false;
        }
        else if(hostStalled || (uint16_t)millis() - start > HOST_TIMEOUT_MS)
        {
            hostStalled = true;
            return false;
        }
    }
    return true;
}

void DigiCDCDevice::begin(){

    usbBegin();
    DigiCDCDevice::delay(500);//delay to allow enumeration and such

}

void DigiCDCDevice::begin(unsigned long){  /* baud rate is meaningless over USB */
    begin();
}

/* Waits while the buffer is full, since a rejected byte is lost (Print
   skips it, or stops printing). If the host doesn't take data for
   HOST_TIMEOUT_MS, bytes are rejected immediately until there is room again,
   so a sketch printing with no program reading doesn't slow to a crawl. */
size_t DigiCDCDevice::write(uint8_t c)
{
    if(!waitForHost(false))
        return 0;
    hostStalled = false;
    RingBuffer_Insert(&txBuf,c);
    usbPollWrapper();
    return 1;
}

/* Print's version calls write(uint8_t) through the virtual table for each byte */
size_t DigiCDCDevice::write(const uint8_t *buffer, size_t size)
{
    size_t n = 0;
    while(n < size && DigiCDCDevice::write(buffer[n]))
        n++;
    return n;
}

int DigiCDCDevice::availableForWrite()
{
    return RingBuffer_GetFreeCount(&txBuf);
}

/* The bit rate the host last set for the port (for bridges; DigiCDC itself
   ignores it). Changed from usbPoll(), so no interrupt can interfere. */
unsigned long DigiCDCDevice::baud()
{
    /* widen before shifting: (lineCoding[1] << 8) would be a negative int
       for rates like 57600 (0xE100) and sign-extend */
    return (unsigned long)lineCoding[0] | ((unsigned long)lineCoding[1] << 8)
        | ((unsigned long)lineCoding[2] << 16) | ((unsigned long)lineCoding[3] << 24);
}

int DigiCDCDevice::available()
{
    refresh();
    return RingBuffer_GetCount(&rxBuf);
}

int DigiCDCDevice::read()
{
    int c = peek();
    if(c >= 0)
        RingBuffer_Remove(&rxBuf);
    return c;
}

int DigiCDCDevice::peek()
{
    refresh();
    if(RingBuffer_IsEmpty(&rxBuf))
        return -1;
    return RingBuffer_Peek(&rxBuf);
}


void DigiCDCDevice::task(void)
{    
 
  refresh();

}

void DigiCDCDevice::refresh(void)
{    
  usbPollWrapper();
}


void DigiCDCDevice::end(void)
{
    // drive both USB pins low to disconnect
    usbDeviceDisconnect();
    cli();
    RingBuffer_InitBuffer(&rxBuf,digiCdcRxBuffer,pgm_read_byte(&digiCdcBufferSizes[1]));
    sei(); 
    
}

DigiCDCDevice::operator bool() {
    refresh();
    return true;
}






void DigiCDCDevice::usbBegin()
{
    cli();

    PORTB &= ~(_BV(USB_CFG_DMINUS_BIT) | _BV(USB_CFG_DPLUS_BIT));
    usbDeviceDisconnect();
    _delay_ms(250);
    usbDeviceConnect();
    usbInit();

    RingBuffer_InitBuffer(&txBuf,digiCdcTxBuffer,pgm_read_byte(&digiCdcBufferSizes[0]));
    RingBuffer_InitBuffer(&rxBuf,digiCdcRxBuffer,pgm_read_byte(&digiCdcBufferSizes[1]));

    outPacketSize = pgm_read_byte(&digiCdcConfigDescriptor[DIGICDC_OUT_PACKET_SIZE_AT]);
    inPacketSize = pgm_read_byte(&digiCdcConfigDescriptor[DIGICDC_IN_PACKET_SIZE_AT]);
    intr3Status = 0;
    sendEmptyFrame = 0;
    hostStalled = false;
    portB_dtr_bit = 255;
    sei();   
}

void DigiCDCDevice::usbPollWrapper()
{
    usbPoll();
    /* Resume input paused by usbFunctionWriteOut() once a full packet fits.
       V-USB allows this only while requests are disabled; enabling them
       unconditionally (as DigiCDC did on every packet sent) discards a
       received packet that is waiting to be processed. */
    if(usbAllRequestsAreDisabled() && RingBuffer_GetFreeCount(&rxBuf) >= outPacketSize)
        usbEnableAllRequests();
    while((!(RingBuffer_IsEmpty(&txBuf)))&&(index<inPacketSize))
    {
        tmp[index++] = RingBuffer_Remove(&txBuf);
    }

    if(usbInterruptIsReady())
    {
        if(index>0)
        {
            usbSetInterrupt(tmp,index);
            /* only a full packet leaves the host waiting for more */
            sendEmptyFrame = (index == inPacketSize);
            index = 0;
        }
        else if(sendEmptyFrame)
        {
            usbSetInterrupt(tmp,0);
            sendEmptyFrame = 0;
        }
    }

    /* We need to report rx and tx carrier after open attempt */
    if(intr3Status != 0 && usbInterruptIsReady3()){
        static uchar serialStateNotification[10] = {0xa1, 0x20, 0, 0, 0, 0, 2, 0, 3, 0};

        if(intr3Status == 2){
            usbSetInterrupt3(serialStateNotification, 8);
        }else{
            usbSetInterrupt3(serialStateNotification+8, 2);
        }
        intr3Status--;
    }
    
}

void DigiCDCDevice::setDtrPin(uint8_t dtrPin){
    portB_dtr_bit = dtrPin;
    PORTB &= ~(_BV(portB_dtr_bit));
    DDRB |= (1 << portB_dtr_bit);
}

#ifdef __cplusplus
extern "C"{
#endif 

enum {
    SEND_ENCAPSULATED_COMMAND = 0,
    GET_ENCAPSULATED_RESPONSE,
    SET_COMM_FEATURE,
    GET_COMM_FEATURE,
    CLEAR_COMM_FEATURE,
    SET_LINE_CODING = 0x20,
    GET_LINE_CODING,
    SET_CONTROL_LINE_STATE,
    SEND_BREAK
};

/* 8-byte packets, unless the sketch defines the descriptor too (see
   DigiCDCDescriptor.h and DigiCDCMedium.h) */
extern const uchar digiCdcConfigDescriptor[DIGICDC_DESCRIPTOR_SIZE] PROGMEM __attribute__((weak)) =
    DIGICDC_CONFIG_DESCRIPTOR(8, 8);

uchar usbFunctionDescriptor(usbRequest_t *rq)
{
    if(rq->wValue.bytes[1] == USBDESCR_DEVICE){
        usbMsgPtr = (uchar *)usbDescriptorDevice;
        return usbDescriptorDevice[0];
    }else{  /* must be config descriptor */
        usbMsgPtr = (uchar *)digiCdcConfigDescriptor;
        return DIGICDC_DESCRIPTOR_SIZE;
    }
}

/* ------------------------------------------------------------------------- */
/* ----------------------------- USB interface ----------------------------- */
/* ------------------------------------------------------------------------- */

uchar usbFunctionSetup(uchar data[8])
{
usbRequest_t    *rq = (usbRequest_t*)((void *)data);

    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS){    /* class request type */

        if( rq->bRequest==GET_LINE_CODING ){
            usbMsgPtr = lineCoding;
            return sizeof(lineCoding);
        }
        if( rq->bRequest==SET_LINE_CODING ){
            return 0xff;    /* -> usbFunctionWrite() */
        }
        if(rq->bRequest == SET_CONTROL_LINE_STATE){
            if(portB_dtr_bit != 0xFF)
                PORTB  = (PORTB&~(1<<portB_dtr_bit))|((rq->wValue.word&1)<<portB_dtr_bit);
            /* Report serial state (carrier detect). On several Unix platforms,
             * tty devices can only be opened when carrier detect is set.
             */
            if( intr3Status==0 )
                intr3Status = 2;
        }

        /*  Prepare bulk-in endpoint to respond to early termination   */
        if((rq->bmRequestType & USBRQ_DIR_MASK) == USBRQ_DIR_HOST_TO_DEVICE)
            sendEmptyFrame  = 1;
    }

    return 0;
}

/*---------------------------------------------------------------------------*/
/* usbFunctionWrite                                                          */
/*---------------------------------------------------------------------------*/
/* A sketch may define this to refuse line settings it can't provide (a
   USB-UART bridge with a few bit rates, say): the request is then answered
   with a STALL, as the CDC specification asks, and GET_LINE_CODING keeps
   reporting the settings in use. Note that Linux does not pass the refusal
   on to the program that called tcsetattr(). */
uchar digiCdcAcceptLineCoding(const uchar *coding) __attribute__((weak));
uchar digiCdcAcceptLineCoding(const uchar *)
{
    return 1;
}

uchar usbFunctionWrite( uchar *data, uchar len )
{
    /* SET_LINE_CODING: 7 bytes, which fit in one 8-byte packet */
    if(len > sizeof(lineCoding))
        len = sizeof(lineCoding);
    if(len == sizeof(lineCoding) && !digiCdcAcceptLineCoding(data))
        return 0xff;    /* STALL: these settings are refused */
    memcpy(lineCoding, data, len);
    return 1;
}

void usbFunctionWriteOut( uchar *data, uchar len )
{
    uint8_t qw = 0;
    for(qw=0;qw<len;qw++)
    {
        if(!RingBuffer_IsFull(&rxBuf))
        {
            RingBuffer_Insert(&rxBuf,data[qw]);
        }  
    }

    /* postpone receiving next data */
    /* pause input (the host gets NAKs) until another full packet fits */
    if(RingBuffer_GetFreeCount(&rxBuf) < outPacketSize)
    {
        usbDisableAllRequests();
    }
}


#ifdef __cplusplus
} // extern "C"
#endif

DigiCDCDevice SerialUSB;
