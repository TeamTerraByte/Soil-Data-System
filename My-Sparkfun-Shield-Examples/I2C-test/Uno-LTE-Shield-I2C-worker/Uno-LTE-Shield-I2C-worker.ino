/*
  Code by Jacob Poland with help from Jim Lindblom's 
  SparkFun LTE shield example.

  Modified to:
  - Act as an I2C slave (address 0x36).
  - Accept a string from I2C master, possibly in multiple 30-byte chunks.
  - Treat the message as complete when the sequence "!F" is received,
    even if '!' and 'F' arrive in different chunks.
  - Upload that string to ThingSpeak using the existing HTTP format.
*/

#include <Wire.h>
#include <SoftwareSerial.h>
// Click here to get the library: http://librarymanager/All#SparkFun_LTE_Shield_Arduino_Library
#include <SparkFun_LTE_Shield_Arduino_Library.h>
#include "secrets.h"

#define SerialMonitor Serial
#define DEBUG true

// ---------- I2C configuration ----------
#define I2C_SLAVE_ADDR   0x36
#define I2C_BUFFER_SIZE  128  // Max length of accumulated I2C string (including null terminator)

// Volatile because modified in ISR
volatile bool    i2cMessageReady = false;
volatile uint8_t i2cLen          = 0;
volatile char    i2cRaw[I2C_BUFFER_SIZE];

// ---------- Global LTE / Serial ----------
SoftwareSerial lteSerial(8, 9);  // RX, TX for LTE shield
LTE_Shield lte;

// ---------- Forward declarations ----------
String sendAT(const String &cmd, uint32_t timeout = 2000, bool dbg = DEBUG);
void sendToThingSpeak(const String &fieldsPart);
void onI2CReceive(int numBytes);

// =============================================================
// Setup
// =============================================================
void setup() {
  SerialMonitor.begin(9600);

  // I2C: act as a slave device
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(onI2CReceive);

  // Initialize LTE shield
  if (lte.begin(lteSerial, 9600)) {
    SerialMonitor.println(F("LTE Shield connected!"));
  }

  // Basic LTE / network setup
  // sendAT("AT");                // check that modem is responsive
  // sendAT("AT+CMEE=2");         // enable verbose error codes
  // sendAT("AT+UMNOPROF?");      // should return 2 for ATT
  // sendAT("AT+CEREG?");         // verify RAT registration, should show 0,1
  // sendAT("AT+COPS?");          // check operator
  // sendAT("AT+CSQ");            // signal quality RSSI
  sendAT("AT+CGATT=1");        // ensure PS attached
  sendAT("AT+CGACT=1,1");      // activate PDP context 1
  sendAT("AT+CGPADDR=1");      // confirm obtained IP address

  SerialMonitor.println(F("Ready! I2C slave + LTE passthrough.\r\n"));
}

// =============================================================
// Main loop
// =============================================================
void loop() {
  // Serial <-> LTE passthrough (kept from your original code)
  if (Serial.available()) {
    lteSerial.write((char)Serial.read());
  }
  if (lteSerial.available()) {
    Serial.write((char)lteSerial.read());
  }

  // If we received a complete I2C message, send it to ThingSpeak
  if (i2cMessageReady) {
    // Safely copy the volatile buffer into a local buffer
    noInterrupts();
    char    localBuf[I2C_BUFFER_SIZE];
    uint8_t len = i2cLen;
    if (len >= I2C_BUFFER_SIZE) len = I2C_BUFFER_SIZE - 1;
    memcpy(localBuf, (const void *)i2cRaw, len);
    localBuf[len] = '\0';

    // Reset for next message
    i2cMessageReady = false;
    i2cLen          = 0;
    i2cRaw[0]       = '\0';
    interrupts();

    String fieldsPart = String(localBuf);
    SerialMonitor.print(F("I2C received fields: "));
    SerialMonitor.println(fieldsPart);

    // Upload to ThingSpeak using existing HTTP format
    sendToThingSpeak(fieldsPart);
  }
}

// =============================================================
// I2C receive handler
// Called when the master sends data to this slave.
// Data may arrive in multiple 30-byte chunks.
// We append to i2cRaw and mark complete when we see "!F".
// =============================================================
void onI2CReceive(int numBytes) {
  // If a message is already ready and main loop hasn’t consumed it yet,
  // we can either discard new bytes or start overwriting.
  // Here we'll discard until main loop resets i2cLen.
  if (i2cMessageReady) {
    while (Wire.available()) {
      (void)Wire.read();
    }
    return;
  }

  uint8_t curLen = i2cLen;

  // Append new bytes to existing buffer, stripping CR/LF
  while (Wire.available() && curLen < (I2C_BUFFER_SIZE - 1)) {
    char c = Wire.read();

    if (c == '\r' || c == '\n') {
      continue;  // ignore CR/LF
    }

    i2cRaw[curLen++] = c;
  }

  // Null-terminate current buffer
  i2cRaw[curLen] = '\0';
  i2cLen         = curLen;

  // Look for "!F" anywhere in the buffer
  if (curLen >= 2) {
    for (uint8_t i = 0; i < (curLen - 1); i++) {
      if (i2cRaw[i] == '!' && i2cRaw[i + 1] == 'F') {
        // Truncate message at the '!' so "!F" is not included
        i2cRaw[i] = '\0';
        i2cLen    = i;
        i2cMessageReady = true;
        break;
      }
    }
  }

  // If buffer filled without term, future chunks will continue appending
  // until either "!F" is found or the buffer saturates.
}

// =============================================================
// sendToThingSpeak
// Expects fieldsPart like:
//   "field1=10&field2=20&field3=30&field4=40&field5=50"
// It prepends "api_key=YOURKEY&" and does the HTTP POST via AT commands
// =============================================================
void sendToThingSpeak(const String &fieldsPart) {
  String domain = "\"api.thingspeak.com\"";

  // Build POST body: api_key=...&field1=...&field2=... ...
  String data = "api_key=" + String(API_WRITE_KEY);
  if (fieldsPart.length() > 0) {
    if (fieldsPart[0] != '&') {
      data += '&';
    }
    data += fieldsPart;
  }

  SerialMonitor.print(F("ThingSpeak POST body: "));
  SerialMonitor.println(data);

  // Configure HTTP profile (same as your original sequence)
  sendAT("AT+UHTTP=0,1," + domain);
  sendAT("AT+UHTTP=0,5,80");
  sendAT("AT+UHTTP=0,6,0");

  // Upload POST body into post.txt on the module
  sendAT("AT+UDWNFILE=\"post.txt\"," + String(data.length()));
  sendAT(data);

  // Perform HTTP POST: /update
  String resp = sendAT("AT+UHTTPC=0,4,\"/update\",\"resp.txt\",\"post.txt\",0", 60000);

  // Read response file (optional but useful for debugging)
  sendAT("AT+URDFILE=\"resp.txt\"");

  SerialMonitor.print(F("HTTP response raw: "));
  SerialMonitor.println(resp);
}

// =============================================================
// Simple AT helper – sends a command & collects reply until timeout
// =============================================================
String sendAT(const String &cmd, uint32_t timeout, bool dbg) {
  lteSerial.println(cmd);
  uint32_t t0 = millis();
  String buffer;
  while (millis() - t0 < timeout) {
    while (lteSerial.available()) {
      char c = lteSerial.read();
      buffer += c;
    }
  }
  if (dbg) {
    SerialMonitor.print(cmd);
    SerialMonitor.print(F(" → "));
    SerialMonitor.println(buffer);
  }
  return buffer;
}
