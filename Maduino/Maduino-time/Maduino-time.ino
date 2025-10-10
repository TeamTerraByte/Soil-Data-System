/* ---------- PIN MAP ------------------------------------------------ */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
/* ------------------------------------------------------------------- */

const bool DEBUG = true;

/* ====== PROTOTYPES ====== */
String   sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
bool     modemBoot();
bool     networkAttach();
void     enableTimeUpdates();


/* ====== TYPES ====== */
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

  SerialUSB.println("\nSend commands to the Serial1 terminal");
}

void loop() {
  DateTime now = DateTime::getTime();
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


