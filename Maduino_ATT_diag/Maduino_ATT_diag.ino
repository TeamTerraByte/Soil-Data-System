/*
  Maduino Zero (SIM7600) – AT&T Connectivity Deep Debugger (v2)
  Using EXACT user-provided modem power-on sequence.

  What this sketch does:
    ✓ Uses your proven working LTE power sequence
    ✓ Waits for modem to answer "AT"
    ✓ Dumps:
        ATI, CGMM, CGMR, CGSN, CICCID
        CNMP?, CMNB?, CNBP?
        CPIN?, CSQ, CREG?, CEREG?, COPS?, CPSI?
    ✓ Waits for LTE registration (CEREG=1 or 5)
    ✓ If registered: set APN → attach → activate PDP → print IP
    ✓ If PDP works: optional HTTP GET to http://example.com/

  SerialUSB = PC terminal (115200)
  Serial1   = SIM7600 interface (115200)

  Power-on pins (Makerfabs Maduino Zero LTE):
    LTE_RESET_PIN  (GPIO 6)
    LTE_PWRKEY_PIN (GPIO 5)
    LTE_FLIGHT_PIN (GPIO 7)
*/

#include <Arduino.h>

/* ----------------- User Config ----------------------------------- */
#define APN "m2m.com.attz"   // Your AT&T IoT/M2M APN

/* ----------------- Modem Control Pins ----------------------------- */
#define LTE_RESET_PIN  6
#define LTE_PWRKEY_PIN 5
#define LTE_FLIGHT_PIN 7

/* ----------------- AT Debug Switch ------------------------------- */
const bool DEBUG_AT = true;

/* ----------------- Utility Functions ----------------------------- */

void flushModem() {
  while (Serial1.available()) Serial1.read();
}

String sendAT(const String &cmd, uint32_t timeoutMs = 3000, bool dbg = DEBUG_AT) {
  flushModem();
  Serial1.println(cmd);

  uint32_t start = millis();
  String buffer = "";

  while (millis() - start < timeoutMs) {
    while (Serial1.available()) buffer += (char)Serial1.read();

    if (buffer.indexOf("\r\nOK\r\n") != -1 ||
        buffer.indexOf("ERROR") != -1 ||
        buffer.indexOf("+CME ERROR") != -1) {
      break;
    }
  }

  if (dbg) {
    SerialUSB.print(F(">>> ")); SerialUSB.println(cmd);
    SerialUSB.print(F("<<< ")); SerialUSB.println(buffer);
    SerialUSB.println(F("--------------------------------------------------"));
  }

  return buffer;
}

/* ----------------- EXACT USER POWER SEQUENCE ---------------------- */

void powerOnModem() {
  SerialUSB.println(F("[MODEM] Starting user-defined power-on sequence..."));

  pinMode(LTE_RESET_PIN, OUTPUT);
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  pinMode(LTE_FLIGHT_PIN, OUTPUT);

  // EXACT CODE AS USER PROVIDED ↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓↓
  digitalWrite(LTE_RESET_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_RESET_PIN, LOW);

  delay(100);
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  digitalWrite(LTE_FLIGHT_PIN, LOW); // Normal mode
  // EXACT CODE AS USER PROVIDED ↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑↑

  SerialUSB.println(F("[MODEM] Power sequence applied. Waiting for boot..."));
}

/* ----------------- Wait for "AT" OK ------------------------------- */

bool waitForModemReady(uint32_t timeoutMs = 60000) {
  SerialUSB.println(F("[MODEM] Waiting for modem AT ready..."));

  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    String r = sendAT("AT", 2000, false);

    if (r.indexOf("OK") != -1) {
      SerialUSB.println(F("[MODEM] ✓ Modem responded to AT."));
      return true;
    }

    if (r.length() > 0) {
      SerialUSB.println(F("[MODEM] Boot URCs:"));
      SerialUSB.println(r);
      SerialUSB.println(F("--------------------------------------------------"));
    }

    delay(1000);
  }

  SerialUSB.println(F("[ERROR] Modem never responded to AT."));
  return false;
}

/* ----------------- LTE Registration Wait -------------------------- */

bool waitForLTE() {
  SerialUSB.println(F("[NET] Waiting for LTE (CEREG=1/5)..."));

  uint32_t start = millis();

  while (millis() - start < 120000) {
    String r = sendAT("AT+CEREG?", 4000);

    if (r.indexOf(",1") != -1 || r.indexOf(",5") != -1) {
      SerialUSB.println(F("[NET] ✓ LTE registered."));
      return true;
    }
    if (r.indexOf(",3") != -1) {
      SerialUSB.println(F("[NET] ✗ Registration denied (CEREG=3)."));
      return false;
    }
    if (r.indexOf(",4") != -1) {
      SerialUSB.println(F("[NET] ! Unknown / out of LTE coverage (CEREG=4)."));
    }

    SerialUSB.println(F("[NET] ...still searching..."));
    delay(3000);
  }

  SerialUSB.println(F("[NET] ✗ Timed out waiting for LTE."));
  return false;
}

/* ----------------- PDP Context Setup ------------------------------ */

bool bringUpPDP() {
  SerialUSB.println(F("[PDP] Setting APN & activating context..."));

  if (sendAT("AT+CGDCONT=1,\"IP\",\"" APN "\"", 8000).indexOf("OK") == -1)
    return false;

  if (sendAT("AT+CGATT=1", 20000).indexOf("OK") == -1)
    return false;

  if (sendAT("AT+CGACT=1,1", 20000).indexOf("OK") == -1)
    return false;

  String ip = sendAT("AT+CGPADDR=1", 8000);
  if (ip.indexOf("+CGPADDR: 1,") == -1 || ip.indexOf('.') == -1) {
    SerialUSB.println(F("[PDP] ✗ No IP address assigned."));
    return false;
  }

  SerialUSB.println(F("[PDP] ✓ PDP Context Up (Got IP)."));
  return true;
}

/* ----------------- HTTP Test (Optional) --------------------------- */

void httpTest() {
  SerialUSB.println(F("[HTTP] Testing HTTP GET..."));

  sendAT("AT+HTTPTERM", 2000);

  if (sendAT("AT+HTTPINIT", 8000).indexOf("OK") == -1) return;

  if (sendAT("AT+HTTPPARA=\"CID\",1", 5000).indexOf("OK") == -1) return;

  if (sendAT("AT+HTTPPARA=\"URL\",\"http://example.com/\"", 8000).indexOf("OK") == -1)
    return;

  String r = sendAT("AT+HTTPACTION=0", 30000);
  if (r.indexOf("+HTTPACTION: 0,200") == -1) {
    SerialUSB.println(F("[HTTP] ✗ HTTPACTION not 200."));
    return;
  }

  SerialUSB.println(F("[HTTP] ✓ HTTPACTION=200, reading..."));
  sendAT("AT+HTTPREAD", 15000);

  sendAT("AT+HTTPTERM", 3000);
}

/* ----------------- Arduino Setup --------------------------------- */

void setup() {
  SerialUSB.begin(115200);
  while (!SerialUSB) {}

  SerialUSB.println(F("\n===================================================="));
  SerialUSB.println(F("   Maduino Zero – SIM7600 AT&T Deep Debugger (v2)"));
  SerialUSB.println(F("   Using USER-VERIFIED POWER-ON SEQUENCE"));
  SerialUSB.println(F("===================================================="));

  Serial1.begin(115200);

  powerOnModem();
  delay(5000);

  if (!waitForModemReady()) return;

  /* ---- Identity ---- */
  sendAT("ATI", 5000);
  sendAT("AT+CGMM", 5000);
  sendAT("AT+CGMR", 5000);
  sendAT("AT+CGSN", 5000);
  sendAT("AT+CICCID", 5000);

  /* ---- RAT / Bands ---- */
  sendAT("AT+CNMP?", 5000);
  sendAT("AT+CMNB?", 5000);
  sendAT("AT+CNBP?", 8000);

  /* ---- Full Power ---- */
  sendAT("AT+CFUN=1", 8000);

  /* ---- Basic Network Status ---- */
  sendAT("AT+CPIN?", 5000);
  sendAT("AT+CSQ", 5000);
  sendAT("AT+CREG?", 5000);
  sendAT("AT+CEREG?", 5000);
  sendAT("AT+COPS?", 8000);
  sendAT("AT+CPSI?", 8000);

  /* ---- LTE Registration Wait ---- */
  bool regOK = waitForLTE();

  /* ---- Final Snapshot ---- */
  sendAT("AT+CEREG?", 5000);
  sendAT("AT+CREG?", 5000);
  sendAT("AT+COPS?", 8000);
  sendAT("AT+CPSI?", 8000);

  if (!regOK) {
    SerialUSB.println(F("[DIAG] LTE registration did not occur. Stopping."));
    return;
  }

  /* ---- PDP & HTTP ---- */
  if (!bringUpPDP()) {
    SerialUSB.println(F("[DIAG] PDP failed. Check APN/SIM provisioning."));
    return;
  }

  httpTest();

  SerialUSB.println(F("[DONE] Debug complete."));
}

void loop() {}
