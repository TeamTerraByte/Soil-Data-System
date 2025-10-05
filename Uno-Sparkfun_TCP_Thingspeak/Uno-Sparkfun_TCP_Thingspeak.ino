/*
  Fast-first-send: Post lines to ThingSpeak via HTTP (no TLS)
  Hardware: SparkFun LTE Cat M1/NB-IoT Shield (u-blox SARA-R4/R410) + Arduino Uno

  Strategy:
  - Warm up PDP/DNS during setup() using a tiny HEAD request.
  - When you press Enter, do a single fast socket open/connect and send.
*/

#include <SparkFun_LTE_Shield_Arduino_Library.h>
#include <SoftwareSerial.h>
#include <ctype.h>
#include <stdlib.h>
#include "secrets.h" // defines: API_WRITE_KEY

// ---------- Config ----------
const char THINGSPEAK_HOST[] = "api.thingspeak.com";
const int  THINGSPEAK_PORT   = 80;
const char THINGSPEAK_PATH[] = "/update.json";
const char * THINGSPEAK_API_KEY = API_WRITE_KEY;

// APN (T-Mobile USA)
const char APN[] = "fast.t-mobile.com";

// Warmup aggressiveness (during setup)
const uint8_t PREFLIGHT_ATTEMPTS   = 8;    // try up to ~8 quick warmups at boot
const uint16_t PREFLIGHT_DELAY_MS  = 500;  // short pause between tries
// ----------------------------

SoftwareSerial lteSerial(8, 9);
LTE_Shield lte;

static String urlEncode(const String& s);
static bool   tryParseFloat(const String& s, float& outVal);
static int    openSocketOnce(void);
static bool   writeAllHTTP(int socket, const String& headers, const String& body);
static bool   postThingSpeak(const String& input);
static bool   getThingSpeakFallback(const String& input);
static void   postToThingSpeak(const String& input);
static bool   warmUpDataPath(void);

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  if (lte.begin(lteSerial, 9600)) {
    Serial.println(F("LTE Shield connected!"));
  } else {
    Serial.println(F("ERROR: LTE Shield not responding."));
  }

  LTE_Shield_error_t apnErr = lte.setAPN(String(APN));
  Serial.print(F("APN set attempt: ")); Serial.println((int)apnErr);

  // ---------- Warm up PDP/DNS so first user send is fast ----------
  Serial.println(F("Warming up data path..."));
  bool warmed = false;
  for (uint8_t i = 0; i < PREFLIGHT_ATTEMPTS; i++) {
    if (warmUpDataPath()) { warmed = true; break; }
    unsigned long t0 = millis();
    while (millis() - t0 < PREFLIGHT_DELAY_MS) { lte.poll(); delay(10); }
  }
  Serial.println(warmed ? F("Data path warm.") : F("Warmup skipped/partial; proceeding."));

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

// ================= Helpers =================

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

  outVal = atof(buf); // on AVR, double == float

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

// Single, fast socket open
static int openSocketOnce(void) {
  int s = lte.socketOpen(LTE_SHIELD_TCP);
  return s; // <0 means not ready yet
}

// Split headers/body writes with tiny settles
static bool writeAllHTTP(int socket, const String& headers, const String& body) {
  if (lte.socketWrite(socket, headers) != LTE_SHIELD_SUCCESS) return false;
  unsigned long t0 = millis(); while (millis() - t0 < 20) { lte.poll(); } // ~20ms
  if (lte.socketWrite(socket, body) != LTE_SHIELD_SUCCESS)   return false;
  return true;
}

// Pre-flight: HEAD / to warm up DNS/PDP
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

  // brief settle
  unsigned long t1 = millis(); while (millis() - t1 < 40) { lte.poll(); }

  lte.socketClose(s);
  return ok;
}

// Build and send POST; single fast attempt
static bool postThingSpeak(const String& input) {
  // Body
  String body; body.reserve(128 + input.length());
  body  = "api_key=" + String(THINGSPEAK_API_KEY);
  float v=0.0f; if (tryParseFloat(input, v)) body += "&field1=" + String(v, 6);
  body += "&status=" + urlEncode(input);

  // Headers
  String headers; headers.reserve(256);
  headers  = "POST " + String(THINGSPEAK_PATH) + " HTTP/1.1\r\n";
  headers += "Host: " + String(THINGSPEAK_HOST) + "\r\n";
  headers += "User-Agent: SparkFun-LTE-Arduino\r\n";
  headers += "Connection: close\r\n";
  headers += "Content-Type: application/x-www-form-urlencoded\r\n";
  headers += "Content-Length: " + String(body.length()) + "\r\n\r\n";

  // One fast try
  int s = openSocketOnce();
  if (s < 0) {
    // quick one-time re-warm attempt then retry once
    warmUpDataPath();
    s = openSocketOnce();
    if (s < 0) return false;
  }

  if (lte.socketConnect(s, THINGSPEAK_HOST, THINGSPEAK_PORT) != LTE_SHIELD_SUCCESS) {
    lte.socketClose(s);
    return false;
  }

  // tiny settle after connect
  unsigned long tc = millis(); while (millis() - tc < 30) { lte.poll(); }

  bool ok = writeAllHTTP(s, headers, body);

  // short settle window
  unsigned long t2 = millis(); while (millis() - t2 < 60) { lte.poll(); }

  if (lte.socketClose(s) == LTE_SHIELD_SUCCESS) {
    Serial.println("Socket " + String(s) + " closed");
  } else {
    Serial.println(F("WARN: socketClose failed"));
  }

  return ok;
}

// Tiny GET fallback (rarely needed after warmup)
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

static void postToThingSpeak(const String& input) {
  if (postThingSpeak(input)) {
    Serial.println(F("POST sent (check ThingSpeak; free tier has rate limits)."));
    return;
  }
  Serial.println(F("POST failed; trying GET fallback..."));
  if (getThingSpeakFallback(input)) {
    Serial.println(F("GET sent (check ThingSpeak)."));
  } else {
    Serial.println(F("Both POST and GET sends failed."));
  }
}
