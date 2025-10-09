#include <SDI12.h>
#include <AltSoftSerial.h>       // ADD

// AltSoftSerial on Uno uses fixed pins: RX=8, TX=9
AltSoftSerial meshSerial;        // no pin arguments

#define SOIL_SENSOR_PIN 2
SDI12 enviroPro(SOIL_SENSOR_PIN);

String probeAddress = "C";
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 180000; // 3 minutes
const unsigned long POWER_STABILIZATION_DELAY = 5000; // 5 seconds

// ==== LTE Shield + ThingSpeak (TCP/HTTP) integration ====
#include <SparkFun_LTE_Shield_Arduino_Library.h>
#include <SoftwareSerial.h>
#include "secrets.h"   // must define API_WRITE_KEY

// ThingSpeak config
static const char THINGSPEAK_HOST[] = "api.thingspeak.com";
static const int  THINGSPEAK_PORT   = 80;
static const char THINGSPEAK_PATH[] = "/update.json";
static const char * THINGSPEAK_API_KEY = API_WRITE_KEY;

// APN for your carrier (default: T-Mobile USA). Change if needed.
static const char APN[] = "fast.t-mobile.com";

// Warmup parameters
static const uint8_t  PREFLIGHT_ATTEMPTS  = 4;
static const uint16_t PREFLIGHT_DELAY_MS  = 500;

// LTE shield uses pins 8 (TX) and 9 (RX)
static SoftwareSerial lteSerial(8, 9);
static LTE_Shield lte;

// Forward decls
static bool   warmUpDataPath(void);
static int    openSocketOnce(void);
static bool   writeAllHTTP(int socket, const String& headers, const String& body);
static bool   postThingSpeak(const String& input);
static bool   getThingSpeakFallback(const String& input);
static String urlEncode(const String& s);
static bool   tryParseFloat(const String& s, float& outVal);

// Convenience: send a line to ThingSpeak with POST (falls back to GET on failure)
static void sendToThingSpeak(const String& line) {
  if (postThingSpeak(line)) {
    Serial.println(F("[ThingSpeak] POST ok."));
  } else if (getThingSpeakFallback(line)) {
    Serial.println(F("[ThingSpeak] Fallback GET ok."));
  } else {
    Serial.println(F("[ThingSpeak] Send failed."));
  }
}
// =========================================================

void setup() {
  Serial.begin(9600);
  meshSerial.begin(38400);   // Mesh on D8/D9 via AltSoftSerial
  delay(POWER_STABILIZATION_DELAY);

  enviroPro.begin();
  while(!initializeProbe()) {
    delay(1000);
  }

  // ---- LTE shield init (ThingSpeak TCP/HTTP) ----
  lteSerial.begin(9600);
  if (lte.begin(lteSerial, 9600)) {
    Serial.println(F("LTE Shield connected!"));
  } else {
    Serial.println(F("ERROR: LTE Shield not responding."));
  }
  LTE_Shield_error_t apnErr = lte.setAPN(String(APN));
  Serial.print(F("APN set attempt: ")); Serial.println((int)apnErr);

  // Warm up PDP/DNS so first send is fast
  Serial.println(F("Warming up data path..."));
  bool warmed = false;
  for (uint8_t i = 0; i < PREFLIGHT_ATTEMPTS; i++) {
    if (warmUpDataPath()) { warmed = true; break; }
    unsigned long t0 = millis();
    while (millis() - t0 < PREFLIGHT_DELAY_MS) { lte.poll(); delay(10); }
  }
  Serial.println(warmed ? F("Data path warm.") : F("Warmup skipped/partial; proceeding."));
  // -----------------------------------------------

  takeMeasurements();
  lastMeasurement = millis();
}

void loop() {
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {
    enviroPro.begin();  // re-init SDI-12 if you power-cycle the sensor between reads
    takeMeasurements();
    lastMeasurement = millis();
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      sendCommand(command);
    }
  }

  delay(100);
}

void sendMesh(const String& s) {
  meshSerial.print(s);
  meshSerial.print('\n');
  Serial.print(F("Sent: "));
  Serial.println(s);
}

bool initializeProbe() {
  String response = sendCommand("?!");
  if (response.length() > 0) {
    probeAddress = response;
    return true;
  } else {
    Serial.println(F("Soil Probe C not detected"));
    probeAddress = "C";
    return false;
  }
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

  int startIndex = 1; // skip address char
  while (startIndex < data.length()) {
    int nextPlus  = data.indexOf('+', startIndex);
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

  sendToThingSpeak(outputData);
}

void parseTemperatureData(String data) {
  String outputData = "Temp";
  Serial.print(outputData);

  int startIndex = 1;
  while (startIndex < data.length()) {
    int nextPlus  = data.indexOf('+', startIndex);
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

  sendToThingSpeak(outputData);
}

String sendCommand(String command) {
  enviroPro.sendCommand(command);

  String response = "";
  unsigned long startTime = millis();
  const unsigned long timeout = 2000;

  while (millis() - startTime < timeout) {
    if (enviroPro.available()) {
      char c = enviroPro.read();
      if (c == '\n' || c == '\r') {
        if (response.length() > 0) return response;
      } else {
        response += c;
      }
    }
    delay(10);
  }
  return response;
}

// ==== Helpers from Uno-Sparkfun_TCP_Thingspeak.ino ====
static String urlEncode(const String& s) {
  String out; out.reserve(s.length() * 3);
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else if (c == ' ') {
      out += '+';
    } else {
      out += '%'; out += hex[(c >> 4) & 0x0F]; out += hex[c & 0x0F];
    }
  }
  return out;
}

static bool tryParseFloat(const String& s, float& outVal) {
  char buf[64];
  size_t n = s.length();
  if (n >= sizeof(buf)) return false;
  s.toCharArray(buf, sizeof(buf));
  outVal = atof(buf);
  if (outVal == 0.0f) {
    bool seenDigitOrDot = false;
    for (size_t i = 0; i < n; i++) {
      char ch = buf[i];
      if (ch == '\0') break;
      if (ch == '0' || ch == '.' || ch == '+' || ch == '-' || isspace((unsigned char)ch)) {
        if (ch == '0' || ch == '.') seenDigitOrDot = true;
        continue;
      }
      return false;
    }
    if (!seenDigitOrDot) return false;
  }
  return true;
}

static int openSocketOnce(void) {
  int s = lte.socketOpen(LTE_SHIELD_TCP);
  return s;
}

static bool writeAllHTTP(int socket, const String& headers, const String& body) {
  if (lte.socketWrite(socket, headers) != LTE_SHIELD_SUCCESS) return false;
  unsigned long t0 = millis(); while (millis() - t0 < 20) { lte.poll(); }
  if (lte.socketWrite(socket, body) != LTE_SHIELD_SUCCESS)   return false;
  return true;
}

static bool warmUpDataPath(void) {
  int s = openSocketOnce();
  if (s < 0) return false;
  if (lte.socketConnect(s, THINGSPEAK_HOST, THINGSPEAK_PORT) != LTE_SHIELD_SUCCESS) {
    lte.socketClose(s);
    return false;
  }
  String headReq;
  headReq.reserve(128);
  headReq  = "HEAD / HTTP/1.1\r\n";
  headReq += "Host: " + String(THINGSPEAK_HOST) + "\r\n";
  headReq += "Connection: close\r\n\r\n";
  bool ok = (lte.socketWrite(s, headReq) == LTE_SHIELD_SUCCESS);
  unsigned long t1 = millis(); while (millis() - t1 < 40) { lte.poll(); }
  lte.socketClose(s);
  return ok;
}

static bool postThingSpeak(const String& input) {
  String body; body.reserve(128 + input.length());
  body  = "api_key=" + String(THINGSPEAK_API_KEY);
  float v=0.0f; if (tryParseFloat(input, v)) body += "&field1=" + String(v, 6);
  body += "&status=" + urlEncode(input);

  String headers; headers.reserve(256);
  headers  = "POST " + String(THINGSPEAK_PATH) + " HTTP/1.1\r\n";
  headers += "Host: " + String(THINGSPEAK_HOST) + "\r\n";
  headers += "User-Agent: SparkFun-LTE-Arduino\r\n";
  headers += "Connection: close\r\n";
  headers += "Content-Type: application/x-www-form-urlencoded\r\n";
  headers += "Content-Length: " + String(body.length()) + "\r\n\r\n";

  int s = openSocketOnce();
  if (s < 0) {
    warmUpDataPath();
    s = openSocketOnce();
    if (s < 0) return false;
  }

  if (lte.socketConnect(s, THINGSPEAK_HOST, THINGSPEAK_PORT) != LTE_SHIELD_SUCCESS) {
    lte.socketClose(s);
    return false;
  }

  unsigned long tc = millis(); while (millis() - tc < 30) { lte.poll(); }
  bool ok = writeAllHTTP(s, headers, body);
  unsigned long t2 = millis(); while (millis() - t2 < 60) { lte.poll(); }
  lte.socketClose(s);
  return ok;
}

static bool getThingSpeakFallback(const String& input) {
  String qs = "api_key=" + String(THINGSPEAK_API_KEY);
  float v=0.0f; if (tryParseFloat(input, v)) qs += "&field1=" + String(v, 6);
  qs += "&status=" + urlEncode(input);

  String req; req.reserve(128 + qs.length());
  req  = "GET /update?" + qs + " HTTP/1.1\r\n";
  req += "Host: " + String(THINGSPEAK_HOST) + "\r\n";
  req += "User-Agent: SparkFun-LTE-Arduino\r\n";
  req += "Connection: close\r\n\r\n";

  int s = openSocketOnce();
  if (s < 0) {
    warmUpDataPath();
    s = openSocketOnce();
    if (s < 0) return false;
  }

  if (lte.socketConnect(s, THINGSPEAK_HOST, THINGSPEAK_PORT) != LTE_SHIELD_SUCCESS) {
    lte.socketClose(s);
    return false;
  }

  unsigned long tc = millis(); while (millis() - tc < 20) { lte.poll(); }
  bool ok = (lte.socketWrite(s, req) == LTE_SHIELD_SUCCESS);
  unsigned long t2 = millis(); while (millis() - t2 < 40) { lte.poll(); }

  lte.socketClose(s);
  return ok;
}
