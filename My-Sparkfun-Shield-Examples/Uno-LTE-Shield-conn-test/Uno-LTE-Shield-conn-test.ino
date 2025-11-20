/*
  Code by Jacob Poland with help from Jim Lindblom's 
  SparkFun LTE shield example.
*/

//Click here to get the library: http://librarymanager/All#SparkFun_LTE_Shield_Arduino_Library
#include <SparkFun_LTE_Shield_Arduino_Library.h>
#define SerialMonitor Serial
#define DEBUG true

/* ---------- Global Variables---- ----------------------------------- */
// Create a SoftwareSerial object to pass to the LTE_Shield library
SoftwareSerial lteSerial(8, 9);
// Create a LTE_Shield object to use throughout the sketch
LTE_Shield lte;
/* ------------------------------------------------------------------- */


/* ---------- Forward declarations ----------------------------------- */
String sendAT(const String &cmd, uint32_t timeout = 2000, bool dbg = DEBUG);
/* ------------------------------------------------------------------- */

void setup() {
  SerialMonitor.begin(9600);

  if ( lte.begin(lteSerial, 9600) ) {
    Serial.println(F("LTE Shield connected!"));
  }

  sendAT("AT"); // check that modem is responsive

  // Automatically configures the module to be compliant to the requirements of various Mobile Network Operators.
  sendAT("AT+UMNOPROF?");  // should return 2 for ATT
  sendAT("AT+CEREG?");  // verify RAT (Radio Access Tech.) registration, should show 0, 1
  sendAT("AT+COPS?");  // check operator (first number = 1 means manually selected, "310410" means ATT)
  sendAT("AT+CSQ");  // first returned number is signal quality rssi.
  sendAT("AT+CGATT=1"); // ensure PS attached
  sendAT("AT+CGACT=1,1");  // activate PDP context 1
  sendAT("AT+CGPADDR=1");  // confirm obtained IP address
  sendAT("AT+UDNSRN=0,\"www.google.com\"");  //  query google's DNS


  SerialMonitor.println(F("Ready to passthrough!\r\n"));
}

void loop() {
  if (Serial.available()) {
    lteSerial.write((char) Serial.read());
  }
  if (lteSerial.available()) {
    Serial.write((char) lteSerial.read());
  }
}


/* ===================================================================
   Simple AT helper – sends a command & collects reply until timeout
   =================================================================== */
String sendAT(const String &cmd, uint32_t timeout, bool dbg) {
  lteSerial.println(cmd);
  uint32_t t0 = millis();
  String buffer;
  while (millis() - t0 < timeout) {
    while (lteSerial.available()) {
      char c = lteSerial.read();
      buffer += c;
    }
  }
  if (dbg) {
    SerialMonitor.print(cmd); 
    SerialMonitor.print(F(" → ")); 
    SerialMonitor.println(buffer);
  }
  return buffer;
}