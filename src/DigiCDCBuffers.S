/*
  DigiCDCBuffers.S -- DigiCDCFast's default transmit and receive buffers and
  their sizes, as weak symbols. A sketch can define its own (DIGICDC_BUFFERS
  in DigiCDCDescriptor.h, as DigiCDCMedium.h does), and the linker takes those.
  (Defined here rather than in C++, where link-time optimization warns that
  buffers of different sizes don't match.)
*/
#include "DigiCDCBufferSizes.h"

    .section .bss
    .weak   digiCdcTxBuffer
digiCdcTxBuffer:
    .space  HW_CDC_TX_BUF_SIZE
    .weak   digiCdcRxBuffer
digiCdcRxBuffer:
    .space  HW_CDC_RX_BUF_SIZE

    .section .progmem.data, "a"
    .weak   digiCdcBufferSizes
digiCdcBufferSizes:
    .byte   HW_CDC_TX_BUF_SIZE, HW_CDC_RX_BUF_SIZE
