/*
  SparkFun ESP32 RedBoard as I2C master
  Sends a ThingSpeak field string to Arduino Uno I2C slave (0x08).

  Matching the Uno slave code you’re using:
  - Uno expects a single ASCII string, e.g.:
      "field1=10&field2=20&field3=30&field4=40&field5=50"
  - Uno will prepend api_key=...& and send to ThingSpeak via LTE.
*/

#include <Wire.h>

#define UNO_I2C_ADDR 0x36      // Must match I2C_SLAVE_ADDR in Uno sketch
#define MAX_PAYLOAD_LEN 120    // Safety: keep below Uno's 128-byte buffer

// How often to send updates (ThingSpeak minimum is ~15 seconds)
const unsigned long SEND_INTERVAL_MS = 120000;
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);

  // Initialize I2C as master.
  // On ESP32, Wire.begin() with no args uses default SDA/SCL (often 21/22).
  // If needed, you can specify pins: Wire.begin(SDA_PIN, SCL_PIN);
  // Wire.begin();
  Wire.begin(21, 22);
  Wire.setClock(100000);

  Serial.println(F("ESP32 I2C master ready. Will send field data to Uno at 0x36"));
}

void loop() {
  unsigned long now = millis();
  if (now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;

    // ------------------------------------------------------------
    // 1. Get or compute the values you want to send
    //    Replace these with your real sensor reads
    // ------------------------------------------------------------
    float tempC    = 23.4;  // example temperature
    float moisture = 56.7;  // example moisture %
    int   status   = 1;     // example status flag
    int   battery  = 95;    // example battery %

    // ------------------------------------------------------------
    // 2. Build the ThingSpeak field string
    //    This must match what the Uno expects:
    //    "field1=...&field2=...&field3=...&field4=...&field5=..."
    // ------------------------------------------------------------
    String payload;

    payload  = "field1=" + String(tempC, 1);      // one decimal place
    payload += "&field2=" + String(moisture, 1);  // one decimal place
    payload += "&field3=" + String(status);
    payload += "&field4=" + String(battery);
    payload += "&field5=0";                       // spare field / placeholder

    // Make sure we don’t exceed the slave buffer
    if (payload.length() > MAX_PAYLOAD_LEN) {
      Serial.print(F("Payload too long ("));
      Serial.print(payload.length());
      Serial.println(F(" bytes). Truncating."));
      payload.remove(MAX_PAYLOAD_LEN);
    }

    Serial.print(F("Sending to Uno: "));
    Serial.println(payload);

    // ------------------------------------------------------------
    // 3. Send the string to the Uno over I2C
    // ------------------------------------------------------------
    sendToUno(payload);
  }

  // Do other ESP32 work here (reading sensors, etc.)
}

// ===============================================================
// sendToUno: sends an ASCII string to Uno I2C slave
// Matches the Uno's onReceive handler that strips CR/LF and
// expects a single message per transmission.
// ===============================================================
void sendToUno(const String &msg) {
  Wire.beginTransmission(UNO_I2C_ADDR);

  // Write raw bytes of the string (no terminator, no CR/LF)
  for (size_t i = 0; i < msg.length(); i++) {
    Wire.write((uint8_t)msg[i]);
  }

  uint8_t err = Wire.endTransmission();  // 0 on success

  if (err == 0) {
    Serial.println(F("I2C transmission OK."));
  } else {
    Serial.print(F("I2C transmission error: "));
    Serial.println(err);
  }
}
