/*
  HTTPS GET to www.google.com/generate_204 (tiny response)
  Hardware: Arduino Uno + SparkFun LTE Cat M1/NB-IoT Shield (u-blox SARA-R4/R410)
  Notes:
  - Uses SparkFun LTE library to power on & set APN, then raw AT for secure HTTP (UHTTP).
  - For demo: TLS is enabled but server cert validation is disabled (USECPRF validate=0).
    For production, load a CA and set validate=trusted root (see note below).

  Wiring (default SparkFun shield jumpers):
  - Module UART <-> Arduino SoftwareSerial on D8 (RX), D9 (TX)
*/

#include <SparkFun_LTE_Shield_Arduino_Library.h>
#include <SoftwareSerial.h>

///////////////////// Config /////////////////////
const char APN[] = "fast.t-mobile.com";     // your APN
const char HTTPS_HOST[] = "www.google.com"; // target host
const char HTTPS_PATH[] = "/robots.txt";  // small/no-content URL
const uint8_t SEC_PROFILE = 1;              // security profile id we'll use
const uint16_t AT_DEFAULT_TIMEOUT = 8000;   // ms
/////////////////////////////////////////////////

SoftwareSerial lteSerial(8, 9); // D8=RX (Arduino), D9=TX (Arduino)
LTE_Shield lte;

// --- tiny AT helper ---
static bool atWaitFor(const char* expect, uint32_t timeoutMs) {
  String buf;
  uint32_t t0 = millis();
  while ((millis() - t0) < timeoutMs) {
    while (lteSerial.available()) {
      char c = (char)lteSerial.read();
      buf += c;
      if (buf.length() > 2048) buf.remove(0, 1024); // prevent runaway
      if (buf.indexOf(expect) >= 0) {
        Serial.print(F("[AT] matched: ")); Serial.println(expect);
        // Bubble any remaining URCs to Serial for visibility:
        while (lteSerial.available()) Serial.write(lteSerial.read());
        return true;
      }
    }
    lte.poll(); // let library process URCs
    delay(5);
  }
  Serial.print(F("[AT] timeout waiting for: ")); Serial.println(expect);
  Serial.print(F("[AT] last buf: ")); Serial.println(buf);
  return false;
}

static void atSend(const String& s, bool echo=true) {
  if (echo) { Serial.print(F(">> ")); Serial.println(s); }
  lteSerial.print(s);
  lteSerial.print("\r");
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  // Bring up modem & library
  if (lte.begin(lteSerial, 9600)) {
    Serial.println(F("LTE Shield connected!"));
  } else {
    Serial.println(F("ERROR: LTE Shield not responding."));
  }

  LTE_Shield_error_t apnErr = lte.setAPN(String(APN));
  Serial.print(F("APN set attempt: ")); Serial.println((int)apnErr);

  // Verbose errors, sanity ping
  atSend(F("AT+CMEE=2"));               atWaitFor("OK", 2000);
  atSend(F("AT"));                      atWaitFor("OK", 2000);

  // Ensure PDP/profile active for internal apps (HTTP/HTTPS)
  // Map PSD profile #0 to CID=1 (typical), then activate.
  // (On some networks the library already did this; doing it again is harmless.)
  atSend(F("AT+UPSD=0,100,1"));         atWaitFor("OK", 2000);  // profile#0 -> CID 1
  atSend(F("AT+UPSDA=0,3"));            atWaitFor("OK", 12000); // activate PSD profile 0

  // -------- Secure HTTP setup (profile 0) --------
  // Reset HTTP profile 0, set host
  atSend(F("AT+UHTTP=0"));              atWaitFor("OK", 2000);
  {
    String cmd = String("AT+UHTTP=0,1,\"") + HTTPS_HOST + "\"";
    atSend(cmd);                        atWaitFor("OK", 4000);
  }

  // DEMO ONLY: use TLS with NO server verification (no CA installed).
  // Security profile SEC_PROFILE: set validation=0 (none).
  {
    String cmd = String("AT+USECPRF=") + SEC_PROFILE + ",0,0";
    atSend(cmd);                        atWaitFor("OK", 2000);
  }

  // Enable HTTPS on UHTTP profile 0 and bind to our security profile.
  {
    String cmd = String("AT+UHTTP=0,6,1,") + SEC_PROFILE; // secure=1, profile=SEC_PROFILE
    atSend(cmd);                        atWaitFor("OK", 4000);
  }

  // Optional: explicitly set port 443 (enabling secure does this automatically)
  // atSend(F("AT+UHTTP=0,5,443"));     atWaitFor("OK", 2000);

  // -------- Execute HTTPS GET and save to file --------
  // Use a very small Google endpoint to avoid big payloads.
  atSend(String("AT+UHTTPC=0,1,\"") + HTTPS_PATH + "\",\"g.resp\"");
  // Wait for operation URC: +UUHTTPCR: 0,1,1  (profile, command, result=1=success)
  atWaitFor("+UUHTTPCR: 0,1,1", 20000);

  // Read back the response file (contains HTTP status line + headers for 204)
  atSend(F("AT+URDFILE=\"g.resp\""));
  atWaitFor("OK", 4000); // prints the file contents before OK

  Serial.println(F("\nDone. (If you see HTTP/1.1 204 No Content headers, HTTPS succeeded.)"));
}

void loop() {
  lte.poll();
}
