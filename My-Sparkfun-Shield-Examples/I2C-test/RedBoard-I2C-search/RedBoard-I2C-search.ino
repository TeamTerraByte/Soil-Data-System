#include <Wire.h>

// Change these if you're using different pins:
const int SDA_PIN = 21;
const int SCL_PIN = 22;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("ESP32 I2C Scanner"));
  Serial.print(F("Initializing I2C on SDA="));
  Serial.print(SDA_PIN);
  Serial.print(F(", SCL="));
  Serial.println(SCL_PIN);

  // Initialize I2C as master on specified pins
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);  // 100 kHz is fine for most devices
}

void loop() {
  Serial.println(F("\nScanning I2C bus..."));

  uint8_t deviceCount = 0;

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.print(F("I2C device found at address 0x"));
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(F("  (OK)"));
      deviceCount++;
    } else if (error == 4) {
      Serial.print(F("Unknown error at address 0x"));
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  if (deviceCount == 0) {
    Serial.println(F("No I2C devices found."));
  } else {
    Serial.print(F("Scan complete. Devices found: "));
    Serial.println(deviceCount);
  }

  delay(2000);  // Wait a bit before scanning again
}
