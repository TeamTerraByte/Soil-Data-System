#include <AltSoftSerial.h>

AltSoftSerial meshSerial;  // RX = 8, TX = 9 on Arduino Uno

void setup() {
  Serial.begin(9600);       // USB serial for debug
  meshSerial.begin(38400);   // Must match Meshtastic serial baud

  delay(500);

  Serial.println(F("Meshtastic RX demo: waiting for messages..."));
}

void loop() {
  // Read all incoming characters from Meshtastic and echo to USB serial
  while (meshSerial.available()) {
    char c = meshSerial.read();
    Serial.write(c);
  }
}
