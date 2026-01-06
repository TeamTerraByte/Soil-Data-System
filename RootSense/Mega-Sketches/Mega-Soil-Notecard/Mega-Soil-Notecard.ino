#include <SDI12.h>
#include <Notecard.h>  // for LTE

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

// ------ ---------- SDI-12 ----------------
#define DATA_PIN 11

SDI12 mySDI12(DATA_PIN);
String probeAddress = "C";
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 3600000; 
const unsigned long POWER_STABILIZATION_DELAY = 5000; // 5 seconds

// ---------------- Meshtastic over Serial1 ----------------
// Mega2560: TX1 = 18, RX1 = 19 (cross-wire to Meshtastic RX/TX)
const unsigned long MESH_BAUD = 38400;
const unsigned long MESH_TIMEOUT_MS = 120000; // 2 minutes
const uint8_t REQUIRED_NODE_COUNT = 1;        // configurable device count
const char* BOSS_PREFIX = "@boss";              // expected response prefix
const char* WORKERS_QUERY = "@workers";           // query we send
const int NUM_WORKERS = 2;
const char* WORKERS[] = {
  "@w1",
  "@w2"
};

struct ParsedMessage {
  String header;  // @w{x}r
  String moist;   // Moist,...
  String temp;    // Temp,...
};

// Forward declarations
String sendCommand(String command);
void takeMeasurements();
String measureSoilMoisture();
String measureTemperature();
String parseMoistureData(String data);
String parseTemperatureData(String data);
bool findProbe();
void uploadNote(String device, String moist, String temp);
ParsedMessage parseMessage(const String& input);
void waitForSyncCompletion(unsigned long pollIntervalMs);

// Meshtastic helpers
void meshBegin();
void meshSendLine(const String& line);
uint8_t meshQueryNodes(unsigned long timeoutMs);
bool meshReadLine(String& outLine, unsigned long perCharTimeoutMs);
int queryNumber = 0;

// LTE Notecard stuff
#define productUID "edu.tamu.ag.jacob.poland:rootsense"
Notecard notecard;
void splitPayload(const String& input,
                  String& device,
                  String& moist,
                  String& temp);


void setup() {
  delay(2500);
  LOG_BEGIN(9600);
  
  notecard.begin();
  notecard.setDebugOutputStream(Serial);
  {
    J *req = notecard.newRequest("hub.set");
    if (req != NULL) {
      JAddStringToObject(req, "product", productUID);
      JAddStringToObject(req, "mode", "continuous");
      notecard.sendRequest(req);
    }
  }

  // Bring up Meshtastic serial
  meshBegin();

  delay(POWER_STABILIZATION_DELAY);

  // Initialize SDI-12 after power stabilization
  mySDI12.begin();

  // Initialize and find probe address
  while (!findProbe()) {
    delay(1000);
  }

  LOG_PRINTLN(F("Taking first measurement..."));
  takeMeasurements();
  lastMeasurement = millis();

  // --- Query mesh nodes once at startup ---
  LOG_PRINTLN(F("[Mesh] Querying nodes..."));
  uint8_t got = meshQueryNodes(MESH_TIMEOUT_MS);
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
    uint8_t got = meshQueryNodes(MESH_TIMEOUT_MS);
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


// TODO: combine this with splitPayload
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


void splitPayload(const String& input,
                  String& device,
                  String& moist,
                  String& temp)
{
  int t1 = input.indexOf('\t');
  if (t1 == -1) return;

  int t2 = input.indexOf('\t', t1 + 1);
  if (t2 == -1) return;

  device = input.substring(0, t1);
  moist  = input.substring(t1 + 1, t2);
  temp   = input.substring(t2 + 1);
}


uint8_t meshQueryNodes(unsigned long timeoutMs) {
  const unsigned long PER_CHAR_TO = 1500;
  unsigned long start = 0;
  String line = "";
  uint8_t recvd = 0;

  for (int i = 0; i < NUM_WORKERS; i++) {
    start = millis();

    const String QUERY    = String(WORKERS[i]) + "q";
    const String RESPONSE = String(WORKERS[i]) + "r";

    meshSendLine(QUERY);  // q represents query

    while (millis() - start < timeoutMs) {
      if (meshReadLine(line, PER_CHAR_TO)) {
        if (line.indexOf(RESPONSE) != -1) {  // r represents response
          String payload = parseLoRa(line);

          Serial.println(String("Parsed data from ") + WORKERS[i] + ":" + line);
          recvd++;
          
          ParsedMessage pm = parseMessage(line);
          uploadNote(pm.header, pm.moist, pm.temp);

          break;  // TODO: figure out why sometimes I get two responses from the same device. 
          // Maybe it's getting relayed from a different device. 
        }
      }
    }
  }
  return recvd;
}

// ---------------- SDI-12 workflow ----------------
bool findProbe() {
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
  Serial.println("hub"); // hub
  Serial.println(sm);    // Moist,+004.65,...
  Serial.println(st);    // Temp,+020.45,...

  uploadNote("hub", sm, st);
}

// TODO: Combine common code with measureTemperature
String measureSoilMoisture() {
  String measureCommand = probeAddress + "C0!";
  String dataResponse = "";
  String dataCommand = probeAddress + "D0!";
  const int MAX_TRIES = 3;
  int try_num = 0;
  int measureTime = 3000;
  String response = "";

  while (try_num < MAX_TRIES){
    Serial.println("Moisture Measure Attempt # " + String(try_num));
    response = sendCommand(measureCommand);
    delay(measureTime);
    String dataResponse = sendCommand(dataCommand);

    if (dataResponse.length() == 57) {
      return parseMoistureData(dataResponse);
    }

    try_num++;
  }
  return "Error measuring soil moisture";
}

String measureTemperature() {
  String measureCommand = probeAddress + "C2!";
  String dataResponse = "";
  String dataCommand = probeAddress + "D0!";
  const int MAX_TRIES = 3;
  int try_num = 0;
  int measureTime = 3000;
  String response = "";

  while (try_num < MAX_TRIES) {
    Serial.println("Temperature Measure Attempt # " + String(try_num));
    response = sendCommand(measureCommand);
    delay(measureTime);
    dataResponse = sendCommand(dataCommand);

    if (dataResponse.length() == 57) {
      return parseTemperatureData(dataResponse);
    }
    try_num++;
  }

  return "Error measuring soil temp";
}

// TODO: Combine common code with parseTemperatureData
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

// TODO: Combine common code with parseMoistureData
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


void uploadNote(String device, String moist, String temp){
    J *req = notecard.newRequest("note.add");
    if (req != NULL) {
      JAddStringToObject(req, "file", "sensors.qo");
      JAddBoolToObject(req, "sync", true);
      J *body = JAddObjectToObject(req, "body");
      if (body) {
        JAddStringToObject(body, "dev", device.c_str());
        JAddStringToObject(body, "moist", moist.c_str());
        JAddStringToObject(body, "temp", temp.c_str());
      }
      notecard.sendRequest(req);
    }
}



ParsedMessage parseMessage(const String& input) {
  ParsedMessage out;

  int len = input.length();

  int headerEnd = -1;
  int moistStart = -1;
  int moistEnd = -1;
  int tempStart = -1;

  // Locate boundaries in one scan
  for (int i = 0; i < len; i++) {
    char c = input[i];

    // End of header (@w{x}r ends at 'r')
    if (headerEnd < 0 && c == 'r') {
      headerEnd = i + 1;
    }

    // Moist section starts
    if (moistStart < 0 && c == 'M') {
      moistStart = i;
    }

    // Tab ends Moist section
    if (moistStart >= 0 && moistEnd < 0 && c == '\t') {
      moistEnd = i;
      tempStart = i + 1;
      break;
    }
  }

  // Extract substrings (no whitespace included)
  if (headerEnd > 0)
    out.header = input.substring(0, headerEnd);

  if (moistStart >= 0 && moistEnd > moistStart)
    out.moist = input.substring(moistStart, moistEnd);

  if (tempStart >= 0)
    out.temp = input.substring(tempStart);

  return out;
}


void waitForSyncCompletion(unsigned long pollIntervalMs = 2000) {
  while (true) {
    // Create hub.sync.status request
    J *req = notecard.newRequest("hub.sync.status");
    if (req == NULL) {
      Serial.println("ERROR: Failed to create hub.sync.status request");
      return;
    }

    // Send request and get response
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) {
      Serial.println("ERROR: No response from Notecard");
      return;
    }

    // Check for Notecard-level error
    const char *err = JGetString(rsp, "err");
    if (err != NULL) {
      Serial.print("Notecard error: ");
      Serial.println(err);
      JDelete(rsp);
      return;
    }

    // Fields documented by hub.sync.status
    bool alert = JGetBool(rsp, "alert");
    int completed = JGetInt(rsp, "completed");
    int requested = JGetInt(rsp, "requested");

    Serial.print("sync.status -> ");
    Serial.print("requested=");
    Serial.print(requested);
    Serial.print("s, completed=");
    Serial.print(completed);
    Serial.print("s, alert=");
    Serial.println(alert ? "true" : "false");

    JDelete(rsp);

    // If completed > 0, the sync finished
    if (completed > 0) {
      if (alert) {
        Serial.println("SYNC COMPLETED WITH ERROR");
      } else {
        Serial.println("SYNC COMPLETED SUCCESSFULLY");
      }
      break;
    }

    delay(pollIntervalMs);
  }
}




