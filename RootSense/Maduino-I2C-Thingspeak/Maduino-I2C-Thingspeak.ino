/* =====================================================================
   Maduino Zero – I²C Slave + LTE HTTP Uploader (ThingSpeak)
   ---------------------------------------------------------------------
   • Receives CSV sensor frames over I²C from an Arduino master
   • Buffers the latest Moisture and Temperature values (add more if needed)
   • Powers-up & configures the on-board SIM7600 LTE module
   • Uploads the readings to ThingSpeak using HTTP GET
   ---------------------------------------------------------------------
   Required wiring (Makerfabs Maduino Zero LTE):
     LTE_RESET_PIN  ➜ SIM7600 RESET    (active-LOW)
     LTE_PWRKEY_PIN ➜ SIM7600 PWRKEY   (active-LOW, via NPN inverter on PCB)
     LTE_FLIGHT_PIN ➜ SIM7600 FLIGHT   (LOW = RF ON)
   ---------------------------------------------------------------------
   Adjust the following before use:
     • SLAVE_ADDRESS – I²C address seen by the master
     • APN           – Your carrier’s APN string
     • API_WRITE_KEY – Your private ThingSpeak write key
   ===================================================================== */

#include <Arduino.h>
#include <Wire.h>
#include "secrets.h"

/* ---------- User config -------------------------------------------- */
#define SLAVE_ADDRESS  0x08                 // I²C address of this Maduino
// #define APN            "fast.t-mobile.com"  // old Carrier APN
// #define APN            "PHONE"  // new Carrier APN (AT&T)
#define APN            "m2m.com.attz"  // new Carrier APN (AT&T)
/* ------------------------------------------------------------------- */

/* ---------- LTE control pins --------------------------------------- */
#define LTE_RESET_PIN  6   // active-LOW
#define LTE_PWRKEY_PIN 5   // active-LOW pulse ≥100 ms
#define LTE_FLIGHT_PIN 7   // LOW = normal operation
/* ------------------------------------------------------------------- */

const bool DEBUG = true;            // echo AT chatter to SerialUSB

/* ---------- Upload queue (SPSC ring buffer) ------------------------ */
static const uint8_t QUEUE_CAPACITY = 8;
volatile uint8_t qHead = 0;     // next index to write
volatile uint8_t qTail = 0;     // next index to read
String uploadQueue[QUEUE_CAPACITY]; // payloads to upload (concatenated lines)

/* Enqueue payload; returns true on success, false if full (drop new) */
bool enqueueUpload(const String &payload) {
  uint8_t nextHead = (qHead + 1) % QUEUE_CAPACITY;
  if (nextHead == qTail) {
    // queue full – drop newest to avoid overwriting in-flight data
    if (DEBUG) SerialUSB.println(F("! Queue full, dropping payload"));
    return false;
  }
  uploadQueue[qHead] = payload;
  qHead = nextHead; // single producer: safe
  return true;
}

/* Dequeue payload; returns true if an item was read */
bool dequeueUpload(String &out) {
  if (qTail == qHead) return false; // empty
  out = uploadQueue[qTail];
  uploadQueue[qTail] = ""; // free memory
  qTail = (qTail + 1) % QUEUE_CAPACITY; // single consumer: safe
  return true;
}

/* ---------- I²C frame buffering / assembly ------------------------- */
// Data arrives in chunks. Each *unit* is newline ('\n') terminated.
// We assemble complete lines until dataBuf is at least 140 chars,
// then push that as *one* string into the queue.
String dataBuf = "";                 // assembly accumulator for current batch
String rxRemainder = "";             // holds a partial line between callbacks
volatile bool assembling = false;    // true while running receiveEvent
/* ------------------------------------------------------------------- */

/* ---------- Upload pacing / overlap control ------------------------ */
bool uploading = false;                       // guarded by loop()
const unsigned long UPLOAD_GAP_MS = 1500;     // small delay between uploads
unsigned long nextUploadAllowed = 0;          // earliest time we may start
/* ------------------------------------------------------------------- */

/* ---------- Forward declarations ----------------------------------- */
String sendAT(const String &cmd, uint32_t timeout = 2000, bool dbg = DEBUG);
void   ltePowerSequence();
void   uploadData(const String &payload);
void   receiveEvent(int numBytes);
String removePrefix(String data, String prefix); // (kept for compatibility)
/* ------------------------------------------------------------------- */

// ---------------------------- Custom classes ------------------------
class DateTime {
public:
  String yr;    
  String mon;    
  String day;   
  String hr;     
  String min;   
  String sec;   
  String timeStr;

  DateTime() = default;

  explicit DateTime(const String& core) : timeStr(core) {
    if (core.length() >= 17) {
      yr  = core.substring(0, 2);
      mon = core.substring(3, 5);
      day = core.substring(6, 8);
      hr  = core.substring(9, 11);
      min = core.substring(12, 14);
      sec = core.substring(15, 17);
    }
  }

  // Static: retrieves time directly from the modem
  static DateTime getTime() {
    String r = sendAT("AT+CCLK?");
    int a = r.indexOf('"');
    int b = r.indexOf('"', a + 1);

    if (a < 0 || b <= a) {
      DateTime dt;
      dt.timeStr = "";
      return dt;
    }

    String core = r.substring(a + 1, b); // e.g. "24/10/10,20:10:00-20"

    int tzPos = core.indexOf('+');
    if (tzPos < 0) tzPos = core.indexOf('-');
    if (tzPos > 0) core.remove(tzPos);

    if (!(core.length() >= 17 &&
          core[2] == '/' && core[5] == '/' &&
          core[8] == ',' &&
          core[11] == ':' && core[14] == ':')) {
      DateTime dt;
      dt.timeStr = core;
      return dt;
    }
    return DateTime(core);
  }

  String formatted() const {
    return yr + "/" + mon + "/" + day + " " + hr + ":" + min + ":" + sec;
  }
};

class SoilData {
public:
  String meshName;
  String moistData;
  String tempData;

  // Constructor that parses a single input string in the format:
  // "meshName\tMoist\tTemp"
  SoilData(String input) {
    int firstTab = input.indexOf('\t');
    int secondTab = input.indexOf('\t', firstTab + 1);

    if (firstTab == -1 || secondTab == -1) {
      meshName = "";
      moistData = "";
      tempData = "";
      return;
    }

    meshName = input.substring(0, firstTab);
    moistData = input.substring(firstTab + 1, secondTab);
    tempData = input.substring(secondTab + 1);

    // remove Moist and Temp prefixes
    moistData = SoilData::removePrefix(moistData, ", ");
    tempData = SoilData::removePrefix(tempData, ", ");

    meshName.trim();
    moistData.trim();
    tempData.trim();
  }

  static String removePrefix(String data, String delim) {
    int pos = data.indexOf(delim);
    if (pos != -1) {
      data.remove(0, pos + delim.length());
    }
    return data;
  }
};
// -------------------------- end custom classes ----------------------


void setup() {
  /* Serial ports ----------------------------------------------------- */
  SerialUSB.begin(115200);
  Serial1.begin(115200);            // LTE module on Serial1
  
  delay(5000);
  SerialUSB.println(F("Maduino I²C + LTE uploader booting…"));

  /* LTE GPIO --------------------------------------------------------- */
  pinMode(LTE_RESET_PIN,  OUTPUT);
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  pinMode(LTE_FLIGHT_PIN, OUTPUT);

  /* I²C slave -------------------------------------------------------- */
  Wire.begin(SLAVE_ADDRESS);
  Wire.onReceive(receiveEvent);

  SerialUSB.println(F("Modem ready – waiting for sensor frames…"));
}

void loop() {
  // Upload worker: single-consumer side of the queue
  if (!uploading && millis() >= nextUploadAllowed) {
    String payload;
    if (dequeueUpload(payload)) {
      uploading = true;                 // prevent overlap
      uploadData(payload);
      uploading = false;
      nextUploadAllowed = millis() + UPLOAD_GAP_MS; // pacing
    }
  }
}

/* ===================================================================
   I²C receive callback – chunked input, newline-delimited "units".
   We assemble complete lines into dataBuf until it reaches ≥140 chars,
   then push that *as one string* into the upload queue.
   =================================================================== */
void receiveEvent(int numBytes) {
  assembling = true;

  String chunk;
  chunk.reserve(numBytes + 8);
  while (Wire.available()) {
    char c = Wire.read();
    chunk += c;
  }
  if (chunk.length() == 0) {
    assembling = false;
    return;
  }

  // Prepend any partial trailing data from previous callback
  String work = rxRemainder + chunk;
  rxRemainder = "";

  // Process complete lines
  int start = 0;
  while (true) {
    int nl = work.indexOf('\n', start);
    if (nl < 0) break;  // no complete line left
    String unit = work.substring(start, nl);
    start = nl + 1;

    unit.trim();                // drop \r and whitespace
    if (unit.length() == 0) continue;

    // Append to current batch (keep '\n' as separator while assembling)
    if (dataBuf.length() > 0) dataBuf += '\n';
    dataBuf += unit;

    // When batch reaches threshold, push into queue and start a new batch
    if (dataBuf.length() >= 140) {
      // Try to enqueue; on failure (queue full) we drop the batch
      if (enqueueUpload(dataBuf)) {
        if (DEBUG) {
          SerialUSB.print(F("Queued payload ("));
          SerialUSB.print(dataBuf.length());
          SerialUSB.println(F(" bytes)"));
        }
      }
      dataBuf = ""; // reset assembly for the next batch
    }
  }

  // Any leftover partial line becomes the remainder for next time
  if (start < (int)work.length()) {
    rxRemainder = work.substring(start);
  }

  assembling = false;
}

bool waitForRegistration(uint32_t timeoutMs = 120000) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    String r = sendAT("AT+CEREG?", 5000);
    // Look for ,1 or ,5 (home or roaming)
    if (r.indexOf(",1") != -1 || r.indexOf(",5") != -1) {
      SerialUSB.println(F("✓ Network registered"));
      return true;
    }
    SerialUSB.println(F("… still searching (CEREG != 1/5)"));
    delay(3000);
  }
  SerialUSB.println(F("✗ Timed out waiting for LTE registration"));
  return false;
}

bool waitForModemReady(uint32_t timeoutMs = 30000) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    String r = sendAT("AT", 2000);
    if (r.indexOf("OK") != -1) {
      SerialUSB.println(F("✓ Modem AT-responsive"));
      return true;
    }
    SerialUSB.println(F("… waiting for modem to accept AT"));
    delay(1000);
  }
  SerialUSB.println(F("✗ Timed out waiting for AT response"));
  return false;
}

/* ===================================================================
   LTE power-up & network attach – called before each upload
   Uses conservative timing to guarantee a clean start.
   =================================================================== */
void ltePowerSequence() {
  if (DEBUG){
    SerialUSB.println("Starting LTE power sequence...");
  }

  digitalWrite(LTE_RESET_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_RESET_PIN, LOW);

  delay(100);
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  digitalWrite(LTE_FLIGHT_PIN, LOW); // Normal mode

  delay(10000); // shorter initial wait (was 30 seconds)
  waitForModemReady();

  sendAT("ATI", 120000);  // det device information
  // Put modem in automatic LTE search mode (recommended in SIM7600 docs) :contentReference[oaicite:7]{index=7}
  sendAT("AT+CFUN=1", 9000);
  sendAT("AT+CNMP=2", 9000);   // automatic mode (you *could* also try LTE-only: AT+CNMP=13)

  sendAT("AT+CSQ", 5000);

  if (!waitForRegistration()) {
    SerialUSB.println(F("! No network registration – aborting LTE setup"));
    return;
  }

  // PDP / APN config
  sendAT("AT+CGDCONT=1,\"IP\",\"" APN "\"", 5000);
  sendAT("AT+CGATT=1", 15000);
  sendAT("AT+CGACT=1,1", 15000);
  sendAT("AT+CGPADDR=1", 10000);
}


/* ===================================================================
   Upload a single payload to ThingSpeak via HTTP GET
   - payload: concatenated newline-delimited units (≥140 when queued)
   =================================================================== */
void uploadData(const String &payload) {
  // Sanitize – ThingSpeak URL cannot contain CR/LF
  String clean = payload;
  clean.replace("\r", "");
  clean.replace("\n", "");

  if (clean.length() == 0){
    SerialUSB.println(F("! Upload cancelled: empty payload"));
    return;
  }

  // Reset & bring up the modem for a clean HTTP session
  ltePowerSequence();

  // NOTE: Existing parser expects one "meshName\tMoist\tTemp" unit.
  // If your payload combines multiple units, ensure your upstream format
  // is compatible. Here we proceed with the concatenated string.
  SoilData data(clean);
  DateTime now = DateTime::getTime();

  /* ---- Build ThingSpeak URL -------------------------------------- */
  String apiKey = API_WRITE_KEY;
  String url = "http://api.thingspeak.com/update?api_key=" + apiKey;
  url += "&field1=" + data.meshName;
  url += "&field2=" + now.yr + "-" + now.mon + "-" + now.day;
  url += "&field3=" + now.hr + ":" + now.min + ":" + now.sec;
  url += "&field4=" + data.moistData;
  url += "&field5=" + data.tempData;

  SerialUSB.println("\n[HTTP] » " + url);

  /* ---- One-shot HTTP session ------------------------------------- */
  if (sendAT("AT+HTTPTERM", 1000).indexOf("ERROR") == -1) {
    // ignore result – module may reply ERROR if not initialized yet
  }
  if (sendAT("AT+HTTPINIT", 5000).indexOf("OK") == -1) {
    SerialUSB.println(F("HTTPINIT failed – aborting"));
    return;
  }
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"", 1000);
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 2000);

  /* Start HTTP GET (method 0) */
  String resp = sendAT("AT+HTTPACTION=0", 15000);
  if (resp.indexOf("+HTTPACTION: 0,200") != -1) {
    SerialUSB.println(F("Upload OK"));
  } else {
    SerialUSB.println("Upload failed: " + resp);
  }
  sendAT("AT+HTTPTERM", 1000);
}

/* ===================================================================
   Simple AT helper – sends a command & collects reply until timeout
   =================================================================== */
String sendAT(const String &cmd, uint32_t timeout, bool dbg) {
  Serial1.println(cmd);
  uint32_t t0 = millis();
  String buffer;
  while (millis() - t0 < timeout) {
    while (Serial1.available()) {
      char c = Serial1.read();
      buffer += c;
    }
    if (buffer.indexOf("\r\nOK\r\n") != -1 ||
          buffer.indexOf("ERROR") != -1 ||
          buffer.indexOf("+CME ERROR") != -1) {
            break;
    }
  }
  if (dbg) {
    SerialUSB.print(cmd); SerialUSB.print(F(" → ")); SerialUSB.println(buffer);
  }
  return buffer;
}

/* ===================================================================
   Legacy helper retained for compatibility with prior code
   =================================================================== */
String removePrefix(String data, String prefix){
  int pos = data.indexOf(prefix);
  if (pos != -1) {
    data.remove(0, pos + prefix.length());
  }
  return data;
}
