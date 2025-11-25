#include <SDI12.h>
#include <Wire.h>

/* ==============================
   Logging macros (toggle/retarget)
   --------------------------------
   • Set ENABLE_LOG to 0 to strip all log calls at compile time
   • Set LOG_PORT to another HardwareSerial (e.g., SerialUSB) to retarget
   ============================== */
#ifndef ENABLE_LOG
#define ENABLE_LOG 1
#endif

#ifndef LOG_PORT
#define LOG_PORT Serial
#endif

#if ENABLE_LOG
  #define LOG_BEGIN(...)     LOG_PORT.begin(__VA_ARGS__)
  #define LOG_PRINT(...)     LOG_PORT.print(__VA_ARGS__)
  #define LOG_PRINTLN(...)   LOG_PORT.println(__VA_ARGS__)
  #define LOG_WRITE(...)     LOG_PORT.write(__VA_ARGS__)
#else
  #define LOG_BEGIN(...)
  #define LOG_PRINT(...)
  #define LOG_PRINTLN(...)
  #define LOG_WRITE(...)
#endif

// ------ ---------- SDI-12 & I2C ----------------
#define DATA_PIN 11
// Match Uno LTE slave address (I2C_SLAVE_ADDR 0x36)
#define SLAVE_ADDRESS 0x36

// I2C chunk size (leave headroom for Wire's 32-byte buffer)
const int I2C_CHUNK_SIZE = 30;

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
String measureSoilMoisture();
String measureTemperature();
String parseMoistureData(String data);
String parseTemperatureData(String data);
void transmitI2C(String data);  // sends already-formatted field string (with !F) in chunks
void sendFieldsToSlave(const String& deviceName,
                       const String& data,
                       const String& soilMoist,
                       const String& soilTemp);
bool initializeProbe();

// Meshtastic helpers
void meshBegin();
void meshSendLine(const String& line);
uint8_t meshQueryNodes(uint8_t requiredCount, unsigned long timeoutMs);
bool meshReadLine(String& outLine, unsigned long perCharTimeoutMs);
String parseLoRa(String data);

int queryNumber = 0;

void setup() {
  LOG_BEGIN(9600);
  Wire.begin();   // Mega as I2C master

  // Bring up Meshtastic serial
  meshBegin();

  delay(POWER_STABILIZATION_DELAY);

  // Initialize SDI-12 after power stabilization
  mySDI12.begin();

  // Initialize and find probe address
  while (!initializeProbe()) {
    delay(1000);
  }

  LOG_PRINTLN(F("Taking first measurement..."));
  takeMeasurements();
  lastMeasurement = millis();

  // --- Query mesh nodes once at startup ---
  LOG_PRINTLN(F("[Mesh] Querying nodes..."));
  uint8_t got = meshQueryNodes(REQUIRED_NODE_COUNT, MESH_TIMEOUT_MS);
  LOG_PRINT(F("[Mesh] Discovery complete. Responses: "));
  LOG_PRINTLN(got);
}

void loop() {
  // Periodic SDI-12 measurement
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {

    // local sensor measurement
    mySDI12.begin();
    takeMeasurements();
    lastMeasurement = millis();

    delay(500);  // delay so it doesn't block the meshtastic query?

    // other sensor measurements
    uint8_t got = meshQueryNodes(REQUIRED_NODE_COUNT, MESH_TIMEOUT_MS);
    LOG_PRINT(F("[Mesh] Discovery complete. Responses: "));
    LOG_PRINTLN(got);
    queryNumber += 1;
  }

  // (Optional) Non-blocking echo of any unsolicited mesh text to USB
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    LOG_WRITE(c);
  }

  delay(100);
}

// ---------------- Meshtastic implementation ----------------
void meshBegin() {
  Serial1.begin(MESH_BAUD);
  delay(100);
  LOG_PRINTLN(F("[Mesh] Serial1 started for Meshtastic."));
}

void meshSendLine(const String& line) {
  // TEXTMSG mode requires newline-terminated message
  Serial1.print(line);
  Serial1.print('\n');
  LOG_PRINT(F("[Mesh] Sent: "));
  LOG_PRINTLN(line);
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

String parseLoRa(String data) {
  // Find the position of ':' which separates the node ID from the rest
  int colonIndex = data.indexOf(':');
  if (colonIndex == -1) return ""; // Invalid format, return empty string

  // Extract node ID (everything before ':')
  String nodeID = data.substring(0, colonIndex);
  nodeID.trim(); // Remove any extra spaces

  // Find the first tab after "@hub" to locate where the real data starts
  int tabIndex = data.indexOf('\t', colonIndex);
  if (tabIndex == -1) return ""; // Invalid format, return empty string

  // Extract everything after the first tab
  String payload = data.substring(tabIndex + 1);
  payload.trim();

  // Combine node ID with the payload separated by a tab
  return nodeID + '\t' + payload;
}

// Sends @nodes and waits until we get `requiredCount` lines that begin with "@hub"
// or until `timeoutMs` is reached. Each valid response is printed and forwarded over I2C.
uint8_t meshQueryNodes(uint8_t requiredCount, unsigned long timeoutMs) {
  meshSendLine(String(NODES_QUERY) + " " + String(queryNumber));  

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

        String parsed_line = parseLoRa(line);
        LOG_PRINTLN(String("[Mesh] Parsed: ") + parsed_line);

        // parsed_line format: "nodeID\tpayload"
        String deviceName = F("mesh");
        String dataField  = parsed_line;
        int tabIdx = parsed_line.indexOf('\t');
        if (tabIdx > 0) {
          deviceName = parsed_line.substring(0, tabIdx);
          dataField  = parsed_line.substring(tabIdx + 1);
        }

        // For remote nodes we may not know soil moisture/temperature here
        String soilMoist = "";
        String soilTemp  = "";

        LOG_PRINTLN(String("Transmitting remote node over I2C as fields: dev=") +
                    deviceName + ", data=" + dataField);
        sendFieldsToSlave(deviceName, dataField, soilMoist, soilTemp);
      } else {
        // Still print other lines for visibility
        LOG_PRINT(F("[Mesh] Unfiltered: "));
        LOG_PRINTLN(line);
      }
    }
  }

  if (got < requiredCount) {
    LOG_PRINT(F("[Mesh] Timeout waiting for nodes. Got "));
    LOG_PRINT(got);
    LOG_PRINT(F(" of "));
    LOG_PRINTLN(requiredCount);
  }
  return got;
}

// ---------------- SDI-12 workflow ----------------
bool initializeProbe() {
  String response = sendCommand("?!");
  if (response.length() > 0) {
    probeAddress = response;
    return true;
  } else {
    LOG_PRINTLN("Soil Probe C not detected");
    probeAddress = "C";
    return false;
  }
  delay(1000);
}

void takeMeasurements() {
  String sm = measureSoilMoisture();
  delay(500);
  String st = measureTemperature();

  // Build ThingSpeak-style fields for local probe:
  // field1=deviceName, field2=data, field3=time, field4=soilMoist, field5=soilTemp
  String deviceName = F("hub");      // this Mega / hub
  String dataField  = F("local");    // generic data tag

  LOG_PRINTLN(String("Transmitting local SDI-12 over I2C as fields: dev=") +
              deviceName + ", data=" + dataField +
              ", sm=" + sm + ", st=" + st);

  sendFieldsToSlave(deviceName, dataField, sm, st);
}

String measureSoilMoisture() {
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
      return parseMoistureData(dataResponse);
    }
    else {
      return "Error measuring soil moisture";
    }
  }
  return "";
}

String measureTemperature() {
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
      return parseTemperatureData(dataResponse);
    }
    else {
      return "Error measuring soil temp";
    }
  }
  return "";
}

String parseMoistureData(String data) {
  String outputData = "Moist";

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
      LOG_PRINT(",");
      LOG_PRINT(value);
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
      startIndex = endNum;
    }
  }

  return outputData;
}

String parseTemperatureData(String data) {
  String outputData = "Temp";

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
      LOG_PRINT(",");
      LOG_PRINT(value);
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
      startIndex = endNum;
    }
  }

  return outputData;
}

// ----------------------------------------------------
// Build ThingSpeak-style field string and terminate with !F
// field1=deviceName, field2=data, field3=time, field4=soilMoist, field5=soilTemp
// ----------------------------------------------------
void sendFieldsToSlave(const String& deviceName,
                       const String& data,
                       const String& soilMoist,
                       const String& soilTemp) {
  String timeStr = String(millis());  // simple timestamp (ms since boot)

  String frame = "field1=" + deviceName +
                 "&field2=" + data +
                 "&field3=" + timeStr +
                 "&field4=" + soilMoist +
                 "&field5=" + soilTemp +
                 "!F";

  LOG_PRINT(F("[I2C] Frame: "));
  LOG_PRINTLN(frame);

  transmitI2C(frame);
}

// ----------------------------------------------------
// transmitI2C: send a prebuilt string (including !F) in 30-byte chunks
// ----------------------------------------------------
void transmitI2C(String data) {
  int dataLength = data.length();

  for (int offset = 0; offset < dataLength; offset += I2C_CHUNK_SIZE) {
    int chunkLen = dataLength - offset;
    if (chunkLen > I2C_CHUNK_SIZE) {
      chunkLen = I2C_CHUNK_SIZE;
    }

    String chunk = data.substring(offset, offset + chunkLen);

    Wire.beginTransmission(SLAVE_ADDRESS);
    delayMicroseconds(10);
    Wire.write(chunk.c_str(), chunk.length());

    byte error = Wire.endTransmission();
    if (error != 0) {
      LOG_PRINT(F("I2C Transmission error: "));
      LOG_PRINTLN(error);
    } else {
      LOG_PRINT(F("[I2C] Sent chunk: '"));
      LOG_PRINT(chunk);
      LOG_PRINTLN("'");
    }

    if (offset + chunkLen < dataLength) {
      delay(5); // brief pause between chunks
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
