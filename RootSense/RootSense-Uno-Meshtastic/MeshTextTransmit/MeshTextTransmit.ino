#include <AltSoftSerial.h>

AltSoftSerial meshSerial;  // RX = 8, TX = 9 on Arduino Uno

void setup() {
  Serial.begin(38400);       // USB serial for debug
  meshSerial.begin(38400);   // Must match Meshtastic serial baud

  delay(500);

  Serial.println(F("Meshtastic TX demo: sending initial message..."));
  sendMesh("Hello from Arduino Uno TX!");
}

void loop() {
  // Periodically send a ping message
  static unsigned long last = 0;
  unsigned long now = millis();

  if (now - last > 15000UL) {  // every 15 seconds
    last = now;
    String msg = String("Ping ") + (now / 1000) + "s";
    sendMesh(msg);
  }
}

void sendMesh(const String &s) {
  // Meshtastic TEXTMSG mode: newline terminates the message
  meshSerial.print(s);
  meshSerial.print('\n');

  Serial.print(F("Sent: "));
  Serial.println(s);
}
