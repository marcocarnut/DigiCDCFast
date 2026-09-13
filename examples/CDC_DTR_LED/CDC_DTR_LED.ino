/******************************************************************************
 * Adapted from the DigisparkCDC examples of the Digistump AVR core 1.7.5.
 * DigiCDC uses low-speed bulk endpoints, which the USB specification does not
 * allow, so some hosts may refuse the device. Only Linux has been tested with
 * DigiCDCFast; see README.md. Older notes on host compatibility:
 * http://digistump.com/board/index.php/topic,2720.msg13422.html#msg13422
 * setDtrPin() has not been tested with DigiCDCFast.
 ******************************************************************************/
#include <DigiCDCFast.h>
void setup() {                
  SerialUSB.begin();
  // initialize the digital pin 1 as an DTR output.
  SerialUSB.setDtrPin(1);
}

// the loop routine runs over and over again forever:
void loop() {
   SerialUSB.delay(10);
}
