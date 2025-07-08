#include <Wire.h>
#include <Arduino.h>
#include "secrets.h"

// ---------------- PIN MAP & CONSTANTS ----------------
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
#define SLAVE_ADDRESS   0x08
#define DEBUG true

String apiKey = API_WRITE_KEY;
String tempCSV, moistCSV, currentType;
bool   assembling = false;

/* ========= forward declarations ========= */
// LTE / AT
void  ltePowerSequence();
bool  modemBoot();
bool  networkAttach();
void  enableTimeUpdates();
String getTime();
String sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);

// I2C
void  receiveEvent(int bytes);
void  processReceivedData(String data);

// ThingSpeak
void  uploadToThingSpeak();
bool  httpGetThingSpeak(const String& url);
int   parseHttpStatus(const String& httpActionResp);

void setup() {
  SerialUSB.begin(115200);
  Serial1.begin(115200);

  pinMode(LTE_RESET_PIN, OUTPUT);
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  pinMode(LTE_FLIGHT_PIN, OUTPUT);

  Wire.begin(SLAVE_ADDRESS);
  Wire.onReceive(receiveEvent);

  ltePowerSequence();
  if (!modemBoot() || !networkAttach()) while (1);
  enableTimeUpdates();

  SerialUSB.println(F("Setup complete."));
}

void loop() { delay(500); }

/* ============ I2C HANDLING ============ */
void receiveEvent(int) {
  String rcv;
  while (Wire.available()) rcv += (char)Wire.read();
  processReceivedData(rcv);
}

void processReceivedData(String data) {
  if (data.startsWith("Moist,"))  { currentType = "Moist"; moistCSV = data.substring(6); assembling = true; }
  else if (data.startsWith("Temp,")){ currentType = "Temp"; tempCSV  = data.substring(5); assembling = true; }
  else if (assembling) {
    if (currentType == "Moist") moistCSV += data;
    else                        tempCSV  += data;

    if (data.length() < 15) {         // likely last chunk
      assembling = false;
      if (tempCSV.length() && moistCSV.length()) {
        uploadToThingSpeak();
        tempCSV.clear();  moistCSV.clear();
      }
    }
  }
}

/* ============ THINGSPEAK UPLOAD ============ */
void uploadToThingSpeak() {
  String ts = getTime();                     // yy/MM/dd,hh:mm:ss±zz
  String date = ts.substring(0,8);
  String time = ts.substring(9,17);

  String url = "http://api.thingspeak.com/update?api_key=" + apiKey +
               "&field1=" + date +
               "&field2=" + time +
               "&field3=" + tempCSV +
               "&field4=" + moistCSV +
               "&field5=0,0";

  if (httpGetThingSpeak(url))
    SerialUSB.println(F("ThingSpeak upload OK (status 200/302)."));
  else
    SerialUSB.println(F("ThingSpeak upload FAILED – see status above."));
}

/* -------- HTTP helper -------- */
bool httpGetThingSpeak(const String& url) {
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPPARA=\"CID\",1");
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"");
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"");

  /* issue the GET and wait – HTTPACTION URC arrives asynchronously */
  String resp = sendAT("AT+HTTPACTION=0", 65000);   // wait up to 65 s

  int status = parseHttpStatus(resp);
  SerialUSB.print(F("HTTP status: ")); SerialUSB.println(status);

  sendAT("AT+HTTPTERM");

  return (status == 200 || status == 201 || status == 202 ||
          status == 204 || status == 301 || status == 302);
}

/* extract <statuscode> from  “+HTTPACTION: <m>,<status>,<len>” */
int parseHttpStatus(const String& r) {
  int p = r.indexOf("+HTTPACTION:");
  if (p < 0) return -1;
  int c1 = r.indexOf(',', p);          // after <method>
  int c2 = r.indexOf(',', c1 + 1);     // after <status>
  if (c1 < 0 || c2 < 0) return -1;
  String s = r.substring(c1 + 1, c2);  s.trim();
  return s.toInt();
}

/* ============ LTE CONTROL & BASICS ============ */
void ltePowerSequence() {
  digitalWrite(LTE_RESET_PIN, HIGH); delay(2000); digitalWrite(LTE_RESET_PIN, LOW);
  digitalWrite(LTE_PWRKEY_PIN, HIGH); delay(2000); digitalWrite(LTE_PWRKEY_PIN, LOW);
  digitalWrite(LTE_FLIGHT_PIN, LOW);
  delay(10000);
}

bool modemBoot() {
  for (int i=0;i<5;i++){ if (sendAT("AT",1000).indexOf("OK")>=0) return true; delay(1000);}
  return false;
}

bool networkAttach() {
  sendAT("AT+CFUN=1");
  sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"");
  if (sendAT("AT+CGATT=1",5000).indexOf("OK")<0) return false;
  sendAT("AT+CGACT=1,1",5000);
  return sendAT("AT+CGPADDR=1",3000).indexOf('.')>=0;
}

void enableTimeUpdates(){ sendAT("AT+CTZU=1"); }

String getTime() {
  String r = sendAT("AT+CCLK?");
  int q = r.indexOf('"');
  return (q>=0)? r.substring(q+1, q+20) : "00/00/00,00:00:00";
}

/* basic AT wrapper */
String sendAT(const String& cmd, uint32_t to, bool dbg) {
  String resp;
  Serial1.println(cmd);
  unsigned long t0 = millis();
  while (millis() - t0 < to) while (Serial1.available()) resp += (char)Serial1.read();
  if (dbg) { SerialUSB.println(); SerialUSB.println("→ "+cmd); SerialUSB.println(resp); }
  return resp;
}
