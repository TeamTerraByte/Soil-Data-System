#include <SoftwareSerial.h>

static const uint8_t UNO_RX = 10;
static const uint8_t UNO_TX = 11; 

SoftwareSerial meshSerial(UNO_RX, UNO_TX); // RX, TX

void setup() {
  Serial.begin(38400);        // USB serial for debug (PC)
  meshSerial.begin(38400);    // Must match Meshtastic Serial Module baud
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
    sendMesh("Ping " + String(millis()/1000) + "s");
  }
}

void sendMesh(const String& s) {
  // In TEXTMSG mode, a newline-terminated line is a message.
  meshSerial.print(s);
  meshSerial.print('\n');
  Serial.print(F("Sent: "));
  Serial.println(s);
}
