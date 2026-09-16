/*
  DigiCDCBuffers.S -- DigiCDCFast's default transmit and receive buffers and
  their sizes, as weak symbols. A sketch can define its own (DIGICDC_BUFFERS
  in DigiCDCDescriptor.h, as DigiCDCMedium.h does), and the linker takes those.
  (Defined here rather than in C++, where link-time optimization warns that
  buffers of different sizes don't match.)

  Also the default, do-nothing usbTransactionEnd, which the USB interrupt calls
  at the end of every transaction. A sketch that must do something while the
  interrupt still holds the processor -- collect a UART byte that would
  otherwise be overwritten, say -- defines its own. It runs with the driver's
  registers saved, so it may clobber r0, r16-r22, Y and the flags, and nothing
  else, which is why it has to be written in assembler, naked: the compiler
  saves nothing for a naked function and would use registers the driver kept.
  Keep it short, every cycle spent here delays the next packet.
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

    .section .text
    .weak   usbTransactionEnd
    .type   usbTransactionEnd, @function
usbTransactionEnd:
    ret
