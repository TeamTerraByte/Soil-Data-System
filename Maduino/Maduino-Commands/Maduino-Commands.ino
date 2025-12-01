/* ---------- PIN MAP ------------------------------------------------ */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
/* ------------------------------------------------------------------- */
#define APN "m2m.com.attz"  // new Carrier APN (AT&T)

const bool DEBUG = true;

// PROTOTYPES
String  sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
bool    modemBoot();
bool    networkAttach();
bool waitForModemReady(uint32_t timeoutMs = 30000);
bool waitForRegistration(uint32_t timeoutMs = 120000);


void setup() {
    SerialUSB.begin(115200);
    Serial1.begin(115200);

    ltePowerSequence();
    // networkAttach();

    SerialUSB.println("\nSend commands to the Serial1 terminal");
}


void loop() {
    if (SerialUSB.available()) {
        String command = SerialUSB.readStringUntil('\n');
        sendAT(command);
    }

    // If data comes from AT module, send it to Serial1 Monitor
    while (Serial1.available()) {
        String response = Serial1.readStringUntil('\n');
        SerialUSB.println(response);
    }
}


void ltePowerSequence() {
  if (DEBUG){
    SerialUSB.println("Starting LTE power sequence...");
  }
    /* LTE GPIO --------------------------------------------------------- */
  pinMode(LTE_RESET_PIN,  OUTPUT);
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  pinMode(LTE_FLIGHT_PIN, OUTPUT);

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

  //sendAT("ATI", 120000);  // Get device information
  // Put modem in automatic LTE search mode (recommended in SIM7600 docs)
  sendAT("AT+CNMP=2", 9000);  // automatic mode (you *could* also try LTE-only: AT+CNMP=38)
  sendAT("AT+CFUN=1,1");      // soft reset the modem
  waitForModemReady();
  sendAT("AT+CMEE=2");        // Enable verbose errors  
  sendAT("AT+CEREG=2");       // Enable extended net reg and location info unsolicited
  sendAT("AT+CFUN=1", 9000);  // Set full functionality


  sendAT("AT+CESQ", 5000);  // Check signal quality

  if (!waitForRegistration()) {
    SerialUSB.println(F("! No network registration – aborting LTE setup"));
    return;
  }

  // PDP / APN config
  sendAT("AT+CGDCONT=1,\"IPV4V6\",\"" APN "\"", 5000);
  sendAT("AT+CGATT=1", 15000);
  sendAT("AT+CGACT=1,1", 15000);
  sendAT("AT+CGPADDR=1", 10000);
}


bool waitForModemReady(uint32_t timeoutMs) {
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


bool waitForRegistration(uint32_t timeoutMs) {
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


// bool networkAttach() {
//     sendAT("AT+CFUN=1");
//     sendAT("AT+CGDCONT=1,\"IP\",\"m2m.com.attz\"");   // APN
//     if (sendAT("AT+CGATT=1", 5000).indexOf("OK") < 0) {
//         SerialUSB.println(F("CGATT FAIL")); return false;
//     }

//     // do I need to run CGACT twice??
//     sendAT("AT+CGACT=1,1", 5000);                          // ← new: activate PDP
//     String ipResp = sendAT("AT+CGPADDR=1", 3000);          // ← new: get IP
//     SerialUSB.print(F("IP RESP: ")); SerialUSB.println(ipResp);

//     if (ipResp.indexOf('.') < 0) {                         // no dot → no IP
//         SerialUSB.println(F("NO IP")); return false;
//     }
//     return true;
// }

String sendAT(const String& cmd, uint32_t to, bool dbg) {
    String resp;
    Serial1.println(cmd);
    unsigned long t0 = millis();
    while (millis() - t0 < to){
        while (Serial1.available()){
            resp += (char)Serial1.read();
        }
            
    } 
    if (dbg) SerialUSB.print(resp);
    return resp;
}