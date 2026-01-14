// Blues Swan Feather - Using hardware Serial1 (RX/TX pins on left side)
// RX pin connects to Meshtastic TX
// TX pin connects to Meshtastic RX

HardwareSerial &meshSerial = Serial1; // RX/TX pins on the Feather header

void setup() {
  Serial.begin(38400);        // USB serial for debug (PC via USB-C)
  meshSerial.begin(38400);    // Must match Meshtastic Serial Module baud
  
  delay(500);
  Serial.println("Meshtastic TEXTMSG demo: sending message...");
  sendMesh("Hello from Blues Swan!");
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
  Serial.print("Sent: ");
  Serial.println(s);
}