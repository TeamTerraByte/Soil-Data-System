/*
  SparkFun LTE Shield AT Bridge with RAW upload mode
  - Powers the modem via SparkFun LTE lib (lte.begin)
  - Uses your atSend(...) for line AT commands
  - Auto-enters RAW pass-through when '>' prompt appears after AT+USECMNG=0,0,...,<SIZE>
    so you can send a DER file from RealTerm. Exit RAW when Ctrl+Z (0x1A) is received.
  Hardware: UNO + SparkFun LTE Cat M1/NB-IoT (SARA-R4/R410)
  Wiring:   Shield TXO -> D8, Shield RXI -> D9, GND<->GND
*/

#include <SparkFun_LTE_Shield_Arduino_Library.h>
#include <SoftwareSerial.h>

static const unsigned long PC_BAUD   = 9600;   // Your PC terminal baud
static const unsigned long LTE_BAUD  = 9600;   // SARA UART baud (matches your working code)

SoftwareSerial lteSerial(8, 9); // RX=D8 (from shield TXO), TX=D9 (to shield RXI)
LTE_Shield lte;

// ------------ helpers ------------
static void purgeModemUart(uint16_t ms = 10) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    while (lteSerial.available()) (void)lteSerial.read();
    delay(1);
  }
}

// YOUR function (used for line-mode sends)
static void atSend(const String& s, bool echo = true) {
  purgeModemUart();
  if (echo) { Serial.print(F(">> ")); Serial.println(s); }
  lteSerial.print(s);
  lteSerial.print("\r");
  delay(15);
}

// ------------ state for RAW upload ------------
static bool awaitingRawPrompt = false; // we sent AT+USECMNG=0,0,...,<SIZE>, waiting for '>'
static bool rawMode           = false; // after '>' seen, forward bytes PC->LTE as-is until 0x1A

void setup() {
  Serial.begin(PC_BAUD);
  while (!Serial) {}
  Serial.println(F("Starting LTE bridge..."));

  // Power on the modem + bind serial (SparkFun lib handles PWRKEY)
  if (lte.begin(lteSerial, LTE_BAUD)) {
    Serial.println(F("LTE module on."));
  } else {
    Serial.println(F("ERROR: LTE module not responding. Check power/jumpers/wiring."));
  }

  // Optional but useful:
  atSend(F("ATE0"));       // disable echo for cleaner logs
  atSend(F("AT+CMEE=2"));  // verbose errors

  Serial.println(F("\nBridge ready."));
  Serial.println(F("- Type AT commands; press Enter (CR or CR+LF)."));
  Serial.println(F("- For certificate upload:"));
  Serial.println(F("    1) AT+USECMNG=2,0,\"GTS_Root_R1\"   (ignore error if not present)"));
  Serial.println(F("    2) AT+USECMNG=0,0,\"GTS_Root_R1\",<SIZE>"));
  Serial.println(F("       (When '>' appears, bridge enters RAW mode automatically.)"));
  Serial.println(F("    3) In RealTerm: Send File (binary) -> DER file -> then send Ctrl+Z (0x1A)."));
  Serial.println(F("------------------------------------------------------------"));
}

void loop() {
  // ----------------- LTE -> PC -----------------
  while (lteSerial.available()) {
    char c = (char)lteSerial.read();
    Serial.write(c);

    // Detect the '>' prompt that follows AT+USECMNG=0,0,...,<SIZE>
    if (awaitingRawPrompt && c == '>') {
      rawMode = true;
      awaitingRawPrompt = false;
      Serial.println(F("\n[RAW MODE] Send DER now (binary). End with Ctrl+Z (0x1A)."));
    }
  }

  // ----------------- PC -> LTE -----------------
  if (rawMode) {
    // Raw pass-through: forward every byte, including 0x00 and 0x1A
    while (Serial.available()) {
      uint8_t b = (uint8_t)Serial.read();
      lteSerial.write(b);
      if (b == 0x1A) { // Ctrl+Z ends RAW upload
        rawMode = false;
        Serial.println(F("\n[RAW MODE END] Waiting for modem response..."));
      }
    }
  } else {
    // Line-oriented mode: read a line from PC, send via atSend (adds CR)
    if (Serial.available()) {
      String line = Serial.readStringUntil('\n'); // works for CR/LF/CRLF; we'll trim
      line.trim();
      if (line.length() > 0) {
        // If this is the store-object command, arm raw-prompt detector
        if (line.startsWith("AT+USECMNG=0,0")) {
          awaitingRawPrompt = true;
        }
        atSend(line, true);
      }
    }
  }

  // DO NOT call lte.poll() here; we want all bytes to flow only through our bridge.
}
