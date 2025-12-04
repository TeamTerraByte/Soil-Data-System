/* ---------- PIN MAP ------------------------------------------------ */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
/* ------------------------------------------------------------------- */
#define APN "m2m.com.attz"  // new Carrier APN (AT&T)

const bool DEBUG = true;

// PROTOTYPES
String  sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
bool waitForModemReady(uint32_t timeoutMs = 30000);
bool waitForRegistration(uint32_t timeoutMs = 120000);


void setup() {
  SerialUSB.begin(115200);
  Serial1.begin(115200);

  powerOnModem();
  waitForModemReady(30000);

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

void powerOnModem(){
  pinMode(LTE_RESET_PIN, OUTPUT);
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  pinMode(LTE_FLIGHT_PIN, OUTPUT);

  delay(100);
  digitalWrite(LTE_RESET_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_RESET_PIN, LOW);

  delay(100);
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  digitalWrite(LTE_FLIGHT_PIN, LOW); // Normal mode
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