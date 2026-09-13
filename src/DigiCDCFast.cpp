/*

CDC Arduino Library by Ihsan Kehribar (kehribar.me) 
and Digistump LLC (digistump.com) 
- all changes made under the same license as V-USB


*/

#include "DigiCDCFast.h"
#include <stdint.h>
#include <Arduino.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>

/* library functions and variables start */
static uint8_t tmp[HW_CDC_BULK_IN_SIZE];
static uint8_t index = 0;

static RingBuffer_t rxBuf;
static uint8_t      rxBuf_Data[HW_CDC_RX_BUF_SIZE];

static RingBuffer_t txBuf;
static uint8_t      txBuf_Data[HW_CDC_TX_BUF_SIZE];

static bool         hostStalled;    /* host stopped reading: don't wait for room */
static unsigned long lastInMs;      /* when the last data packet was queued for the host */
static bool         resyncSent;     /* empty packet already queued ahead of the next data */

uchar              sendEmptyFrame;
static uchar       intr3Status;    /* used to control interrupt endpoint transmissions */
static uchar       portB_dtr_bit;

DigiCDCDevice::DigiCDCDevice(void){}


void DigiCDCDevice::delay(long milli) {
  unsigned long last = millis();
  while (milli > 0) {
    unsigned long now = millis();
    milli -= now - last;
    last = now;
    refresh();
  }
}

/* How long write() and flush() wait for the host to take data before
   deciding it has stopped reading (e.g. no program has the port open). */
#define HOST_TIMEOUT_MS 50

/* Idle time after which an empty packet is sent ahead of new data (see
   usbPollWrapper()). */
#define RESYNC_IDLE_MS 10

static uint8_t txPending()
{
    return RingBuffer_GetCount(&txBuf) + index;
}

/* Wait until the host has taken everything written, as in Arduino 1.0 and
   later (DigiCDC's flush() discarded received data instead). Gives up after
   HOST_TIMEOUT_MS without progress. */
void DigiCDCDevice::flush(){
    unsigned long start = millis();
    uint8_t pending = txPending();
    while(pending > 0 || sendEmptyFrame || !usbInterruptIsReady())
    {
        refresh();
        if(txPending() < pending)
        {
            pending = txPending();
            start = millis();
            hostStalled = false;
        }
        else if(hostStalled || millis() - start > HOST_TIMEOUT_MS)
        {
            hostStalled = true;
            return;
        }
    }
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
    unsigned long start = millis();
    while(RingBuffer_IsFull(&txBuf))
    {
        refresh();
        if(!RingBuffer_IsFull(&txBuf))
            break;
        if(hostStalled || millis() - start > HOST_TIMEOUT_MS)
        {
            hostStalled = true;
            return 0;
        }
    }
    hostStalled = false;
    RingBuffer_Insert(&txBuf,c);
    usbPollWrapper();
    return 1;
}

int DigiCDCDevice::available()
{
    refresh();
    return RingBuffer_GetCount(&rxBuf);
}

int DigiCDCDevice::read()
{
    refresh();
    if(RingBuffer_IsEmpty(&rxBuf))
        return -1;
    return RingBuffer_Remove(&rxBuf);
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
    RingBuffer_InitBuffer(&rxBuf,rxBuf_Data,sizeof(rxBuf_Data));
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

    RingBuffer_InitBuffer(&txBuf,txBuf_Data,sizeof(txBuf_Data));
    RingBuffer_InitBuffer(&rxBuf,rxBuf_Data,sizeof(rxBuf_Data));

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
    if(usbAllRequestsAreDisabled() && RingBuffer_GetFreeCount(&rxBuf) >= HW_CDC_BULK_OUT_SIZE)
        usbEnableAllRequests();
    while((!(RingBuffer_IsEmpty(&txBuf)))&&(index<8))
    {
        tmp[index++] = RingBuffer_Remove(&txBuf);
    }

    if(usbInterruptIsReady())
    {
        if(index>0 && !resyncSent && millis() - lastInMs > RESYNC_IDLE_MS)
        {
            /* After receiving heavily without sending, the host sometimes
               drops the first packet sent (on Linux/xHCI: every time under a
               continuous flood). It behaves like a data toggle mismatch: the
               host resets its side of the IN endpoint after transaction
               errors without telling the device. An empty packet first
               absorbs the loss; if nothing is wrong, the host just gets an
               empty read. */
            usbSetInterrupt(tmp,0);
            resyncSent = true;
        }
        else if(index>0)
        {
            usbSetInterrupt(tmp,index);
            /* only a full packet leaves the host waiting for more */
            sendEmptyFrame = (index == HW_CDC_BULK_IN_SIZE);
            index = 0;
            resyncSent = false;
            lastInMs = millis();
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

static const PROGMEM uchar configDescrCDC[] = {   /* USB configuration descriptor */
    9,          /* sizeof(usbDescrConfig): length of descriptor in bytes */
    USBDESCR_CONFIG,    /* descriptor type */
    67,
    0,          /* total length of data returned (including inlined descriptors) */
    2,          /* number of interfaces in this configuration */
    1,          /* index of this configuration */
    0,          /* configuration name string index */
#if USB_CFG_IS_SELF_POWERED
    (1 << 7) | USBATTR_SELFPOWER,       /* attributes */
#else
    (1 << 7),                           /* attributes */
#endif
    USB_CFG_MAX_BUS_POWER/2,            /* max USB current in 2mA units */

    /* interface descriptor follows inline: */
    9,          /* sizeof(usbDescrInterface): length of descriptor in bytes */
    USBDESCR_INTERFACE, /* descriptor type */
    0,          /* index of this interface */
    0,          /* alternate setting for this interface */
    USB_CFG_HAVE_INTRIN_ENDPOINT,   /* endpoints excl 0: number of endpoint descriptors to follow */
    USB_CFG_INTERFACE_CLASS,
    USB_CFG_INTERFACE_SUBCLASS,
    USB_CFG_INTERFACE_PROTOCOL,
    0,          /* string index for interface */

    /* CDC Class-Specific descriptor */
    5,           /* sizeof(usbDescrCDC_HeaderFn): length of descriptor in bytes */
    0x24,        /* descriptor type */
    0,           /* header functional descriptor */
    0x10, 0x01,

    4,           /* sizeof(usbDescrCDC_AcmFn): length of descriptor in bytes    */
    0x24,        /* descriptor type */
    2,           /* abstract control management functional descriptor */
    0x02,        /* SET_LINE_CODING, GET_LINE_CODING, SET_CONTROL_LINE_STATE    */

    5,           /* sizeof(usbDescrCDC_UnionFn): length of descriptor in bytes  */
    0x24,        /* descriptor type */
    6,           /* union functional descriptor */
    0,           /* CDC_COMM_INTF_ID */
    1,           /* CDC_DATA_INTF_ID */

    5,           /* sizeof(usbDescrCDC_CallMgtFn): length of descriptor in bytes */
    0x24,        /* descriptor type */
    1,           /* call management functional descriptor */
    3,           /* allow management on data interface, handles call management by itself */
    1,           /* CDC_DATA_INTF_ID */

    /* Endpoint Descriptor */
    7,           /* sizeof(usbDescrEndpoint) */
    USBDESCR_ENDPOINT,  /* descriptor type = endpoint */
    0x80|USB_CFG_EP3_NUMBER,        /* IN endpoint number 3 */
    0x03,        /* attrib: Interrupt endpoint */
    8, 0,        /* maximum packet size */
    USB_CFG_INTR_POLL_INTERVAL,        /* in ms */

    /* Interface Descriptor  */
    9,           /* sizeof(usbDescrInterface): length of descriptor in bytes */
    USBDESCR_INTERFACE,           /* descriptor type */
    1,           /* index of this interface */
    0,           /* alternate setting for this interface */
    2,           /* endpoints excl 0: number of endpoint descriptors to follow */
    0x0A,        /* Data Interface Class Codes */
    0,
    0,           /* Data Interface Class Protocol Codes */
    0,           /* string index for interface */

    /* Endpoint Descriptor */
    7,           /* sizeof(usbDescrEndpoint) */
    USBDESCR_ENDPOINT,  /* descriptor type = endpoint */
    0x01,        /* OUT endpoint number 1 */
    0x02,        /* attrib: Bulk endpoint */
    HW_CDC_BULK_OUT_SIZE, 0,        /* maximum packet size */
    0,           /* in ms */

    /* Endpoint Descriptor */
    7,           /* sizeof(usbDescrEndpoint) */
    USBDESCR_ENDPOINT,  /* descriptor type = endpoint */
    0x81,        /* IN endpoint number 1 */
    0x02,        /* attrib: Bulk endpoint */
    HW_CDC_BULK_IN_SIZE, 0,        /* maximum packet size */
    0,           /* in ms */
};

uchar usbFunctionDescriptor(usbRequest_t *rq)
{
    if(rq->wValue.bytes[1] == USBDESCR_DEVICE){
        usbMsgPtr = (uchar *)usbDescriptorDevice;
        return usbDescriptorDevice[0];
    }else{  /* must be config descriptor */
        usbMsgPtr = (uchar *)configDescrCDC;
        return sizeof(configDescrCDC);
    }
}

/* ------------------------------------------------------------------------- */
/* ----------------------------- USB interface ----------------------------- */
/* ------------------------------------------------------------------------- */

uchar usbFunctionSetup(uchar data[8])
{
usbRequest_t    *rq = (usbRequest_t*)((void *)data);

    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS){    /* class request type */

        if( rq->bRequest==GET_LINE_CODING || rq->bRequest==SET_LINE_CODING ){
            return 0xff;
        /*    GET_LINE_CODING -> usbFunctionRead()    */
        /*    SET_LINE_CODING -> usbFunctionWrite()    */
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
/* usbFunctionRead                                                          */
/*---------------------------------------------------------------------------*/
uchar usbFunctionRead( uchar *data, uchar len )
{
    // data[0] = 0;
    // data[1] = 0;
    // data[2] = 0;
    // data[3] = 0;
    // data[4] = 0;
    // data[5] = 0;
    // data[6] = 8;

    return 7;
}

/*---------------------------------------------------------------------------*/
/* usbFunctionWrite                                                          */
/*---------------------------------------------------------------------------*/
uchar usbFunctionWrite( uchar *data, uchar len )
{    
    // baud.bytes[0] = data[0];
    // baud.bytes[1] = data[1];

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
    if(RingBuffer_GetFreeCount(&rxBuf) < HW_CDC_BULK_OUT_SIZE)
    {
        usbDisableAllRequests();
    }
}


#ifdef __cplusplus
} // extern "C"
#endif

DigiCDCDevice SerialUSB;
