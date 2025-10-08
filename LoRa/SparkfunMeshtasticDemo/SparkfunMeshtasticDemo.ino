// ESP32 version for SparkFun IoT RedBoard
// Uses HardwareSerial (recommended)

#include <Arduino.h>

static const int ESP_RX = 13; 
static const int ESP_TX = 4; 
static const uint32_t MESH_BAUD = 38400;  // Must match Meshtastic Serial Module

HardwareSerial meshSerial(1);    // Use UART1 (you can also use 2)

void setup() {
  Serial.begin(115200);  // USB debug console (UART0 via USB)
  // Attach RX/TX pins to UART1 and set framing
  meshSerial.begin(MESH_BAUD, SERIAL_8N1, ESP_RX, ESP_TX);
  // SERIAL_8N1 means 8 data bits, no parity, 1 stop bit.

  delay(500);
  Serial.println(F("Meshtastic TEXTMSG demo: sending message..."));
  sendMesh("Hello from ESP32!");
}

void loop() {
  while (meshSerial.available()) {
    Serial.write(meshSerial.read());
  }
  static unsigned long last = 0;
  if (millis() - last > 15000UL) {
    last = millis();
    sendMesh("Ping " + String(millis()/1000) + "s");
  }
}

void sendMesh(const String& s) {
  meshSerial.print(s);
  meshSerial.print('\n');  // TEXTMSG expects newline-terminated lines
  Serial.print(F("Sent: "));
  Serial.println(s);
}
