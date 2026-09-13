/******************************************************************************
 * Adapted from the DigisparkCDC examples of the Digistump AVR core 1.7.5.
 * DigiCDC uses low-speed bulk endpoints, which the USB specification does not
 * allow, so some hosts may refuse the device. Only Linux has been tested with
 * DigiCDCFast; see README.md. Older notes on host compatibility:
 * http://digistump.com/board/index.php/topic,2720.msg13422.html#msg13422
 ******************************************************************************/
#include <DigiCDCFast.h>
void setup() {                
  // initialize the digital pin as an output.
  SerialUSB.begin(); 
  pinMode(1,OUTPUT);
}

// the loop routine runs over and over again forever:
void loop() {
  
  //turns led on and off based on sending 0 or 1 from serial terminal
  if (SerialUSB.available()) {
    char input = SerialUSB.read();
    if(input == '0')
      digitalWrite(1,LOW);
    else if(input == '1')
      digitalWrite(1,HIGH);
      
  }
  
   SerialUSB.delay(100);               // keep usb alive // can alos use SerialUSB.refresh();
}
