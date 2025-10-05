/*
  Post lines from Serial to ThingSpeak via HTTP (no TLS)
  Hardware: SparkFun LTE Cat M1/NB-IoT Shield (u-blox SARA-R4/R410) + Arduino Uno
  Behavior:
  - Open Serial Monitor @ 9600 baud, "Newline" EOL
  - Each line you type is posted to ThingSpeak:
      * status=<entire line>
      * if the line parses as a number, also field1=<number>

  Notes:
  - Uses HTTP on port 80 (avoids TLS/private AT calls).
  - Retries socket open, splits HTTP write (headers/body), and falls back to GET on failure.
*/

// Library Manager: http://librarymanager/All#SparkFun_LTE_Shield_Arduino_Library
#include <SparkFun_LTE_Shield_Arduino_Library.h>
#include <SoftwareSerial.h>
#include <ctype.h>   // isspace
#include <stdlib.h>  // atof
#include "secrets.h" // defines: API_WRITE_KEY

// ---------- Config ----------
const char THINGSPEAK_HOST[] = "api.thingspeak.com";
const int  THINGSPEAK_PORT   = 80;                // HTTP
const char THINGSPEAK_PATH[] = "/update.json";

// ThingSpeak WRITE API KEY
const char * THINGSPEAK_API_KEY = API_WRITE_KEY;  // from secrets.h

// Correct APN for T-Mobile USA
const char APN[] = "fast.t-mobile.com"; // <- was misspelled before
// ----------------------------

SoftwareSerial lteSerial(8, 9); // Arduino pins to LTE shield UART (RX=8, TX=9)
LTE_Shield lte;

// ---------- Forward Declarations ----------
static String urlEncode(const String& s);
static bool   tryParseFloat(const String& s, float& outVal);
static int    openSocketWithRetry(uint8_t attempts, uint16_t delayMs);
static bool   writeAllHTTP(int socket, const String& headers, const String& body);
static bool   postThingSpeak(const String& input);
static bool   getThingSpeakFallback(const String& input);
static void   postToThingSpeak(const String& input);
// -----------------------------------------

void setup() {
  Serial.begin(9600);
  while (!Serial) { /* wait for native USB boards; harmless on Uno */ }

  if (lte.begin(lteSerial, 9600)) {
    Serial.println(F("LTE Shield connected!"));
  } else {
    Serial.println(F("ERROR: LTE Shield not responding."));
  }

  // Optional APN hint (correct signature for this library)
  LTE_Shield_error_t apnErr = lte.setAPN(String(APN));
  Serial.print(F("APN set attempt: ")); Serial.println((int)apnErr);

  Serial.println(F("Type a message. Send a Newline (\\n) to POST to ThingSpeak..."));
  Serial.println(F("If numeric, it will go into field1; full text always goes into 'status'."));
}

void loop() {
  static String line = "";
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      line.trim();
      if (line.length() > 0) {
        Serial.println("Posting: " + line);
        postToThingSpeak(line);
      }
      line = "";
    } else {
      line += c;
    }
  }
  lte.poll();
}

// Minimal URL encoder (ASCII-safe)
static String urlEncode(const String& s) {
  String out;
  out.reserve(s.length() * 3);
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else if (c == ' ') {
      out += '+';
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

// AVR-safe numeric parse using atof (strtof not available in avr-libc)
static bool tryParseFloat(const String& s, float& outVal) {
  char buf[64];
  size_t n = s.length();
  if (n >= sizeof(buf)) return false;
  s.toCharArray(buf, sizeof(buf));

  outVal = atof(buf); // on AVR, double == float (4 bytes)

  if (outVal == 0.0f) {
    bool seenDigitOrDot = false;
    for (size_t i = 0; i < n; i++) {
      char ch = buf[i];
      if (ch == '\0') break;
      if (ch == '0' || ch == '.' || ch == '+' || ch == '-' ||
          isspace((unsigned char)ch)) {
        if (ch == '0' || ch == '.') seenDigitOrDot = true;
        continue;
      }
      return false;
    }
    if (!seenDigitOrDot) return false;
  }
  return true;
}

// Try to open a socket a few times (gives the modem time to attach/activate PDP)
static int openSocketWithRetry(uint8_t attempts, uint16_t delayMs) {
  for (uint8_t i = 0; i < attempts; i++) {
    int s = lte.socketOpen(LTE_SHIELD_TCP);
    if (s >= 0) return s;
    Serial.println(F("socketOpen failed; retrying..."));
    unsigned long t0 = millis();
    while (millis() - t0 < delayMs) {
      lte.poll();
      delay(10);
    }
  }
  return -1;
}

// Write headers and body in two separate writes; small waits in between
static bool writeAllHTTP(int socket, const String& headers, const String& body) {
  if (lte.socketWrite(socket, headers) != LTE_SHIELD_SUCCESS) {
    Serial.println(F("ERROR: socketWrite(headers) failed"));
    return false;
  }
  // brief settle
  unsigned long t0 = millis();
  while (millis() - t0 < 100) { lte.poll(); }

  if (lte.socketWrite(socket, body) != LTE_SHIELD_SUCCESS) {
    Serial.println(F("ERROR: socketWrite(body) failed"));
    return false;
  }
  return true;
}

// Build and send POST; returns true on success
static bool postThingSpeak(const String& input) {
  // Prepare body as application/x-www-form-urlencoded
  String body;
  body.reserve(128 + input.length());
  body = "api_key=" + String(THINGSPEAK_API_KEY);

  float numericVal = 0.0f;
  if (tryParseFloat(input, numericVal)) {
    body += "&field1=" + String(numericVal, 6); // up to 6 dp
  }
  body += "&status=" + urlEncode(input);

  String headers;
  headers.reserve(256);
  headers  = "POST " + String(THINGSPEAK_PATH) + " HTTP/1.1\r\n";
  headers += "Host: " + String(THINGSPEAK_HOST) + "\r\n";
  headers += "User-Agent: SparkFun-LTE-Arduino\r\n";
  headers += "Connection: close\r\n";
  headers += "Content-Type: application/x-www-form-urlencoded\r\n";
  headers += "Content-Length: " + String(body.length()) + "\r\n";
  headers += "\r\n";

  int socket = openSocketWithRetry(8, 1500); // ~12s max
  if (socket < 0) {
    Serial.println(F("ERROR: socketOpen failed after retries"));
    return false;
  }

  Serial.println("Connecting socket " + String(socket) + " to " + String(THINGSPEAK_HOST) + ":" + String(THINGSPEAK_PORT));
  if (lte.socketConnect(socket, THINGSPEAK_HOST, THINGSPEAK_PORT) != LTE_SHIELD_SUCCESS) {
    Serial.println(F("ERROR: socketConnect failed"));
    lte.socketClose(socket);
    return false;
  }

  // small wait after connect (some firmware needs it)
  unsigned long t1 = millis();
  while (millis() - t1 < 150) { lte.poll(); }

  bool ok = writeAllHTTP(socket, headers, body);

  // Optional: quick settle/read window (not strictly required)
  unsigned long t2 = millis();
  while (millis() - t2 < 300) { lte.poll(); }

  if (lte.socketClose(socket) == LTE_SHIELD_SUCCESS) {
    Serial.println("Socket " + String(socket) + " closed");
  } else {
    Serial.println(F("WARN: socketClose failed"));
  }

  return ok;
}

// Super-compact fallback using GET (tiny request)
static bool getThingSpeakFallback(const String& input) {
  // Build GET /update?api_key=...&field1=...&status=...
  String qs = "api_key=" + String(THINGSPEAK_API_KEY);
  float numericVal = 0.0f;
  if (tryParseFloat(input, numericVal)) {
    qs += "&field1=" + String(numericVal, 6);
  }
  qs += "&status=" + urlEncode(input);

  String request;
  request.reserve(128 + qs.length());
  request  = "GET /update?" + qs + " HTTP/1.1\r\n";
  request += "Host: " + String(THINGSPEAK_HOST) + "\r\n";
  request += "User-Agent: SparkFun-LTE-Arduino\r\n";
  request += "Connection: close\r\n";
  request += "\r\n";

  int socket = openSocketWithRetry(6, 1200);
  if (socket < 0) {
    Serial.println(F("ERROR: socketOpen (GET) failed after retries"));
    return false;
  }

  Serial.println("Connecting socket " + String(socket) + " (GET) to " + String(THINGSPEAK_HOST) + ":" + String(THINGSPEAK_PORT));
  if (lte.socketConnect(socket, THINGSPEAK_HOST, THINGSPEAK_PORT) != LTE_SHIELD_SUCCESS) {
    Serial.println(F("ERROR: socketConnect (GET) failed"));
    lte.socketClose(socket);
    return false;
  }

  // brief settle
  unsigned long t1 = millis();
  while (millis() - t1 < 120) { lte.poll(); }

  bool ok = (lte.socketWrite(socket, request) == LTE_SHIELD_SUCCESS);
  if (!ok) Serial.println(F("ERROR: socketWrite(GET) failed"));

  unsigned long t2 = millis();
  while (millis() - t2 < 300) { lte.poll(); }

  if (lte.socketClose(socket) == LTE_SHIELD_SUCCESS) {
    Serial.println("Socket " + String(socket) + " closed (GET)");
  } else {
    Serial.println(F("WARN: socketClose (GET) failed"));
  }

  return ok;
}

static void postToThingSpeak(const String& input) {
  if (postThingSpeak(input)) {
    Serial.println(F("POST sent (check ThingSpeak for entry; may rate-limit)."));
    return;
  }
  Serial.println(F("POST failed; trying GET fallback..."));
  if (getThingSpeakFallback(input)) {
    Serial.println(F("GET sent (check ThingSpeak)."));
  } else {
    Serial.println(F("Both POST and GET sends failed."));
  }
}
