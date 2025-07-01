#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "secrets.h"

#define BAUD 115200
#define DEBUG true

/* ---------- PIN MAP ------------------------------------------------ */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
/* ------------------------------------------------------------------- */

/* ---------- PROTOTYPES --------------------------------------------- */
String  sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
bool    modemBoot();
bool    networkAttach();
bool    postHTTPS(const char* SHEET_URL, const String& payload);
void    enableTimeUpdates();
String  getTime();
/* ------------------------------------------------------------------- */

/* ================================ SETUP ============================ */
void setup() {
    SerialUSB.begin(BAUD);
    delay(100);
    SerialUSB.println("\n=====================================");
    SerialUSB.println("Maduino Google Sheets Upload Test");
    SerialUSB.println("=====================================");
    delay(100);
    Serial1.begin(BAUD);

    if (!modemBoot()) { while (1); }               // halt on fail
    if (!networkAttach()) { while (1); }           // halt on fail

    /* ------- TEST PAYLOAD ------------------------------------------- */
    String payload = "Noon,Test,1,2,3,4,5,6,7,8";

    enableTimeUpdates();
    getTime();

    if (postHTTPS(SHEET_URL, payload))
        SerialUSB.println(F("\nUPLOAD OK"));
    else
        SerialUSB.println(F("\nUPLOAD FAIL"));
}

void loop() {
    // If data comes from Serial Monitor, send it to AT module
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        Serial1.println(command);
    }

    // If data comes from AT module, send it to Serial Monitor
    if (Serial1.available()) {
        String response = Serial1.readStringUntil('\n');
        Serial.println(response);
    }
}

/* =============================== HELPERS =========================== */

/* Boot sequence: pulse PWRKEY 2 s, wait 5 s for modem ready */
bool modemBoot() {
    pinMode(LTE_RESET_PIN,  OUTPUT);
    pinMode(LTE_PWRKEY_PIN, OUTPUT);
    pinMode(LTE_FLIGHT_PIN, OUTPUT);

    digitalWrite(LTE_RESET_PIN, LOW);
    digitalWrite(LTE_FLIGHT_PIN, LOW);   // normal mode

    digitalWrite(LTE_PWRKEY_PIN, HIGH);
    delay(2000);
    digitalWrite(LTE_PWRKEY_PIN, LOW);
    delay(5000);

    String ok = sendAT("AT", 1000, false);
    if (ok.indexOf("OK") < 0) {
    SerialUSB.println(F("MODEM NOT RESPONDING"));
    return false;
    }
    SerialUSB.println(F("MODEM READY"));
    return true;
}

/* Attach to T-Mobile LTE (fast.t-mobile.com) */
bool networkAttach() {
    sendAT("AT+CFUN=1");
    sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"");   // APN
    if (sendAT("AT+CGATT=1", 5000).indexOf("OK") < 0) {
        SerialUSB.println(F("CGATT FAIL")); return false;
    }

    sendAT("AT+CGACT=1,1", 5000);                          // ← new: activate PDP
    String ipResp = sendAT("AT+CGPADDR=1", 3000);          // ← new: get IP
    SerialUSB.print(F("IP RESP: ")); SerialUSB.println(ipResp);

    if (ipResp.indexOf('.') < 0) {                         // no dot → no IP
        SerialUSB.println(F("NO IP")); return false;
    }
    return true;
}


/* General AT wrapper */
String sendAT(const String& cmd, uint32_t to, bool dbg) {
    String resp;
    Serial1.println(cmd);
    unsigned long t0 = millis();
    while (millis() - t0 < to) while (Serial1.available()) resp += (char)Serial1.read();
    if (dbg) SerialUSB.print(resp);
    return resp;
}

/* Post a small text payload via HTTPS *//* Post a small text payload via HTTPS */
bool postHTTPS(const char* SHEET_URL, const String& payload) {
    sendAT("AT+HTTPTERM",  1000, false);   // clean slate

    sendAT("AT+CSSLCFG=\"sslversion\",0,4");     // TLS 1.2
    sendAT("AT+CSSLCFG=\"authmode\",0,0");       // No auth
    sendAT("AT+CSSLCFG=\"ignorertctime\",0,1");  // Ignore RTC time
    
    sendAT("AT+HTTPINIT");
    sendAT("AT+HTTPPARA=\"CID\",1");
    sendAT("AT+HTTPPARA=\"SSLCFG\",0");
    sendAT("AT+HTTPPARA=\"URL\",\"" + String(SHEET_URL) + "\"");
    sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");

    String r = sendAT("AT+HTTPDATA=" + String(payload.length()) + ",10000", 1000);
    if (r.indexOf("DOWNLOAD") < 0) { SerialUSB.println(F("HTTPDATA ERR")); return false; }

    Serial1.print(payload);                     // send body
    delay(100);

    String act = sendAT("AT+HTTPACTION=1", 8000);
    SerialUSB.println("\n========= Response =========");
    SerialUSB.println(act);
    SerialUSB.println("\n============================");


    String body = sendAT("AT+HTTPREAD", 3000);
    sendAT("AT+HTTPTERM");

    return (body.indexOf("Success") >= 0);
}

void enableTimeUpdates(){
  String r = sendAT("AT+CTZU=1");
}

String getTime(){
  String time = sendAT("AT+CCLK?");
  int q_index = time.indexOf("\"");
  time = time.substring(q_index + 1, q_index + 21);

  if (DEBUG) SerialUSB.println("getTime() response:"+time);

  return time;
}
