/*
  SparkFun ESP32 RedBoard as I2C master
  Sends a ThingSpeak field string to Arduino Uno I2C slave (0x36) in 30-byte chunks.
  A complete transmission is terminated with the characters "!F".
*/

#include <Wire.h>

#define UNO_I2C_ADDR    0x36      // Must match I2C_SLAVE_ADDR in Uno sketch
#define MAX_PAYLOAD_LEN 120       // Safety: keep below Uno's 128-byte buffer
#define I2C_CHUNK_SIZE  30        // Max bytes per I2C write() burst

// How often to send updates (ThingSpeak minimum is ~15 seconds)
const unsigned long SEND_INTERVAL_MS = 60000;
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);

  // Initialize I2C as master (ESP32 default SDA=21, SCL=22)
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
    // ------------------------------------------------------------
    float tempC    = 23.4;  // example temperature
    float moisture = 56.7;  // example moisture %
    int   status   = 1;     // example status flag
    int   battery  = 95;    // example battery %

    // ------------------------------------------------------------
    // 2. Build the ThingSpeak field string
    //    Example format:
    //    "field1=23.4&field2=56.7&field3=1&field4=95&field5=0"
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

    Serial.print(F("Base payload: "));
    Serial.println(payload);

    // ------------------------------------------------------------
    // 3. Append end-of-message marker "!F"
    //    The Uno will treat the message as complete when it sees "!F"
    // ------------------------------------------------------------
    String fullMessage = payload + "!F";

    Serial.print(F("Full message (with !F): "));
    Serial.println(fullMessage);
    Serial.print(F("Total length: "));
    Serial.println(fullMessage.length());

    // ------------------------------------------------------------
    // 4. Send in 30-byte chunks to the Uno over I2C
    // ------------------------------------------------------------
    sendToUnoChunked(fullMessage);
  }

  // Do other ESP32 work here (reading sensors, etc.)
}

// ===============================================================
// sendToUnoChunked: sends an ASCII string to Uno I2C slave
// in I2C_CHUNK_SIZE-byte chunks. The string is assumed to
// already contain the "!F" terminator.
// ===============================================================
void sendToUnoChunked(const String &msg) {
  size_t totalLen = msg.length();
  size_t offset   = 0;

  while (offset < totalLen) {
    size_t chunkLen = totalLen - offset;
    if (chunkLen > I2C_CHUNK_SIZE) {
      chunkLen = I2C_CHUNK_SIZE;
    }

    Wire.beginTransmission(UNO_I2C_ADDR);

    for (size_t i = 0; i < chunkLen; i++) {
      Wire.write((uint8_t)msg[offset + i]);
    }

    uint8_t err = Wire.endTransmission();  // 0 on success

    if (err == 0) {
      Serial.print(F("I2C chunk OK (offset "));
      Serial.print(offset);
      Serial.print(F(", len "));
      Serial.print(chunkLen);
      Serial.println(F(")"));
    } else {
      Serial.print(F("I2C chunk error "));
      Serial.print(err);
      Serial.print(F(" at offset "));
      Serial.println(offset);
      // You can choose to break or continue. Break is safer:
      break;
    }

    offset += chunkLen;

    // Small delay so the Uno can finish its ISR and main loop work
    delay(5);
  }
}
