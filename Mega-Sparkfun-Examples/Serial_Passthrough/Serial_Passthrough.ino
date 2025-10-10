/*
  Passthrough AT console for SparkFun LTE Cat M1/NB-IoT Shield (SARA-R4)
  Target: Arduino Mega 2560 (uses HardwareSerial2 on pins TX2=D16, RX2=D17)

  IMPORTANT WIRING (cross-over):
    Shield TX  -> Mega RX2 (D17)
    Shield RX  -> Mega TX2 (D16)
  Set shield power selector: PWR_SEL = ARDUINO
  In the SparkFun LTE library, ensure SoftwareSerial is DISABLED if you use SDI-12 etc.
*/

#include <SparkFun_LTE_Shield_Arduino_Library.h>

// Use HardwareSerial2 on the Mega
#define lteSerial Serial1

LTE_Shield lte;

void setup() {
  // USB serial for your terminal
  Serial.begin(9600);
  Serial.println("Serial began");
  // Start the UART to the modem BEFORE handing it to the library
  lteSerial.begin(9600);
  Serial.println("lteSerial began");

  // Give the modem a moment to boot (helps on some boards)
  delay(200);

  // Initialize the library against Serial2 at 9600
  bool ok = lte.begin(lteSerial, 9600);
  if (ok) {
    Serial.println(F("LTE Shield connected!"));
  } else {
    Serial.println(F("ERROR: lte.begin() failed. Check power, wiring, and jumpers."));
  }

  Serial.println(F("Ready to passthrough! (Set Serial Monitor line ending to CR)"));
}

void loop() {
  // PC -> Modem
  while (Serial.available() > 0) {
    int b = Serial.read();
    if (b >= 0) lteSerial.write((uint8_t)b);
  }
  // Modem -> PC
  while (lteSerial.available() > 0) {
    int b = lteSerial.read();
    if (b >= 0) Serial.write((uint8_t)b);
  }
}
