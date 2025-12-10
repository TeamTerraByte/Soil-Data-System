#include <AltSoftSerial.h>

AltSoftSerial meshSerial;  // RX=8, TX=9 on Arduino Uno (fixed pins)

void setup() {
  Serial.begin(38400);       // USB Serial for debugging
  meshSerial.begin(38400);   // Must match Meshtastic Serial Module baud

  delay(500);

  Serial.println(F("Meshtastic TEXTMSG demo: sending message..."));
  sendMesh("Hello from Arduino Uno!");
}

void loop() {
  // Echo any incoming mesh text back to USB serial
  while (meshSerial.available()) {
    char c = meshSerial.read();
    Serial.write(c);
  }

  // Example: send every 15 seconds
  static unsigned long last = 0;
  if (millis() - last > 15000UL) {
    last = millis();
    sendMesh(String("Ping ") + (millis() / 1000) + "s");
  }
}

void sendMesh(const String& s) {
  // Meshtastic text mode: newline = end of message
  meshSerial.print(s);
  meshSerial.print('\n');

  Serial.print(F("Sent: "));
  Serial.println(s);
}
