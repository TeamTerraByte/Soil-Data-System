// Using Arduino Mega hardware UART (Serial1)
// RX1 = pin 19, TX1 = pin 18

void setup() {
  Serial.begin(9600);        // USB serial for debugging
  Serial1.begin(38400);      // Meshtastic serial baud

  delay(500);

  Serial.println(F("Meshtastic TX demo: sending initial message..."));
  sendMesh("Hello from Arduino Mega TX!");
}

void loop() {
  static unsigned long last = 0;
  unsigned long now = millis();

  // Send every 15 seconds
  if (now - last > 15000UL) {
    last = now;
    String msg = String("Ping ") + (now / 1000) + "s";
    sendMesh(msg);
  }
}

void sendMesh(const String &s) {
  // Meshtastic TEXTMSG mode: newline terminates the message
  Serial1.print(s);
  Serial1.print('\n');

  Serial.print(F("Sent: "));
  Serial.println(s);
}
