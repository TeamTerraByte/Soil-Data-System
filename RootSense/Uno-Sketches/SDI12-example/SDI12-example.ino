#include <SDI12.h>

// ---------------- Configuration ----------------
const bool DEBUG = true;
const uint8_t SOIL_SENSOR_PIN = 2;
const unsigned long POWER_STABILIZATION_DELAY = 5000;

// ---------------- SDI-12 ----------------
SDI12 sdi(SOIL_SENSOR_PIN);
String probeAddress = "C";

// ---------------- Helpers ----------------
String sdiSend(String cmd, unsigned long timeoutMs = 3000) {
  if (DEBUG) {
    Serial.print("SDI12 TX: ");
    Serial.println(cmd);
  }

  // SDI12 library requires String& (non-const), so cmd must be mutable
  sdi.sendCommand(cmd);

  String response;
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    while (sdi.available()) {
      char c = sdi.read();
      response += c;
    }
    delay(2);
  }

  if (DEBUG) {
    Serial.print("SDI12 RX: ");
    Serial.println(response);
  }
  response.trim();
  return response;
}

bool detectProbeAddress() {
  String resp = sdiSend(String("?!"), 1500);

  if (resp.length() == 1 && isalnum(resp[0])) {
    probeAddress = resp;
    if (DEBUG) {
      Serial.print("Detected probe address: ");
      Serial.println(probeAddress);
    }
    return true;
  }
  
  Serial.println("Response: " + resp);
  Serial.println("Response length: " + (String) resp.length());
  if (DEBUG) Serial.println("Probe not detected");
  return false;
}

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(9600);
  while (!Serial) { delay(1000); }

  if (DEBUG) Serial.println("Serial ready");

  delay(POWER_STABILIZATION_DELAY);

  sdi.begin();

  while (!detectProbeAddress()) {
    delay(1000);
  }

  if (DEBUG) {
    Serial.println("Enter SDI-12 commands (e.g. , ?!, " + probeAddress + "C0!, " + probeAddress + "D0!)");
  }
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      sdiSend(cmd);
    }
  }
  delay(2000);
}
