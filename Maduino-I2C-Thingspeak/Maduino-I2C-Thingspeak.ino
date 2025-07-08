#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include "secrets.h"

#define DEBUG true
#define SLAVE_ADDRESS 0x08  // This device's I2C address

// LTE Control Pins
#define LTE_RESET_PIN 6
#define LTE_PWRKEY_PIN 5
#define LTE_FLIGHT_PIN 7

// Variables to store assembled data
String moistData = "";
String tempData = "";
String currentDataType = "";
bool assemblingData = false;

String Apikey = API_WRITE_KEY; // ThingSpeak API Key

// FUNCTION PROTOTYPES
String sendAT(const String& cmd, uint32_t to = 2000, bool dbg = DEBUG);
void resetLTE();
void ltePowerSequence();
bool moistReady = false;
bool tempReady = false;

void setup() {
  SerialUSB.begin(115200);            // Initialize SerialUSB communication
  Serial1.begin(115200);
  if (DEBUG) while(!SerialUSB);
  while(!Serial1);

  // LTE module power sequence
  pinMode(LTE_RESET_PIN, OUTPUT);
  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  pinMode(LTE_FLIGHT_PIN, OUTPUT);
  ltePowerSequence();

  Wire.begin(SLAVE_ADDRESS);     // Initialize I2C as slave with address 0x08
  Wire.onReceive(receiveEvent);  // Register function to handle incoming data

  SerialUSB.println("Maduino Zero I2C Slave Ready");
  SerialUSB.println("Waiting for data from Arduino Uno...");
}

void loop() {
  // Main loop can do other tasks
  // I2C communication is handled by interrupt
  delay(500);
}

// Function called when I2C data is received
void receiveEvent(int numBytes) {
  String receivedString = "";
  
  // Read all incoming bytes and build the string
  while (Wire.available()) {
    char c = Wire.read();
    receivedString += c;
  }
  
  // Print the raw received data for debugging
  SerialUSB.print("Raw received: ");
  SerialUSB.println(receivedString);
  SerialUSB.print("Bytes received: ");
  SerialUSB.println(numBytes);
  
  // Process the received data
  processReceivedData(receivedString);
  
  SerialUSB.println("---");
}

void processReceivedData(String data) {
  // Check if this is the start of a new data transmission
  if (data.startsWith("Moist,")) {
    // Start of moisture data
    currentDataType = "Moist";
    moistData = data;
    assemblingData = true;
    SerialUSB.println("Started assembling Moisture data");
  } 
  else if (data.startsWith("Temp,")) {
    // Start of temperature data
    currentDataType = "Temp";
    tempData = data;
    assemblingData = true;
    SerialUSB.println("Started assembling Temperature data");
  }
  else if (assemblingData) {
    // This is a continuation of the current data type
    if (currentDataType == "Moist") {
      moistData += data;
    } else if (currentDataType == "Temp") {
      tempData += data;
    }
    
    // Check if this appears to be the end of the transmission
    // (looking for data that ends with a comma followed by few characters or just comma)
    if (data.length() < 15) {
      // This looks like the end of transmission
      assemblingData = false;
      
      if (currentDataType == "Moist") {
        SerialUSB.println("=== COMPLETE MOISTURE DATA ===");
        SerialUSB.println(moistData);
        SerialUSB.println("===============================");
        // You can process the complete moisture CSV string here
        
        moistReady = true;
      }
      else if (currentDataType == "Temp") {
        SerialUSB.println("=== COMPLETE TEMPERATURE DATA ===");
        SerialUSB.println(tempData);
        SerialUSB.println("==================================");
        // You can process the complete temperature CSV string here

        tempReady = true;
      }
      
      if (moistReady && tempReady){
        SerialUSB.println("Attempting to upload data...");
        uploadData(moistData, tempData);
        moistData = "";
        tempData = "";
        moistReady = false;
        tempReady = false;
      }

      currentDataType = "";
    }
  }
}

void uploadData(String moist, String temp) {
  ltePowerSequence();

  moist.replace("\n", "");
  temp.replace("\n", "");

  sendAT("AT+HTTPINIT", 2000, DEBUG);
  String http_str = "AT+HTTPPARA=\"URL\",\"http://api.thingspeak.com\"";
  // String http_str = "AT+HTTPPARA=\"URL\",\"http://api.thingspeak.com/update?api_key=" + Apikey;
  // http_str += "&field1=" + moist;
  // http_str += "&field2=" + temp + "\"";
  sendAT(http_str, 2000, DEBUG);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"", 1000, DEBUG);
  sendAT("AT+HTTPACTION=0", 30000, DEBUG);
  sendAT("AT+HTTPTERM", 3000, DEBUG);
}

/* --- SEND AT COMMAND to 4G LTE MODULE --- */
String sendAT(const String& cmd, uint32_t to, bool dbg) {
    String resp;
    SerialUSB.println("Sending " + cmd);
    Serial1.println(cmd);
    unsigned long t0 = millis();
    while (millis() - t0 < to) while (Serial1.available()) resp += (char)Serial1.read();
    if (dbg) SerialUSB.print("Received " + resp);
    return resp;
}

void ltePowerSequence(){
  delay(100);
  digitalWrite(LTE_RESET_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_RESET_PIN, LOW);
  
  delay(100);
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  digitalWrite(LTE_FLIGHT_PIN, LOW); // Normal mode

  delay(30000); // Wait for LTE module

  // LTE network setup
  sendAT("AT+CCID", 3000, DEBUG);
  sendAT("AT+CREG?", 3000, DEBUG);
  sendAT("AT+CGATT=1", 1000, DEBUG);
  sendAT("AT+CGACT=1,1", 1000, DEBUG);
  sendAT("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"", 1000, DEBUG);
  sendAT("AT+CGPADDR=1", 3000, DEBUG);          // show pdp address
}