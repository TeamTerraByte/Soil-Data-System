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
#define APN            "fast.t-mobile.com" // Carrier APN (example)
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
/* ------------------------------------------------------------------- */

/* ---------- Forward declarations ----------------------------------- */
String sendAT(const String &cmd, uint32_t timeout = 2000, bool dbg = DEBUG);
void   ltePowerSequence();
void   uploadData();
void   receiveEvent(int numBytes);
/* ------------------------------------------------------------------- */

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

  /* Initialise LTE once at power‑up --------------------------------- */
  ltePowerSequence();
  SerialUSB.println(F("Modem ready – waiting for sensor frames…"));
}

void loop() {
  /* Only attempt upload when both values are fresh */
  if (moistReady && tempReady && !assembling) {
    uploadData();
    /* mark as consumed so the next I²C burst triggers another upload */
    moistReady = tempReady = false;
  }
}

/* ===================================================================
   I²C receive ISR – builds a String from incoming bytes
   Format expected from master (examples):
     "Moist,550,"   – soil moisture raw ADC or percent
     "Temp,23.45,"  – °C
   You can extend the parser to accept other labels/fields.
   =================================================================== */
void receiveEvent(int numBytes) {
  assembling = true;
  String frame;
  while (Wire.available()) {
    char c = Wire.read();
    frame += c;
  }

  /* Decide which buffer to fill */
  if (frame.startsWith("Moist,")) {
    moistBuf = frame;
    moistReady = true;
  } else if (frame.startsWith("Temp,")) {
    tempBuf = frame;
    tempReady = true;
  }
  assembling = false;
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

  /* PWRKEY pulse – turns the module on -------------------------------- */
  delay(100);
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  /* Ensure RF is enabled ------------------------------------------- */
  digitalWrite(LTE_FLIGHT_PIN, LOW);

  /* Give modem time to boot & register ----------------------------- */
  delay(30000);

  /* Basic sanity ATs ---------------------------------------------- */
  sendAT("AT", 1000);
  sendAT("ATE0", 1000);                 // echo off
  sendAT("AT+CMEE=2", 1000);            // verbose errors
  sendAT("AT+CPIN?", 3000);             // SIM ready?
  sendAT("AT+CGATT=1", 5000);           // force packet attach
  sendAT("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 3000);
  sendAT("AT+CGACT=1,1", 5000);         // activate PDP context 1
  sendAT("AT+CGPADDR=1", 3000);
}

/* ===================================================================
   Upload the latest buffered readings to ThingSpeak via HTTP GET
   =================================================================== */
void uploadData() {
  ltePowerSequence();

  /* ---- Extract numeric part (strip label & trailing comma) -------- */
  String moistVal = moistBuf.substring(6);   // after "Moist,"
  String tempVal  = tempBuf.substring(5);    // after "Temp,"

  if (moistVal.endsWith(",")) moistVal.remove(moistVal.length() - 1);
  if (tempVal.endsWith(","))  tempVal.remove(tempVal.length()  - 1);

  /* ---- Build ThingSpeak URL -------------------------------------- */
  String url = "http://api.thingspeak.com/update?api_key=";
  url += API_WRITE_KEY;
  url += "&field1=" + moistVal + "&field2=" + tempVal;

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
