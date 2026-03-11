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

#define RELAY_PIN 2

// ------ ---------- SDI-12 ----------------
#define DATA_PIN 11
SDI12 mySDI12(DATA_PIN);
String probeAddress = "C";
unsigned long lastMeasurement = 0;
#define MEASUREMENT_INTERVAL 3600000 // 1 hour
// const unsigned long MEASUREMENT_INTERVAL = 120000UL; // 2 minutes
#define POWER_STABILIZATION_DELAY 5000 // 5 seconds

// ---------------- Meshtastic over Serial1 ----------------
const unsigned long MESH_BAUD = 38400;
#define MESH_TIMEOUT_MS 300000 // 5 minutes
#define MESH_RETRY_DELAY 60000 // 1 minute

const int NUM_WORKERS = 1;
const char* WORKERS[] = {  
  "@w1",
  // "@w2"
};

// Data structure for bulk upload
struct SensorData {
  String device;
  String moist;
  String temp;
};

const int MAX_SENSORS = 10;  // Hub + workers
SensorData sensorReadings[MAX_SENSORS];
int sensorCount = 0;

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
void storeSensorData(String device, String moist, String temp);
void bulkUploadToThingSpeak();
ParsedMessage parseMessage(const String& input);

// Meshtastic helpers
void meshBegin();
void meshSendLine(const String& line);
uint8_t meshQueryNodes(unsigned long timeoutMs);
bool meshReadLine(String& outLine, unsigned long perCharTimeoutMs);

// LTE Notecard stuff
#define productUID "edu.tamu.ag.jacob.poland:rootsense"
Notecard notecard;

void setup() {
  LOG_BEGIN(9600);
  pinMode(RELAY_PIN, OUTPUT);
  
  // Turn ON LoRa 32, SDI-12 sensor, and Notecard
  digitalWrite(RELAY_PIN, HIGH);
  delay(POWER_STABILIZATION_DELAY);
  
  notecard.begin();
  {
    J *req = notecard.newRequest("hub.set");
    if (req != NULL) {
      JAddStringToObject(req, "product", productUID);
      JAddStringToObject(req, "mode", "minimum"); 
      notecard.sendRequest(req);
    }
  }
  
  // Bring up Meshtastic serial
  meshBegin();
  
  // Initialize SDI-12 after power stabilization
  mySDI12.begin();
  
  // Initialize and find probe address
  while (!findProbe()) {
    delay(1000);
  }
  
  // Reset sensor count
  sensorCount = 0;
  
  // Step 1: Query hub's own soil sensor
  LOG_PRINTLN(F("Step 1: Querying hub sensor..."));
  takeMeasurements();
  
  // Step 2: Query each worker's soil sensors
  LOG_PRINTLN(F("Step 2: Querying worker sensors..."));
  uint8_t got = meshQueryNodes(MESH_TIMEOUT_MS);
  LOG_PRINT(F("[Mesh] Worker queries complete. Responses: "));
  LOG_PRINTLN(got);
  
  // Step 3: Upload all collected data to ThingSpeak
  LOG_PRINTLN(F("Step 3: Uploading all data to ThingSpeak..."));
  bulkUploadToThingSpeak();
  
  delay(60000);  // Generous delay for upload completion
  
  lastMeasurement = millis();
  
  // Step 4: Turn OFF LoRa 32, SDI-12 sensor, and Notecard
  LOG_PRINTLN(F("Step 4: Powering down..."));
  digitalWrite(RELAY_PIN, LOW);
}

void loop() {
  // Periodic SDI-12 measurement
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {
    // Turn ON sensor, LoRa, and LTE
    digitalWrite(RELAY_PIN, HIGH);
    delay(POWER_STABILIZATION_DELAY);
    
    // Reset sensor count
    sensorCount = 0;
    
    // Step 1: Query hub's own soil sensor
    LOG_PRINTLN(F("Step 1: Querying hub sensor..."));
    mySDI12.begin();
    takeMeasurements();
    
    // Step 2: Query each worker's soil sensors
    LOG_PRINTLN(F("Step 2: Querying worker sensors..."));
    uint8_t got = meshQueryNodes(MESH_TIMEOUT_MS);
    LOG_PRINT(F("[Mesh] Worker queries complete. Responses: "));
    LOG_PRINTLN(got);
    
    // Step 3: Upload all collected data to ThingSpeak
    LOG_PRINTLN(F("Step 3: Uploading all data to ThingSpeak..."));
    bulkUploadToThingSpeak();
    
    delay(60000);  // Generous delay for upload completion
    
    lastMeasurement = millis();
    
    // Step 4: Turn OFF sensor, LoRa, and LTE
    LOG_PRINTLN(F("Step 4: Powering down..."));
    digitalWrite(RELAY_PIN, LOW);
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
  Serial1.print(line);
  Serial1.print('\n');
  LOG_PRINT(F("[Mesh] Sent: "));
  LOG_PRINTLN(line);
}

bool meshReadLine(String& outLine, unsigned long perCharTimeoutMs) {
  outLine = "";
  unsigned long lastCharTime = millis();
  while (true) {
    while (Serial1.available() > 0) {
      char c = Serial1.read();
      lastCharTime = millis();
      if (c == '\n') {
        outLine.trim();
        return outLine.length() > 0;
      } else if (c != '\r') {
        outLine += c;
      }
    }
    if (millis() - lastCharTime > perCharTimeoutMs) {
      return false;
    }
    delay(2);
  }
}

String parseLoRa(String data) {
  int colonIndex = data.indexOf(':');
  if (colonIndex == -1) return "";
  
  String nodeID = data.substring(0, colonIndex);
  nodeID.trim();
  
  int tabIndex = data.indexOf('\t', colonIndex);
  if (tabIndex == -1) return "";
  
  String payload = data.substring(tabIndex + 1);
  payload.trim();
  
  return nodeID + '\t' + payload;
}

uint8_t meshQueryNodes(unsigned long timeoutMs) {
  const unsigned long PER_CHAR_TO = 1500;
  unsigned long start = 0;
  unsigned long lastRetry = 0;
  String line = "";
  uint8_t recvd = 0;
  
  for (int i = 0; i < NUM_WORKERS; i++) {
    const String QUERY    = String(WORKERS[i]) + "q Measure";
    const String RESPONSE = String(WORKERS[i]) + "r";
    String sleepResponse = "";
    
    start = millis();
    meshSendLine(QUERY);
    lastRetry = millis();
    
    while (millis() - start < timeoutMs) {
      if (meshReadLine(line, PER_CHAR_TO)) {
        if (line.indexOf(RESPONSE) != -1) {
          String payload = parseLoRa(line);
          LOG_PRINT(F("Parsed data from "));
          LOG_PRINT(WORKERS[i]);
          LOG_PRINT(F(": "));
          LOG_PRINTLN(line);
          
          recvd++;
          ParsedMessage pm = parseMessage(line);
          
          // Store worker data instead of uploading immediately
          storeSensorData(pm.header, pm.moist, pm.temp);
          
          bool workerAsleep = false;
          // Command worker to sleep
          for (int sleepAttempt = 0; sleepAttempt < 5; sleepAttempt++){
            meshSendLine(String(WORKERS[i]) + "q Sleep");
            for (int sleepDelay = 0; sleepDelay < 10; sleepDelay++){
              delay(500);
              if (meshReadLine(sleepResponse, PER_CHAR_TO)){
                break;
              }
            }
            if (sleepResponse.indexOf(String(WORKERS[i]) + " Status Sleeping") != -1){
              workerAsleep = true;
              break;
            }
          }
          if (!workerAsleep){
            LOG_PRINT(WORKERS[i]);
            LOG_PRINTLN(F(" failed to sleep"));
          }
          
          break;
        }
      }
      if (millis() - lastRetry > MESH_RETRY_DELAY){
        lastRetry = millis();
        LOG_PRINTLN(F("[Mesh] Retrying"));
        meshSendLine(QUERY);
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
    LOG_PRINTLN(F("Soil Probe C not detected"));
    probeAddress = "C";
    return false;
  }
}

void takeMeasurements() {
  String sm = measureSoilMoisture();
  delay(500);
  String st = measureTemperature();
  
  LOG_PRINTLN(F("hub"));
  LOG_PRINTLN(sm);
  LOG_PRINTLN(st);
  
  // Store hub data instead of uploading immediately
  storeSensorData("hub", sm, st);
}

String measureSoilMoisture() {
  String measureCommand = probeAddress + "C0!";
  String dataCommand = probeAddress + "D0!";
  const int MAX_TRIES = 3;
  int measureTime = 3000;
  
  for (int try_num = 0; try_num < MAX_TRIES; try_num++) {
    LOG_PRINT(F("Moisture Measure Attempt # "));
    LOG_PRINTLN(try_num);
    
    String response = sendCommand(measureCommand);
    delay(measureTime);
    String dataResponse = sendCommand(dataCommand);
    
    if (dataResponse.length() == 57) {
      return parseMoistureData(dataResponse);
    }
  }
  return "Error measuring soil moisture";
}

String measureTemperature() {
  String measureCommand = probeAddress + "C2!";
  String dataCommand = probeAddress + "D0!";
  const int MAX_TRIES = 3;
  int measureTime = 3000;
  
  for (int try_num = 0; try_num < MAX_TRIES; try_num++) {
    LOG_PRINT(F("Temperature Measure Attempt # "));
    LOG_PRINTLN(try_num);
    
    String response = sendCommand(measureCommand);
    delay(measureTime);
    String dataResponse = sendCommand(dataCommand);
    
    if (dataResponse.length() == 57) {
      return parseTemperatureData(dataResponse);
    }
  }
  return "Error measuring soil temp";
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

// Store sensor data for bulk upload
void storeSensorData(String device, String moist, String temp) {
  if (sensorCount < MAX_SENSORS) {
    sensorReadings[sensorCount].device = device;
    sensorReadings[sensorCount].moist = moist;
    sensorReadings[sensorCount].temp = temp;
    sensorCount++;
    
    LOG_PRINT(F("Stored data for "));
    LOG_PRINT(device);
    LOG_PRINT(F(" ("));
    LOG_PRINT(sensorCount);
    LOG_PRINTLN(F(" total)"));
  } else {
    LOG_PRINTLN(F("ERROR: Sensor data buffer full!"));
  }
}

// Bulk upload all collected data to ThingSpeak
void bulkUploadToThingSpeak() {
  if (sensorCount == 0) {
    LOG_PRINTLN(F("No data to upload"));
    return;
  }
  
  LOG_PRINT(F("Uploading "));
  LOG_PRINT(sensorCount);
  LOG_PRINTLN(F(" sensor readings..."));
  
  // Build JSON for bulk upload
  J *req = notecard.newRequest("note.add");
  if (req != NULL) {
    JAddStringToObject(req, "file", "sensors.qo");
    JAddBoolToObject(req, "sync", true);
    
    J *body = JAddObjectToObject(req, "body");
    if (body) {
      // Create array of sensor data
      J *dataArray = JCreateArray();
      
      for (int i = 0; i < sensorCount; i++) {
        J *dataPoint = JCreateObject();
        JAddStringToObject(dataPoint, "delta_t", "1");  // All points use delta_t = 1
        JAddStringToObject(dataPoint, "dev", sensorReadings[i].device.c_str());
        JAddStringToObject(dataPoint, "moist", sensorReadings[i].moist.c_str());
        JAddStringToObject(dataPoint, "temp", sensorReadings[i].temp.c_str());
        JAddItemToArray(dataArray, dataPoint);
      }
      
      JAddItemToObject(body, "readings", dataArray);
    }
    
    J* rsp = notecard.requestAndResponse(req);
    if (rsp) {
      LOG_PRINTLN(JConvertToJSONString(rsp));
      notecard.deleteResponse(rsp);
      LOG_PRINTLN(F("Bulk upload complete"));
    } else {
      LOG_PRINTLN(F("ERROR: Bulk upload failed"));
    }
  } else {
    LOG_PRINTLN(F("ERROR: Failed to create bulk upload request"));
  }
  
  // Reset for next cycle
  sensorCount = 0;
}

ParsedMessage parseMessage(const String& input) {
  ParsedMessage out;
  
  int atW = input.indexOf("@w");
  if (atW != -1) {
    int r = input.indexOf('r', atW);
    if (r != -1) {
      out.header = input.substring(0, r + 1);
    }
  }
  
  int moistStart = input.indexOf("Moist");
  if (moistStart == -1) return out;
  
  int t1 = input.indexOf('\t', moistStart);
  if (t1 == -1) return out;
  
  out.moist = input.substring(moistStart, t1);
  
  int tempStart = t1 + 1;
  int t2 = input.indexOf('\t', tempStart);
  if (t2 == -1) {
    out.temp = input.substring(tempStart);
    out.temp.trim();
    return out;
  }
  
  out.temp = input.substring(tempStart, t2);
  out.temp.trim();
  
  return out;
}