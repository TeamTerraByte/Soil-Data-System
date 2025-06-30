
/* ---------- PIN MAP ------------------------------------------------ */
#define LTE_RESET_PIN   6
#define LTE_PWRKEY_PIN  5
#define LTE_FLIGHT_PIN  7
/* ------------------------------------------------------------------- */

String sendAT(const String& cmd, uint32_t to, bool dbg);

void setup() {
  SerialUSB.begin(115200);        // USB serial monitor
  Serial1.begin(115200);          // UART to A76XX module (change to your actual serial port if needed)

  delay(3000);  // Wait for module and USB to settle
  pinMode(LTE_RESET_PIN,  OUTPUT);
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  pinMode(LTE_FLIGHT_PIN, OUTPUT);

  digitalWrite(LTE_RESET_PIN, LOW);
  digitalWrite(LTE_FLIGHT_PIN, LOW);   // normal mode

  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);
  delay(5000);

  SerialUSB.println("Requesting current time from A76XX...");
  String r = sendAT("AT+CCLK=?", 2000, true);
  if (r == ""){
    SerialUSB.println("No response");
  }
  else {
    SerialUSB.println("Invisible response");
  }
}

void loop() {

}

String sendAT(const String& cmd, uint32_t to, bool dbg) {
  String resp;
  Serial1.println(cmd);
  delay(10);
  unsigned long t0 = millis();
  while (millis() - t0 < to) while (Serial1.available()) resp += (char)Serial1.read();
  if (dbg) SerialUSB.print(resp);
  return resp;
}
