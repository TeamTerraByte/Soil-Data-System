// Arduino Mega RX-only Meshtastic reader
// Uses hardware UART Serial1 instead of AltSoftSerial

void setup() {
  Serial.begin(9600);        // USB serial for debug
  Serial1.begin(38400);      // Meshtastic UART baud (must match node)

  delay(500);

  Serial.println(F("Meshtastic RX demo: waiting for messages..."));
}

void loop() {
  // Read all incoming characters from Meshtastic and echo to USB serial
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.write(c);
  }
}
