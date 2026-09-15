/*
  DigiCDCDescriptor.h -- the USB configuration descriptor of DigiCDCFast, for
  given packet sizes (1 to 8 bytes) of the data endpoints.

  DigiCDCFast.cpp defines it weakly with 8-byte packets. A sketch that includes
  DigiCDCMedium.h defines it with 2-byte packets instead, and the linker takes
  that one. The library reads the packet sizes back from the descriptor. The
  buffers are chosen the same way.
*/
#ifndef __DigiCDCDescriptor_h__
#define __DigiCDCDescriptor_h__

#include "usbdrv.h"

#define DIGICDC_DESCRIPTOR_SIZE     67
#define DIGICDC_OUT_PACKET_SIZE_AT  57  /* offsets of the data endpoints' maximum packet sizes */
#define DIGICDC_IN_PACKET_SIZE_AT   64

#ifdef __cplusplus
extern "C" {
#endif
extern const uchar digiCdcConfigDescriptor[DIGICDC_DESCRIPTOR_SIZE];
/* The transmit and receive buffers, and their sizes (in flash): 64 and 32
   bytes in DigiCDCBuffers.S, unless the sketch defines them too (DIGICDC_BUFFERS) */
extern uint8_t digiCdcTxBuffer[], digiCdcRxBuffer[];
extern const uint8_t digiCdcBufferSizes[2];
#ifdef __cplusplus
}
#endif

/* Define the buffers with other sizes (1 to 255 bytes; the receive buffer must
   hold a whole packet), in one file of the sketch */
#define DIGICDC_BUFFERS(txSize, rxSize)                                     \
    uint8_t digiCdcTxBuffer[txSize];                                        \
    uint8_t digiCdcRxBuffer[rxSize];                                        \
    const uint8_t digiCdcBufferSizes[2] PROGMEM = {(txSize), (rxSize)}

#define DIGICDC_CONFIG_DESCRIPTOR(outSize, inSize) {                        \
   /* USB configuration descriptor */                                                                 \
    9,          /* sizeof(usbDescrConfig): length of descriptor in bytes */                           \
    USBDESCR_CONFIG,    /* descriptor type */                                                         \
    67,                                                                                               \
    0,          /* total length of data returned (including inlined descriptors) */                   \
    2,          /* number of interfaces in this configuration */                                      \
    1,          /* index of this configuration */                                                     \
    0,          /* configuration name string index */                                                 \
    (1 << 7) | (USB_CFG_IS_SELF_POWERED ? USBATTR_SELFPOWER : 0),  /* attributes */                   \
    USB_CFG_MAX_BUS_POWER/2,            /* max USB current in 2mA units */                            \
                                                                                                      \
    /* interface descriptor follows inline: */                                                        \
    9,          /* sizeof(usbDescrInterface): length of descriptor in bytes */                        \
    USBDESCR_INTERFACE, /* descriptor type */                                                         \
    0,          /* index of this interface */                                                         \
    0,          /* alternate setting for this interface */                                            \
    USB_CFG_HAVE_INTRIN_ENDPOINT,   /* endpoints excl 0: number of endpoint descriptors to follow */  \
    USB_CFG_INTERFACE_CLASS,                                                                          \
    USB_CFG_INTERFACE_SUBCLASS,                                                                       \
    USB_CFG_INTERFACE_PROTOCOL,                                                                       \
    0,          /* string index for interface */                                                      \
                                                                                                      \
    /* CDC Class-Specific descriptor */                                                               \
    5,           /* sizeof(usbDescrCDC_HeaderFn): length of descriptor in bytes */                    \
    0x24,        /* descriptor type */                                                                \
    0,           /* header functional descriptor */                                                   \
    0x10, 0x01,                                                                                       \
                                                                                                      \
    4,           /* sizeof(usbDescrCDC_AcmFn): length of descriptor in bytes    */                    \
    0x24,        /* descriptor type */                                                                \
    2,           /* abstract control management functional descriptor */                              \
    0x02,        /* SET_LINE_CODING, GET_LINE_CODING, SET_CONTROL_LINE_STATE    */                    \
                                                                                                      \
    5,           /* sizeof(usbDescrCDC_UnionFn): length of descriptor in bytes  */                    \
    0x24,        /* descriptor type */                                                                \
    6,           /* union functional descriptor */                                                    \
    0,           /* CDC_COMM_INTF_ID */                                                               \
    1,           /* CDC_DATA_INTF_ID */                                                               \
                                                                                                      \
    5,           /* sizeof(usbDescrCDC_CallMgtFn): length of descriptor in bytes */                   \
    0x24,        /* descriptor type */                                                                \
    1,           /* call management functional descriptor */                                          \
    3,           /* allow management on data interface, handles call management by itself */          \
    1,           /* CDC_DATA_INTF_ID */                                                               \
                                                                                                      \
    /* Endpoint Descriptor */                                                                         \
    7,           /* sizeof(usbDescrEndpoint) */                                                       \
    USBDESCR_ENDPOINT,  /* descriptor type = endpoint */                                              \
    0x80|USB_CFG_EP3_NUMBER,        /* IN endpoint number 3 */                                        \
    0x03,        /* attrib: Interrupt endpoint */                                                     \
    8, 0,        /* maximum packet size */                                                            \
    USB_CFG_INTR_POLL_INTERVAL,        /* in ms */                                                    \
                                                                                                      \
    /* Interface Descriptor  */                                                                       \
    9,           /* sizeof(usbDescrInterface): length of descriptor in bytes */                       \
    USBDESCR_INTERFACE,           /* descriptor type */                                               \
    1,           /* index of this interface */                                                        \
    0,           /* alternate setting for this interface */                                           \
    2,           /* endpoints excl 0: number of endpoint descriptors to follow */                     \
    0x0A,        /* Data Interface Class Codes */                                                     \
    0,                                                                                                \
    0,           /* Data Interface Class Protocol Codes */                                            \
    0,           /* string index for interface */                                                     \
                                                                                                      \
    /* Endpoint Descriptor */                                                                         \
    7,           /* sizeof(usbDescrEndpoint) */                                                       \
    USBDESCR_ENDPOINT,  /* descriptor type = endpoint */                                              \
    0x01,        /* OUT endpoint number 1 */                                                          \
    0x02,        /* attrib: Bulk endpoint */                                                          \
    (outSize), 0,  /* maximum packet size */                                                          \
    0,           /* in ms */                                                                          \
                                                                                                      \
    /* Endpoint Descriptor */                                                                         \
    7,           /* sizeof(usbDescrEndpoint) */                                                       \
    USBDESCR_ENDPOINT,  /* descriptor type = endpoint */                                              \
    0x81,        /* IN endpoint number 1 */                                                           \
    0x02,        /* attrib: Bulk endpoint */                                                          \
    (inSize), 0,   /* maximum packet size */                                                          \
    0,           /* in ms */                                                                          \
}

#endif
