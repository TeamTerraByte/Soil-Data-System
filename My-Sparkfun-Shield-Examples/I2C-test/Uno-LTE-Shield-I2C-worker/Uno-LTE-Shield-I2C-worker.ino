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
#define MAX_OPERATORS    5

// Network operator can be set to either:
// MNO_SW_DEFAULT -- DEFAULT
// MNO_ATT -- AT&T 
// MNO_VERIZON -- Verizon
// MNO_TELSTRA -- Telstra
// MNO_TMO -- T-Mobile
const mobile_network_operator_t MOBILE_NETWORK_OPERATOR = MNO_ATT;

// Keep this as simple const C strings (no heap use from String objects)
const char* const MOBILE_NETWORK_STRINGS[] = {
  "Default",
  "SIM_ICCD",
  "AT&T",
  "VERIZON",
  "TELSTRA",
  "T-Mobile",
  "CT"
};

// APN as a const C string; library should accept this directly
const char APN[] = "Broadband";

// Volatile because modified in ISR
volatile bool    i2cMessageReady = false;
volatile uint8_t i2cLen          = 0;
volatile char    i2cRaw[I2C_BUFFER_SIZE];

// ---------- Global LTE / Serial ----------
SoftwareSerial lteSerial(8, 9);  // RX, TX for LTE shield
LTE_Shield     lte;

// ---------- Forward declarations ----------
String sendAT(const String &cmd, uint32_t timeout = 2000, bool dbg = DEBUG);
void   sendToThingSpeak(const String &fieldsPart);
void   onI2CReceive(int numBytes);
void   printInfo(void);
void   printOperators(struct operator_stats * ops, int operatorsAvailable);
int    searchOp(String longOp, struct operator_stats * ops, int operatorsAvailable);

// =============================================================
// Setup
// =============================================================
void setup() {
  int opsAvailable;
  struct operator_stats ops[MAX_OPERATORS];
  String currentOperator = "";
  bool newConnection = true;

  SerialMonitor.begin(9600);
  SerialMonitor.println(F("SerialMonitor connected"));

  // Initialize LTE shield
  if (lte.begin(lteSerial, 9600)) {
    SerialMonitor.println(F("LTE Shield connected!"));
  } else {
    SerialMonitor.println(F("Unable to initialize the shield."));
  }

  if (lte.getOperator(&currentOperator) == LTE_SHIELD_SUCCESS) {
    SerialMonitor.print(F("Current operator: "));
    SerialMonitor.println(currentOperator);
    if (currentOperator == F("AT&T")) {
      SerialMonitor.println(F("Current operator is AT&T, skipping 'newConnection'"));
      newConnection = false;
    }
  } else {
    SerialMonitor.println(F("getOperator failed!"));
  }

  if (newConnection) {
    // Set MNO to either Verizon, T-Mobile, AT&T, Telstra, etc.
    // This will narrow the operator options during our scan later
    SerialMonitor.println(F("Setting mobile-network operator"));
    if (lte.setNetwork(MOBILE_NETWORK_OPERATOR)) {
      SerialMonitor.print(F("Set mobile network operator to "));
      SerialMonitor.println(MOBILE_NETWORK_STRINGS[MOBILE_NETWORK_OPERATOR]);
      SerialMonitor.println();
    } else {
      SerialMonitor.println(F("Error setting MNO. Try cycling power to the shield/Arduino."));
      while (1) ;
    }
    
    // Set the APN -- Access Point Name -- e.g. "hologram"
    SerialMonitor.println(F("Setting APN..."));
    if (lte.setAPN(APN) == LTE_SHIELD_SUCCESS) {
      SerialMonitor.println(F("APN successfully set."));
      SerialMonitor.println();
    } else {
      SerialMonitor.println(F("Error setting APN. Try cycling power to the shield/Arduino."));
      while (1) ;
    }

    SerialMonitor.println(F("Scanning for operators...this may take up to 3 minutes"));
    SerialMonitor.println();

    // lte.getOperators takes in an operator_stats struct pointer and max number of
    // structs to scan for, then fills up those objects with operator names and numbers
    opsAvailable = lte.getOperators(ops, MAX_OPERATORS); // This will block for up to 3 minutes

    if (opsAvailable > 0) {
      // Pretty-print operators we found:
      SerialMonitor.print(F("Found "));
      SerialMonitor.print(opsAvailable);
      SerialMonitor.println(F(" operators:"));
      printOperators(ops, opsAvailable);

      int myOp = searchOp(F("AT&T"), ops, opsAvailable);
      if (myOp != -1) {
        SerialMonitor.print(F("Connecting to option "));
        SerialMonitor.println(myOp);
        if (lte.registerOperator(ops[myOp]) == LTE_SHIELD_SUCCESS) {
          SerialMonitor.print(F("Network "));
          SerialMonitor.print(ops[myOp].longOp);
          SerialMonitor.println(F(" registered"));
          SerialMonitor.println();
        } else {
          SerialMonitor.println(F("Error connecting to operator. Reset and try again, or try another network."));
        }
      }
    } else {
      SerialMonitor.println(F("Did not find an operator. Double-check SIM and antenna, reset and try again, or try another network."));
      while (1) ;
    }
  }

  // At the very end print connection information
  printInfo();
  delay(2000);

  // I2C: act as a slave device
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(onI2CReceive);

  SerialMonitor.println(F("Ready! I2C slave + LTE passthrough."));
  SerialMonitor.println();
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
  String data = F("api_key=");
  data += API_WRITE_KEY;
  if (fieldsPart.length() > 0) {
    if (fieldsPart[0] != '&') {
      data += '&';
    }
    data += fieldsPart;
  }

  SerialMonitor.print(F("ThingSpeak POST body: "));
  SerialMonitor.println(data);

  // Configure HTTP profile
  sendAT(String(F("AT+UHTTP=0,1,")) + domain);
  sendAT(F("AT+UHTTP=0,5,80"));
  sendAT(F("AT+UHTTP=0,6,0"));

  // Delete the old file
  sendAT(F("AT+UDELFILE=\"post.txt\""));

  // Upload POST body into post.txt on the module
  sendAT(String(F("AT+UDWNFILE=\"post.txt\",")) + String(data.length()));
  sendAT(data);

  sendAT(F("AT+URDFILE=\"post.txt\""));  // read the file for confirmation

  // Perform HTTP POST: /update
  String resp = sendAT(F("AT+UHTTPC=0,4,\"/update\",\"resp.txt\",\"post.txt\",0"), 60000);

  // Read response file (optional but useful for debugging)
  sendAT(F("AT+URDFILE=\"resp.txt\""));

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

void printInfo(void) {
  String    currentApn = "";
  IPAddress ip(0, 0, 0, 0);
  String    currentOperator = "";

  SerialMonitor.println(F("Connection info:"));

  // APN Connection info: APN name and IP
  if (lte.getAPN(&currentApn, &ip) == LTE_SHIELD_SUCCESS) {
    SerialMonitor.print(F("APN: "));
    SerialMonitor.println(currentApn);

    SerialMonitor.print(F("IP: "));
    SerialMonitor.println(ip);
  }

  // Operator name or number
  if (lte.getOperator(&currentOperator) == LTE_SHIELD_SUCCESS) {
    SerialMonitor.print(F("Operator: "));
    SerialMonitor.println(currentOperator);
  }

  // Received signal strength
  SerialMonitor.print(F("RSSI: "));
  SerialMonitor.println(lte.rssi());

  SerialMonitor.println();
}

void printOperators(struct operator_stats * ops, int operatorsAvailable) {
  for (int i = 0; i < operatorsAvailable; i++) {
    SerialMonitor.print(i + 1);
    SerialMonitor.print(F(": "));
    SerialMonitor.print(ops[i].longOp);
    SerialMonitor.print(F(" ("));
    SerialMonitor.print(ops[i].numOp);
    SerialMonitor.print(F(") - "));

    switch (ops[i].stat) {
      case 0:
        SerialMonitor.println(F("UNKNOWN"));
        break;
      case 1:
        SerialMonitor.println(F("AVAILABLE"));
        break;
      case 2:
        SerialMonitor.println(F("CURRENT"));
        break;
      case 3:
        SerialMonitor.println(F("FORBIDDEN"));
        break;
    }
  }
  SerialMonitor.println();
}

int searchOp(String longOp, struct operator_stats * ops, int operatorsAvailable) {
  for (int i = 0; i < operatorsAvailable; i++) {
    if (ops[i].longOp == longOp) {
      return i;
    }
  }
  return -1;  // -1 indicates that the op was not found
}
