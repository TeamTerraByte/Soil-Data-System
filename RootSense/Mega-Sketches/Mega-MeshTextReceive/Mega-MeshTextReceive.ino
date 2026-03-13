// Arduino Mega RX-only Meshtastic reader
// Uses hardware UART MeshSerial instead of AltSoftSerial


#define RELAY_PIN 2

#define MeshSerial Serial1

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  Serial.begin(9600);        // USB serial for debug
  MeshSerial.begin(38400);      // Meshtastic UART baud (must match node)


  delay(500);

  Serial.println(F("Meshtastic RX demo: waiting for messages..."));
}

void loop() {
  // Read all incoming characters from Meshtastic and echo to USB serial
  while (MeshSerial.available()) {
    char c = MeshSerial.read();
    Serial.write(c);
  }
}
