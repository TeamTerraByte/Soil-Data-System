/*
  HTTPS GET via secure sockets: https://www.google.com/robots.txt
  Forces plain text by sending "Accept-Encoding: identity".
  Hardware: Arduino Uno + SparkFun LTE Cat M1/NB-IoT Shield (u-blox SARA-R4/R410)

  Notes:
  - Uses SparkFun library only for power-on/APN. HTTPS is via AT sockets.
  - TLS server validation is DISABLED here for simplicity. Load a CA and
    switch USECPRF validation to 1 for production.

  Wiring: Shield TXO -> Uno D8 (RX), Shield RXI -> Uno D9 (TX), common GND.
*/

#include <SparkFun_LTE_Shield_Arduino_Library.h>
#include <SoftwareSerial.h>

// -------- Config --------
const char APN[]  = "fast.t-mobile.com";
const char HOST[] = "www.google.com";
const int  PORT   = 443;
const char PATH[] = "/robots.txt";

const uint8_t SEC_PROFILE = 1; // security profile id we'll use
// ------------------------

SoftwareSerial lteSerial(8, 9); // Uno D8 (RX), D9 (TX)
LTE_Shield lte;

// ---- UART helpers (no lte.poll() inside these) ----
static void purgeModemUart(uint16_t ms = 10) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    while (lteSerial.available()) (void)lteSerial.read();
    delay(1);
  }
}

static void atSend(const String& s, bool echo = true) {
  purgeModemUart();
  if (echo) { Serial.print(F(">> ")); Serial.println(s); }
  lteSerial.print(s);
  lteSerial.print("\r");
  delay(15);
}

static bool waitForToken(const char* expect, uint32_t timeoutMs) {
  const size_t N = strlen(expect);
  size_t k = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (lteSerial.available()) {
      char c = (char)lteSerial.read();
      Serial.write(c); // mirror modem output
      if (c == expect[k]) {
        if (++k == N) return true;
      } else {
        k = (c == expect[0]) ? 1 : 0;
      }
    }
    delay(2);
  }
  Serial.print(F("\n[AT] timeout waiting for: ")); Serial.println(expect);
  return false;
}

// Wait for a line that starts with a given prefix; returns the whole line (without CRLF).
static bool waitForLineWithPrefix(const char* prefix, String& out, uint32_t timeoutMs) {
  out = "";
  String line = "";
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (lteSerial.available()) {
      char c = (char)lteSerial.read();
      Serial.write(c); // echo
      if (c == '\r') continue;
      if (c == '\n') {
        if (line.startsWith(prefix)) { out = line; return true; }
        line = "";
      } else {
        line += c;
      }
    }
    delay(2);
  }
  Serial.print(F("\n[AT] timeout waiting for line prefix: ")); Serial.println(prefix);
  return false;
}

// Specifically detect the final "\r\nOK\r\n" terminator; stream everything
static bool waitForOK(uint32_t timeoutMs) {
  enum { S0, SR, SLF, SO, SK, SR2, SLF2 } st = S0;
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (lteSerial.available()) {
      char c = (char)lteSerial.read();
      Serial.write(c);
      switch (st) {
        case S0:  st = (c == '\r') ? SR  : S0;  break;
        case SR:  st = (c == '\n') ? SLF : (c == '\r' ? SR : S0); break;
        case SLF: st = (c == 'O')  ? SO  : (c == '\r' ? SR : S0); break;
        case SO:  st = (c == 'K')  ? SK  : (c == '\r' ? SR : S0); break;
        case SK:  st = (c == '\r') ? SR2 : (c == '\r' ? SR : S0); break;
        case SR2: st = (c == '\n') ? SLF2: (c == '\r' ? SR : S0); break;
        case SLF2: return true;
      }
    }
    delay(2);
  }
  Serial.println(F("\n[AT] timeout waiting for final OK"));
  return false;
}

// ---- HTTPS GET over secure socket ----
static bool https_get_robots_txt() {
  // Verbose errors; disable echo for easier parsing
  atSend(F("AT+CMEE=2"));                          if (!waitForToken("OK", 4000))  return false;
  atSend(F("ATE0"));                               if (!waitForToken("OK", 4000))  return false;
  atSend(F("AT"));                                 if (!waitForToken("OK", 4000))  return false;

  // Create TCP socket and capture its ID from "+USOCR: <id>"
  atSend(F("AT+USOCR=6")); // TCP
  String line;
  if (!waitForLineWithPrefix("+USOCR:", line, 6000)) return false;
  // line format: "+USOCR: <id>"
  int colon = line.indexOf(':');
  int sockId = -1;
  if (colon >= 0) {
    String idStr = line.substring(colon + 1); idStr.trim();
    sockId = idStr.toInt();
  }
  if (sockId < 0) {
    Serial.println(F("\n[SOCK] failed to parse socket id"));
    return false;
  }
  // Expect "OK" after that
  if (!waitForToken("OK", 4000)) return false;

  // DEMO: no server validation (validation=0)
  {
    String cmd = String("AT+USECPRF=") + SEC_PROFILE + ",0,0";
    atSend(cmd);                                   if (!waitForToken("OK", 4000))  return false;
  }
  // Bind TLS to the socket
  {
    String cmd = String("AT+USOSEC=") + sockId + ",1," + SEC_PROFILE;
    atSend(cmd);                                   if (!waitForToken("OK", 6000))  return false;
  }
  // Connect to HOST:443
  {
    String cmd = String("AT+USOCO=") + sockId + ",\"" + HOST + "\"," + PORT;
    atSend(cmd);                                   if (!waitForToken("OK", 20000)) return false;
  }

  // Build HTTP request with "Accept-Encoding: identity"
  String req;
  req  = "GET ";
  req += PATH;
  req += " HTTP/1.1\r\nHost: ";
  req += HOST;
  req += "\r\nUser-Agent: Arduino-SARA-R4\r\nConnection: close\r\nAccept-Encoding: identity\r\n\r\n";

  // Send request
  {
    String cmd = String("AT+USOWR=") + sockId + "," + req.length();
    atSend(cmd);                                   if (!waitForToken("@", 4000))   return false; // write prompt
    lteSerial.print(req); // raw payload
    if (!waitForLineWithPrefix("+USOWR:", line, 8000)) return false;
    if (!waitForToken("OK", 4000)) return false;
  }

  // Read response until the module reports 0 bytes available
  Serial.println(F("\n---- Begin HTTPS response ----"));
  unsigned long overallDeadline = millis() + 45000;
  for (;;) {
    if (millis() > overallDeadline) { Serial.println(F("\n[SOCK] overall read timeout")); break; }
    String cmd = String("AT+USORD=") + sockId + ",512";
    atSend(cmd);
    // We’ll stream all modem output and look for "+USORD: sock,0"
    bool gotOK = false;
    bool zero  = false;
    String curLine = "";
    unsigned long t1 = millis();
    while (millis() - t1 < 6000) {
      while (lteSerial.available()) {
        char c = (char)lteSerial.read();
        Serial.write(c);
        if (c == '\r') continue;
        if (c == '\n') {
          if (curLine.startsWith("+USORD:")) {
            // Parse "+USORD: s,n,"..."
            int c1 = curLine.indexOf(',');
            int c2 = (c1 >= 0) ? curLine.indexOf(',', c1 + 1) : -1;
            if (c1 >= 0 && c2 > c1) {
              int n = curLine.substring(c1 + 1, c2).toInt();
              if (n == 0) zero = true;
            }
          } else if (curLine == "OK") {
            gotOK = true;
          }
          curLine = "";
        } else {
          curLine += c;
        }
      }
      if (gotOK) break;
      delay(2);
    }
    if (!gotOK) { Serial.println(F("\n[SOCK] timeout waiting OK after USORD")); break; }
    if (zero)   { break; }
    delay(50);
  }
  Serial.println(F("\n---- End HTTPS response ----"));

  // Close socket
  {
    String cmd = String("AT+USOCL=") + sockId;
    atSend(cmd);                                   waitForToken("OK", 4000);
  }
  return true;
}

void setup() {
  Serial.begin(9600);
  delay(1500);

  if (lte.begin(lteSerial, 9600)) {
    Serial.println(F("LTE Shield connected!"));
  } else {
    Serial.println(F("ERROR: LTE Shield not responding."));
  }

  LTE_Shield_error_t apnErr = lte.setAPN(String(APN));
  Serial.print(F("APN set attempt: ")); Serial.println((int)apnErr);

  if (!https_get_robots_txt()) {
    Serial.println(F("\nHTTPS fetch failed."));
  } else {
    Serial.println(F("\nHTTPS fetch complete."));
  }
}

void loop() {
  lte.poll(); // process URCs outside of blocking waits
}
