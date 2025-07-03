#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#define SOIL_SENSOR_PIN A1
#define DEBUG true

// LTE Control Pins
#define LTE_RESET_PIN 6
#define LTE_PWRKEY_PIN 5
#define LTE_FLIGHT_PIN 7

#define SD_CS_PIN 4  // Adjust if different

String Apikey = ""; // ThingSpeak API Key
File logFile;

void setup() {
  SerialUSB.begin(115200);
  Serial1.begin(115200);

  // LTE module power sequence
  pinMode(LTE_RESET_PIN, OUTPUT);
  digitalWrite(LTE_RESET_PIN, LOW);

  pinMode(LTE_PWRKEY_PIN, OUTPUT);
  delay(100);
  digitalWrite(LTE_PWRKEY_PIN, HIGH);
  delay(2000);
  digitalWrite(LTE_PWRKEY_PIN, LOW);

  pinMode(LTE_FLIGHT_PIN, OUTPUT);
  digitalWrite(LTE_FLIGHT_PIN, LOW); // Normal mode

  delay(5000); // Wait for LTE module

  // LTE network setup
  sendData("AT+CCID", 3000, DEBUG);
  sendData("AT+CREG?", 3000, DEBUG);
  sendData("AT+CGATT=1", 1000, DEBUG);
  sendData("AT+CGACT=1,1", 1000, DEBUG);
  sendData("AT+CGDCONT=1,\"IP\",\"fast.t-mobile.com\"", 1000, DEBUG);

  // SD card initialization
  if (!SD.begin(SD_CS_PIN)) {
    SerialUSB.println("SD card initialization failed!");
  } else {
    SerialUSB.println("SD card ready.");
    logFile = SD.open("SOILMOIS.CSV", FILE_WRITE);
    if (logFile) {
      logFile.println("Timestamp(s),SoilMoisture(%)"); // Header
      logFile.close();
    }
  }

  SerialUSB.println("Soil Sensor 4G LTE Ready!");
}

void loop() {
  int soilRaw = analogRead(SOIL_SENSOR_PIN);
  float soilPercent = (float)(1023 - soilRaw) * 100.0 / 1023.0;
  unsigned long timestamp = millis() / 1000; // seconds since boot

  // Log to Serial
  SerialUSB.print("Time: ");
  SerialUSB.print(timestamp);
  SerialUSB.print("s | Soil Moisture: ");
  SerialUSB.print(soilPercent);
  SerialUSB.println("%");

  // Save to SD card
  logFile = SD.open("SOILMOIS.CSV", FILE_WRITE);
  if (logFile) {
    logFile.print(timestamp);
    logFile.print(",");
    logFile.println(soilPercent);
    logFile.close();
    SerialUSB.println("Data saved to SD card.");
  } else {
    SerialUSB.println("Failed to write to SD card.");
  }

  // Send to ThingSpeak
  sendData("AT+HTTPINIT", 2000, DEBUG);
  String http_str = "AT+HTTPPARA=\"URL\",\"http://api.thingspeak.com/update?api_key=" + Apikey + "&field1=" + String((int)soilPercent) + "\"";
  sendData(http_str, 2000, DEBUG);
  sendData("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"", 1000, DEBUG);
  sendData("AT+HTTPACTION=0", 5000, DEBUG);
  sendData("AT+HTTPTERM", 3000, DEBUG);

  delay(60000); // 2 minutes
}

String sendData(String command, const int timeout, boolean debug) {
  String response = "";
  Serial1.println(command);
  long int time = millis();
  while ((time + timeout) > millis()) {
    while (Serial1.available()) {
      char c = Serial1.read();
      response += c;
    }
  }
  if (debug) {
    SerialUSB.print(response);
  }
  return response;
}
