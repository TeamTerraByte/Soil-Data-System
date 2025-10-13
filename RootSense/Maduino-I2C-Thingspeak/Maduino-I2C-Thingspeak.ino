/* =====================================================================
   Maduino Zero – I²C Slave + LTE HTTP Uploader (ThingSpeak)
   ---------------------------------------------------------------------
   • Receives CSV sensor frames over I²C from an Arduino master
   • Buffers the latest Moisture and Temperature values (add more if needed)
   • Powers‑up & configures the on‑board SIM7600 LTE module
   • Uploads the readings to ThingSpeak using HTTP GET
   ---------------------------------------------------------------------
   Required wiring (Makerfabs Maduino Zero LTE):
     LTE_RESET_PIN  ➜ SIM7600 RESET    (active‑LOW)
     LTE_PWRKEY_PIN ➜ SIM7600 PWRKEY   (active‑LOW, via NPN inverter on PCB)
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
#define SLAVE_ADDRESS  0x08           // I²C address of this Maduino
#define APN            "fast.t-mobile.com" // Carrier APN
/* ------------------------------------------------------------------- */

/* ---------- LTE control pins --------------------------------------- */
#define LTE_RESET_PIN  6   // active‑LOW
#define LTE_PWRKEY_PIN 5   // active‑LOW pulse ≥100 ms
#define LTE_FLIGHT_PIN 7   // LOW = normal operation
/* ------------------------------------------------------------------- */

const bool DEBUG = true;            // echo AT chatter to SerialUSB

/* ---------- I²C frame buffering ------------------------------------ */
String moistBuf, tempBuf;           // raw CSV segments
volatile bool assembling = false;   // true while still receiving a block
volatile bool moistReady = false;   // true when moistBuf holds fresh data
volatile bool tempReady  = false;   // true when tempBuf  holds fresh data
String currentDataType = "";
/* ------------------------------------------------------------------- */

/* ---------- Forward declarations ----------------------------------- */
String sendAT(const String &cmd, uint32_t timeout = 2000, bool dbg = DEBUG);
void   ltePowerSequence();
void   uploadData();
void   receiveEvent(int numBytes);
String removePrefix(String data, String delim);
/* ------------------------------------------------------------------- */

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

    // Extract the quoted payload
    String core = r.substring(a + 1, b); // e.g. "24/10/10,20:10:00-20"

    // Strip timezone suffix (+zz or -zzzz)
    int tzPos = core.indexOf('+');
    if (tzPos < 0) tzPos = core.indexOf('-');
    if (tzPos > 0) core.remove(tzPos);

    // Basic validation
    if (!(core.length() >= 17 &&
          core[2] == '/' && core[5] == '/' &&
          core[8] == ',' &&
          core[11] == ':' && core[14] == ':')) {
      DateTime dt;
      dt.timeStr = core;  // Keep raw for debugging
      return dt;
    }

    return DateTime(core);
  }

  // Optional: pretty string representation
  String formatted() const {
    return yr + "/" + mon + "/" + day + " " + hr + ":" + min + ":" + sec;
  }
};


void setup() {
  /* Serial ports ----------------------------------------------------- */
  SerialUSB.begin(115200);
  Serial1.begin(115200);            // LTE module on Serial1

  delay(1000);
  SerialUSB.println(F("Maduino I²C + LTE uploader booting…"));

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
  /* Only attempt upload when both values are fresh */
  if (moistReady && tempReady && !assembling) {
    moistReady = tempReady = false;
    uploadData();
  }
}

/* ===================================================================
   I²C receive ISR – builds a String from incoming bytes
   Format expected from master (examples):
     "Moist,550,"   – soil moisture percent
     "Temp,23.45,"  – °C
   =================================================================== */
void receiveEvent(int numBytes) {
  assembling = true;
  String frame = "";
  while (Wire.available()) {
    char c = Wire.read();
    frame += c;
  }

  // Check if this is the start of a new data transmission
  if (frame.startsWith("Moist,")) {
    // Start of moisture data
    currentDataType = "Moist";
    moistBuf = frame;
    moistReady = false;
    SerialUSB.println("Started assembling Moisture data");
  } 
  else if (frame.startsWith("Temp,")) {
    // Start of temperature data
    currentDataType = "Temp";
    tempBuf = frame;
    tempReady = false;
    SerialUSB.println("Started assembling Temperature data");
  }
  else if (assembling) {
    // This is a continuation of the current data type
    if (currentDataType == "Moist") {
      moistBuf += frame;
    } else if (currentDataType == "Temp") {
      tempBuf += frame;
    }
    
    // Check if this appears to be the end of the transmission
    // (looking for data that ends with a comma followed by few characters or just comma)
    if (frame.length() < 15) {
      // This looks like the end of transmission
      assembling = false;
      
      if (currentDataType == "Moist") {
        SerialUSB.println("=== COMPLETE MOISTURE DATA ===");
        SerialUSB.println(moistBuf);
        SerialUSB.println("===============================");
        // You can process the complete moisture CSV string here
        moistReady = true;
      } 
      else if (currentDataType == "Temp") {
        SerialUSB.println("=== COMPLETE TEMPERATURE DATA ===");
        SerialUSB.println(tempBuf);
        SerialUSB.println("==================================");
        // You can process the complete temperature CSV string here
        tempReady = true;
      }
      
      currentDataType = "";
    }
  }
}

/* ===================================================================
   LTE power‑up & network attach – called once in setup()
   Uses conservative timing to guarantee a clean start.
   =================================================================== */
void ltePowerSequence() {
  /* Hardware reset pulse ------------------------------------------- */
  digitalWrite(LTE_RESET_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_RESET_PIN, LOW);

  
  delay(100);
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  digitalWrite(LTE_FLIGHT_PIN, LOW); // Normal mode

  delay(30000); // Wait for LTE module

  // LTE network setup
  sendAT("AT+CCID", 3000);
  sendAT("AT+CREG?", 3000);
  sendAT("AT+CGATT=1", 1000);
  sendAT("AT+CGACT=1,1", 1000);
  sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"", 1000);
  sendAT("AT+CGPADDR=1", 3000);          // show pdp address
}

String removePrefix(String data, String delim) {
  int pos = data.indexOf(delim);
  if (pos != -1) {
    data.remove(0, pos + delim.length());
  }
  return data;
}

/* ===================================================================
   Upload the latest buffered readings to ThingSpeak via HTTP GET
   =================================================================== */
void uploadData() {
  moistBuf.replace("\n", "");
  moistBuf.replace("\r", "");
  tempBuf.replace("\n", "");
  tempBuf.replace("\r", "");

  if (moistBuf.length() == 0 || tempBuf.length() ==0){
    SerialUSB.println("! Upload cancelled, moistBuf or tempBuf empty");
    return;  // I'm not sure how I'm uploading empty buffers
  }

  // For some reason, I have only observed consistent success using HTTP
  // if I reset LTE before every query
  ltePowerSequence(); 

  /* ---- Extract numeric part (strip label & trailing comma) -------- */
  String moistVal = moistBuf.substring(6);   // after "Moist,"
  String tempVal  = tempBuf.substring(5);    // after "Temp,"

  if (moistVal.endsWith(",")) moistVal.remove(moistVal.length() - 1);
  if (tempVal.endsWith(","))  tempVal.remove(tempVal.length()  - 1);

  DateTime now = DateTime::getTime();

  tempBuf = removePrefix(tempBuf, ", ");
  moistBuf = removePrefix(moistBuf, ", ");

  /* ---- Build ThingSpeak URL -------------------------------------- */
  String apiKey = API_WRITE_KEY;
  String url = "http://api.thingspeak.com/update?api_key=" + apiKey;
  url += "&field1=" + now.yr + "-" + now.mon + "-" + now.day;
  url += "&field2=" + now.hr + ":" + now.min + ":" + now.sec;
  url += "&field3=0.000000,0.000000,0.0";
  url += "&field4=" + tempBuf;
  url += "&field5=" + moistBuf;

  SerialUSB.println("\n[HTTP] » " + url);

  /* ---- One‑shot HTTP session ------------------------------------- */
  if (sendAT("AT+HTTPTERM", 1000).indexOf("ERROR") == -1) {
    // ignore result – module may reply ERROR if not initialised yet
  }
  if (sendAT("AT+HTTPINIT", 5000).indexOf("OK") == -1) {
    SerialUSB.println(F("HTTPINIT failed – aborting"));
    return;
  }
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"", 1000);
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 2000);

  /* Start HTTP GET (method 0) */
  String resp = sendAT("AT+HTTPACTION=0", 30000);
  if (resp.indexOf("+HTTPACTION: 0,200") != -1) {
    SerialUSB.println(F("Upload OK"));
  } else {
    SerialUSB.println("Upload failed: " + resp);
  }
  sendAT("AT+HTTPTERM", 1000);
  
  moistBuf = ""; // Clear for next transmission
  tempBuf = ""; // Clear for next transmission
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
  }
  if (dbg) {
    SerialUSB.print(cmd); SerialUSB.print(F(" → ")); SerialUSB.println(buffer);
  }
  return buffer;
}
