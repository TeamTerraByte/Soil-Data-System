#include <SDI12.h>
#include <Wire.h>

// ---------------- SDI-12 & I2C ----------------
#define DATA_PIN 53
#define SLAVE_ADDRESS 0x08

SDI12 mySDI12(DATA_PIN);

String probeAddress = "C";
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 180000; // 3 minutes
const unsigned long POWER_STABILIZATION_DELAY = 5000; // 5 seconds

// ---------------- Meshtastic over Serial1 ----------------
// Mega2560: TX1 = 18, RX1 = 19 (cross-wire to Meshtastic RX/TX)
const unsigned long MESH_BAUD = 38400;
const unsigned long MESH_TIMEOUT_MS = 120000; // 2 minutes
const uint8_t REQUIRED_NODE_COUNT = 1;        // configurable device count
const char* HUB_PREFIX = "@hub";              // expected response prefix
const char* NODES_QUERY = "@nodes";           // query we send

// Forward declarations
String sendCommand(String command);
void takeMeasurements();
void measureSoilMoisture();
void measureTemperature();
void parseMoistureData(String data);
void parseTemperatureData(String data);
void transmitI2C(String data);

// Meshtastic helpers
void meshBegin();
void meshSendLine(const String& line);
uint8_t meshQueryNodes(uint8_t requiredCount, unsigned long timeoutMs);
bool meshReadLine(String& outLine, unsigned long perCharTimeoutMs);

void setup() {
  Serial.begin(9600);
  Wire.begin();

  // Bring up Meshtastic serial
  meshBegin();

  delay(POWER_STABILIZATION_DELAY);

  // Initialize SDI-12 after power stabilization
  mySDI12.begin();

  // Initialize and find probe address
  while (!initializeProbe()) {
    delay(1000);
  }

  Serial.println(F("Taking first measurement..."));
  takeMeasurements();
  lastMeasurement = millis();

  // --- Query mesh nodes once at startup ---
  Serial.println(F("[Mesh] Querying nodes..."));
  uint8_t got = meshQueryNodes(REQUIRED_NODE_COUNT, MESH_TIMEOUT_MS);
  Serial.print(F("[Mesh] Discovery complete. Responses: "));
  Serial.println(got);
}

void loop() {
  // Periodic SDI-12 measurement
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {
    mySDI12.begin();
    takeMeasurements();
    lastMeasurement = millis();
  }

  // Manual commands from USB Serial
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      if (command.equalsIgnoreCase("@nodes")) {
        Serial.println(F("[Mesh] Manual discovery requested."));
        uint8_t got = meshQueryNodes(REQUIRED_NODE_COUNT, MESH_TIMEOUT_MS);
        Serial.print(F("[Mesh] Discovery complete. Responses: "));
        Serial.println(got);
      } else {
        // Pass anything else to SDI-12 as before
        sendCommand(command);
      }
    }
  }

  // (Optional) Non-blocking echo of any unsolicited mesh text to USB
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    Serial.write(c);
  }

  delay(100);
}

// ---------------- Meshtastic implementation ----------------
void meshBegin() {
  Serial1.begin(MESH_BAUD);
  delay(100);
  Serial.println(F("[Mesh] Serial1 started for Meshtastic."));
}

void meshSendLine(const String& line) {
  // TEXTMSG mode requires newline-terminated message
  Serial1.print(line);
  Serial1.print('\n');
  Serial.print(F("[Mesh] Sent: "));
  Serial.println(line);
}

// Reads one newline-terminated line from Serial1 with a small per-character timeout.
// Returns true if a full line was read; false if timed out.
// outLine is trimmed and CR/LF are removed.
bool meshReadLine(String& outLine, unsigned long perCharTimeoutMs) {
  outLine = "";
  unsigned long lastCharTime = millis();
  while (true) {
    while (Serial1.available() > 0) {
      char c = Serial1.read();
      lastCharTime = millis();
      if (c == '\n') {
        outLine.trim(); // remove trailing \r if present and spaces
        return outLine.length() > 0;
      } else if (c != '\r') {
        outLine += c;
      }
    }
    if (millis() - lastCharTime > perCharTimeoutMs) {
      // no new chars within the per-char timeout: treat as no-line
      return false;
    }
    // tiny yield
    delay(2);
  }
}

// Sends @nodes and waits until we get `requiredCount` lines that begin with "@hub"
// or until `timeoutMs` is reached. Each valid response is printed and forwarded over I2C.
uint8_t meshQueryNodes(uint8_t requiredCount, unsigned long timeoutMs) {
  meshSendLine(String(NODES_QUERY));

  uint8_t got = 0;
  unsigned long start = millis();
  String line;

  // Per-character timeout (for line assembly); keeps outer overall timeout authoritative
  const unsigned long PER_CHAR_TO = 1500;

  while (millis() - start < timeoutMs && got < requiredCount) {
    // Try to read a line; if none arrives within PER_CHAR_TO, loop to check global timeout
    if (meshReadLine(line, PER_CHAR_TO)) {
      // Check for expected prefix
      if (line.indexOf(HUB_PREFIX) != -1) {
        got++;
        Serial.print(F("[Mesh] Got #"));
        Serial.print(got);
        Serial.print(F(": "));
        Serial.println(line);

        // Forward the line over I2C to the slave (keeps your existing pipeline)
        transmitI2C(line);
      } else {
        // Still print other lines for visibility
        Serial.print(F("[Mesh] Unfiltered: "));
        Serial.println(line);
      }
    }
  }

  if (got < requiredCount) {
    Serial.print(F("[Mesh] Timeout waiting for nodes. Got "));
    Serial.print(got);
    Serial.print(F(" of "));
    Serial.println(requiredCount);
  }
  return got;
}

// ---------------- SDI-12 workflow (unchanged) ----------------
bool initializeProbe() {
  String response = sendCommand("?!");
  if (response.length() > 0) {
    probeAddress = response;
    return true;
  } else {
    Serial.println("Soil Probe C not detected");
    probeAddress = "C";
    return false;
  }
  delay(1000);
}

void takeMeasurements() {
  measureSoilMoisture();
  delay(500);
  measureTemperature();
}

void measureSoilMoisture() {
  String measureCommand = probeAddress + "C0!";
  String response = sendCommand(measureCommand);

  if (response.length() > 0) {
    int measureTime = 3000;
    if (response.length() >= 6) {
      String timeStr = response.substring(0, 3);
      measureTime = timeStr.toInt() * 1000 + 1000;
    }
    delay(measureTime);

    String dataCommand = probeAddress + "D0!";
    String dataResponse = sendCommand(dataCommand);

    if (dataResponse.length() > 0) {
      parseMoistureData(dataResponse);
    }
  }
}

void measureTemperature() {
  String measureCommand = probeAddress + "C2!";
  String response = sendCommand(measureCommand);

  if (response.length() > 0) {
    int measureTime = 3000;
    if (response.length() >= 6) {
      String timeStr = response.substring(0, 3);
      measureTime = timeStr.toInt() * 1000 + 1000;
    }
    delay(measureTime);

    String dataCommand = probeAddress + "D0!";
    String dataResponse = sendCommand(dataCommand);

    if (dataResponse.length() > 0) {
      parseTemperatureData(dataResponse);
    }
  }
}

void parseMoistureData(String data) {
  String outputData = "Moist";
  Serial.print(outputData);

  int startIndex = 1;
  while (startIndex < data.length()) {
    int nextPlus = data.indexOf('+', startIndex);
    int nextMinus = data.indexOf('-', startIndex);
    int nextDelim = -1;

    if (nextPlus == -1 && nextMinus == -1) break;
    if (nextPlus == -1) nextDelim = nextMinus;
    else if (nextMinus == -1) nextDelim = nextPlus;
    else nextDelim = min(nextPlus, nextMinus);

    if (nextDelim > startIndex) {
      String value = data.substring(startIndex, nextDelim);
      outputData += ",";
      outputData += value;
      Serial.print(",");
      Serial.print(value);
    }

    startIndex = nextDelim + 1;
    if (startIndex < data.length() && (data.charAt(startIndex-1) == '+' || data.charAt(startIndex-1) == '-')) {
      int endNum = startIndex;
      while (endNum < data.length() && data.charAt(endNum) != '+' && data.charAt(endNum) != '-') {
        endNum++;
      }
      String value = String(data.charAt(startIndex-1)) + data.substring(startIndex, endNum);
      outputData += ",";
      outputData += value;
      Serial.print(",");
      Serial.print(value);
      startIndex = endNum;
    }
  }
  Serial.println();

  transmitI2C(outputData);
}

void parseTemperatureData(String data) {
  String outputData = "Temp";
  Serial.print(outputData);

  int startIndex = 1;
  while (startIndex < data.length()) {
    int nextPlus = data.indexOf('+', startIndex);
    int nextMinus = data.indexOf('-', startIndex);
    int nextDelim = -1;

    if (nextPlus == -1 && nextMinus == -1) break;
    if (nextPlus == -1) nextDelim = nextMinus;
    else if (nextMinus == -1) nextDelim = nextPlus;
    else nextDelim = min(nextPlus, nextMinus);

    if (nextDelim > startIndex) {
      String value = data.substring(startIndex, nextDelim);
      outputData += ",";
      outputData += value;
      Serial.print(",");
      Serial.print(value);
    }

    startIndex = nextDelim + 1;
    if (startIndex < data.length() && (data.charAt(startIndex-1) == '+' || data.charAt(startIndex-1) == '-')) {
      int endNum = startIndex;
      while (endNum < data.length() && data.charAt(endNum) != '+' && data.charAt(endNum) != '-') {
        endNum++;
      }
      String value = String(data.charAt(startIndex-1)) + data.substring(startIndex, endNum);
      outputData += ",";
      outputData += value;
      Serial.print(",");
      Serial.print(value);
      startIndex = endNum;
    }
  }
  Serial.println();

  transmitI2C(outputData);
}

void transmitI2C(String data) {
  int maxChunkSize = 30; // leave room in the Wire buffer
  int dataLength = data.length();

  for (int i = 0; i < dataLength; i += maxChunkSize) {
    int chunkEnd = min(i + maxChunkSize, dataLength);
    String chunk = data.substring(i, chunkEnd);

    Wire.beginTransmission(SLAVE_ADDRESS);
    delayMicroseconds(10);
    Wire.write(chunk.c_str(), chunk.length());

    if (chunkEnd >= dataLength) {
      Wire.write('\n');
    }

    byte error = Wire.endTransmission();
    if (error != 0) {
      Serial.print("I2C Transmission error: ");
      Serial.println(error);
    }

    if (chunkEnd < dataLength) {
      delay(10);
    }
  }
}

String sendCommand(String command) {
  mySDI12.sendCommand(command);

  String response = "";
  unsigned long startTime = millis();
  const unsigned long timeout = 2000;

  while (millis() - startTime < timeout) {
    if (mySDI12.available()) {
      char c = mySDI12.read();
      if (c == '\n' || c == '\r') {
        if (response.length() > 0) {
          return response;
        }
      } else {
        response += c;
      }
    }
    delay(10);
  }
  return response;
}
