/* ---------- PIN MAP ------------------------------------------------ */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
/* ------------------------------------------------------------------- */

const bool DEBUG = true;

/* ====== TYPES ====== */
class DateTime {
  public:
    String   timeStr;
    uint16_t yr;
    uint8_t  mon;
    uint8_t  day;
    uint8_t  hr;
    uint8_t  min;
    uint8_t  sec;

    DateTime() : yr(0), mon(0), day(0), hr(0), min(0), sec(0) {}

    explicit DateTime(const String& time) {
      // Expected: "YY/MM/DD,HH:MM:SS"
      this->timeStr = time;
      yr  = time.substring(0, 2).toInt();   // 00..99
      mon = time.substring(3, 5).toInt();
      day = time.substring(6, 8).toInt();
      hr  = time.substring(9, 11).toInt();
      min = time.substring(12, 14).toInt();
      sec = time.substring(15, 17).toInt();
    }
};

/* ====== PROTOTYPES ====== */
String   sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
bool     modemBoot();
bool     networkAttach();
void     enableTimeUpdates();
DateTime getTime();

void setup() {
  SerialUSB.begin(115200);
  delay(500);
  SerialUSB.println("==== Begin Maduino Init ====");
  delay(500);
  Serial1.begin(115200);

  while (!modemBoot()) {
    delay(1000);
  }
  if (!networkAttach()) {
    while (1) { /* halt on fail */ }
  }

  enableTimeUpdates();
  (void)getTime(); // prime once

  SerialUSB.println("\nSend commands to the Serial1 terminal");
}

void loop() {
  DateTime now = getTime();
  SerialUSB.println(now.timeStr);
  SerialUSB.println(now.yr);

  delay(1000);

  if (SerialUSB.available()) {
    String command = SerialUSB.readStringUntil('\n');
    sendAT(command);
  }

  // If data comes from AT module, send it to Serial Monitor
  if (Serial1.available()) {
    String response = Serial1.readStringUntil('\n');
    SerialUSB.println(response);
  }
}

String sendAT(const String& cmd, uint32_t to, bool dbg) {
  String resp;
  Serial1.println(cmd);
  unsigned long t0 = millis();
  while (millis() - t0 < to) {
    while (Serial1.available()) {
      resp += (char)Serial1.read();
    }
    delay(1); // tiny yield to avoid a hard busy-wait
  }
  if (dbg && resp.length()) {
    SerialUSB.print(resp);
  }
  return resp;
}

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

bool networkAttach() {
  sendAT("AT+CFUN=1");
  sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"");   // APN
  if (sendAT("AT+CGATT=1", 5000).indexOf("OK") < 0) {
    SerialUSB.println(F("CGATT FAIL"));
    return false;
  }

  sendAT("AT+CGACT=1,1", 5000);                          // activate PDP
  String ipResp = sendAT("AT+CGPADDR=1", 3000);          // get IP
  if (DEBUG) { SerialUSB.print(F("IP RESP: ")); SerialUSB.println(ipResp); }

  if (ipResp.indexOf('.') < 0) {                         // no dot → no IP
    SerialUSB.println(F("NO IP"));
    return false;
  }
  return true;
}

void enableTimeUpdates() {
  (void)sendAT("AT+CTZU=1"); // enable automatic time zone updates (if supported)
}

DateTime getTime() {
  // Typical reply: +CCLK: "24/10/10,20:10:00-20"
  String r = sendAT("AT+CCLK?");
  int firstQ = r.indexOf('"');
  int secondQ = r.indexOf('"', firstQ + 1);

  String core;
  if (firstQ >= 0 && secondQ > firstQ) {
    core = r.substring(firstQ + 1, secondQ); // e.g. 24/10/10,20:10:00-20
  }

  // Strip any timezone suffix (±zz or ±zzzz)
  int tzPos = core.indexOf('+');
  if (tzPos < 0) tzPos = core.indexOf('-');
  if (tzPos > 0) core = core.substring(0, tzPos);       // now "YY/MM/DD,HH:MM:SS"

  if (DEBUG) {
    SerialUSB.print("getTime() core: ");
    SerialUSB.println(core);
  }

  // If parsing failed, return an empty DateTime (all zeros) but keep raw string
  if (core.length() < 17) {
    DateTime dt;
    dt.timeStr = core;
    return dt;
  }

  return DateTime(core);
}
