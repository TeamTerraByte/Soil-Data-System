/* ---------- PIN MAP ------------------------------------------------ */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
/* ------------------------------------------------------------------- */

const bool DEBUG = true;

// PROTOTYPES
String  sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
bool    modemBoot();
bool    networkAttach();
void    updateTime();
String  getTime();

void setup() {
  SerialUSB.begin(115200);
  delay(500);
  SerialUSB.println("==== Begin Maduino Init ====");
  delay(500);
  Serial1.begin(115200);

  while ( !modemBoot() ){
    delay(1000);
  }
  if (!networkAttach()) { while (1); }           // halt on fail
  
  updateTime();
  getTime();

  SerialUSB.println("\nSend commands to the Serial1 terminal");
}

void loop() {
    if (SerialUSB.available()) {
        String command = SerialUSB.readStringUntil('\n');
        sendAT(command);
    }

    // If data comes from AT module, send it to Serial1 Monitor
    if (Serial1.available()) {
        String response = Serial1.readStringUntil('\n');
        SerialUSB.println(response);
    }
}

String sendAT(const String& cmd, uint32_t to, bool dbg) {
    String resp;
    Serial1.println(cmd);
    unsigned long t0 = millis();
    while (millis() - t0 < to) while (Serial1.available()) resp += (char)Serial1.read();
    if (dbg) SerialUSB.print(resp);
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
        SerialUSB.println(F("CGATT FAIL")); return false;
    }

    sendAT("AT+CGACT=1,1", 5000);                          // ← new: activate PDP
    String ipResp = sendAT("AT+CGPADDR=1", 3000);          // ← new: get IP
    if (DEBUG) SerialUSB.print(F("IP RESP: ")); SerialUSB.println(ipResp);

    if (ipResp.indexOf('.') < 0) {                         // no dot → no IP
        SerialUSB.println(F("NO IP")); return false;
    }
    return true;
}

void updateTime(){
  String r = sendAT("AT+CTZU=1");
}

String getTime(){
  String time = sendAT("AT+CCLK?");
  int q_index = time.indexOf("\"");
  time = time.substring(q_index + 1, q_index + 21);

  if (DEBUG) SerialUSB.println("getTime() response:"+time);

  return time;
}

