#include <SDI12.h>  
#include <AltSoftSerial.h>

#define DEBUG true
// #define DEBUG false

AltSoftSerial meshSerial;  // RX=8, TX=9 on Uno
#define SOIL_SENSOR_PIN 2
SDI12 enviroPro(SOIL_SENSOR_PIN);

// --- Battery monitor (integrated from Nano-Battery-Monitor.ino) ---
// NOTE: This direct-read method only works if the battery voltage presented to A0 is < 5V.
// If your battery can exceed ~5V at any time, use a resistor divider (or other scaling) first.
const int batteryPin = A0;
// Calibrated ADC (Analog-to-Digital Converter) reference voltage (in Volts).
// This should match the actual Vcc seen by the ATmega328P for best accuracy.
const float ADC_REF = 4.96;

String probeAddress = "C";
const unsigned long POWER_STABILIZATION_DELAY = 5000; // 5 sec

const String workerNum = "2";


struct Measurements {
  String moist;
  String temp;
};

float readBatteryVolts();
String batteryField();

String sendCommand(String command, const unsigned long timeout = 3000);


void setup() {
  Serial.begin(9600);
  pinMode(batteryPin, INPUT);
  if (DEBUG){  // Debug purposes
    while(!Serial){
      delay(1000);
    }
    Serial.println("Serial inited");
  }
  meshSerial.begin(38400);
  delay(POWER_STABILIZATION_DELAY);

  enviroPro.begin();
  while (!initializeProbe()) {
    delay(1000);
  }
}

void loop() {
  // Allow manual SDI-12 commands via USB Serial (debug)
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0){
      // My custom command to debug measurements
      if (command.indexOf("takeMeasurements()") != -1){
        takeMeasurements();
      }
      else {
        sendCommand(command);
      }
    }
  }

  // Listen for inbound Meshtastic TEXTMSG lines
  checkMeshInbound();

  
  delay(50); // normal delay for normal operation
  // delay(10000);  // long delay for debugging
}

void sendMesh(const String& s) {
  meshSerial.print(s);
  meshSerial.print('\n');   // final newline after the multi-line payload
  Serial.print(F("Sent (tab separated payload):\n"));
  Serial.println(s);
}

void respondToNodes() {
  // Fresh measurements on demand
  // enviroPro.begin();  // TODO find out if I need to initialize again
  Measurements m = takeMeasurements();

  // Battery reading appended as the final tab-separated field
  String batt = batteryField();

  String payload = "@w" + workerNum + "r\t" + m.moist + "\t" + m.temp + "\t" + batt;
  sendMesh(payload);
}

float readBatteryVolts() {
  // Analog pins read between 0 and 1023, where 1023 corresponds to ADC_REF volts.
  const int reading = analogRead(batteryPin);
  const float volts = (reading / 1023.0) * ADC_REF;

  if (DEBUG) {
    Serial.print(F("Battery raw ADC: "));
    Serial.println(reading);
    Serial.print(F("Battery volts: "));
    Serial.println(volts, 3);
  }
  return volts;
}

String batteryField() {
  // Keep the same "Label,value" style as the other fields.
  const float v = readBatteryVolts();
  return String(F("Batt,")) + String(v, 2);
}

void checkMeshInbound() {
  while (meshSerial.available()) {
    String line = meshSerial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    Serial.print(F("Mesh RX: "));
    Serial.println(line);

    if (line.indexOf("@w" + workerNum + "q") != -1) {
      respondToNodes();
    }
  }
}


bool initializeProbe() {
  String response = sendCommand("?!");
  if ((response.length() == 1) && isalnum(response[0])) {
    if (DEBUG) Serial.println("Setting probe address to " + response);
    probeAddress = response;
    return true;
  } else {
    Serial.println(F("Soil Probe not detected"));
    probeAddress = "C";
    return false;
  }
}

Measurements takeMeasurements() {
  Measurements m;
  m.moist = measureSoilMoisture();
  delay(500);
  m.temp  = measureTemperature();
  return m;
}


String measureSoilMoisture() {
  String measureCommand = probeAddress + "C0!";
  String dataCommand    = probeAddress + "D0!";
  const int MAX_TRIES   = 5;
  int try_num = 0;

  int measureTime = 2000;  
  String response = "";    

  while (try_num < MAX_TRIES) {
    Serial.println("Moisture Measure Attempt # " + String(try_num));

    response = sendCommand(measureCommand);
    delay(measureTime);
    String dataResponse = sendCommand(dataCommand);

    if (dataResponse.length() == 57 && hasValidChars(dataResponse.substring(1))) {
      return parseMoistureData(dataResponse);
    }
    try_num++;
  }

  return "Error measuring soil moisture";
}


String measureTemperature() {
  String measureCommand = probeAddress + "C2!";
  String dataCommand    = probeAddress + "D0!";
  const int MAX_TRIES   = 5;
  int try_num = 0;

  int measureTime = 2000;
  String response = "";

  while (try_num < MAX_TRIES) {
    Serial.println("Temperature Measure Attempt # " + String(try_num));

    response = sendCommand(measureCommand);
    delay(measureTime);
    String dataResponse = sendCommand(dataCommand);

    if (dataResponse.length() == 57 && hasValidChars(dataResponse.substring(1))) {
      return parseTemperatureData(dataResponse);
    }
    try_num++;
  }
  return "Error measuring soil temp";
}

String parseMoistureData(String data) {
  String outputData = "Moist";
  Serial.print(outputData);

  int startIndex = 1; // skip address char
  while (startIndex < data.length()) {
    int nextPlus  = data.indexOf('+', startIndex);
    int nextMinus = data.indexOf('-', startIndex);
    int nextDelim = -1;

    if (nextPlus == -1 && nextMinus == -1) break;
    if (nextPlus == -1) nextDelim = nextMinus;
    else if (nextMinus == -1) nextDelim = nextPlus;
    else nextDelim = min(nextPlus, nextMinus);

    if (nextDelim > startIndex) {
      String value = data.substring(startIndex, nextDelim);
      outputData += "," + value;
      Serial.print("," + value);
    }

    startIndex = nextDelim + 1;
    if (startIndex < data.length() &&
        (data.charAt(startIndex - 1) == '+' || data.charAt(startIndex - 1) == '-')) {
      int endNum = startIndex;
      while (endNum < data.length() && data.charAt(endNum) != '+' && data.charAt(endNum) != '-') {
        endNum++;
      }
      String value = String(data.charAt(startIndex - 1)) + data.substring(startIndex, endNum);
      outputData += "," + value;
      Serial.print("," + value);
      startIndex = endNum;
    }
  }

  Serial.println();
  return outputData;
}

String parseTemperatureData(String data) {
  String outputData = "Temp";
  Serial.print(outputData);

  int startIndex = 1;
  while (startIndex < data.length()) {
    int nextPlus  = data.indexOf('+', startIndex);
    int nextMinus = data.indexOf('-', startIndex);
    int nextDelim = -1;

    if (nextPlus == -1 && nextMinus == -1) break;
    if (nextPlus == -1) nextDelim = nextMinus;
    else if (nextMinus == -1) nextDelim = nextPlus;
    else nextDelim = min(nextPlus, nextMinus);

    if (nextDelim > startIndex) {
      String value = data.substring(startIndex, nextDelim);
      outputData += "," + value;
      Serial.print("," + value);
    }

    startIndex = nextDelim + 1;
    if (startIndex < data.length() &&
        (data.charAt(startIndex - 1) == '+' || data.charAt(startIndex - 1) == '-')) {
      int endNum = startIndex;
      while (endNum < data.length() && data.charAt(endNum) != '+' && data.charAt(endNum) != '-') {
        endNum++;
      }
      String value = String(data.charAt(startIndex - 1)) + data.substring(startIndex, endNum);
      outputData += "," + value;
      Serial.print("," + value);
      startIndex = endNum;
    }
  }

  Serial.println();
  return outputData;
}

String sendCommand(String command, const unsigned long timeout = 3000) {
  if (DEBUG) {
    Serial.println("Sending " + command + " to EnviroPro sensor");
  }
  enviroPro.sendCommand(command);
  
  String response;
  response.reserve(256);
  unsigned long startTime = millis();
  if (DEBUG){
    // Serial.println("startTime: " + String(startTime));
    // Serial.println("timeout: " + String(timeout));
    // Serial.print("EnviroPro response: ");
  }
  while (millis() - startTime < timeout) {
    if (enviroPro.available()) {
      char c = enviroPro.read();
      response += c;
      if (DEBUG){  // print characters as they come
        Serial.write(c);
      }
    }
    delay(10);
  }

  response.trim();
  if (DEBUG){
    // Serial.println("EnviroPro response: " + response);
    Serial.print("\n");  // newline to end response
    Serial.println("Response length: " + (String) response.length());
  }
  return response;
}

bool hasValidChars(const String& input) {
  const char* acceptable = "+-.0123456789";

  for (int i = 0; i < input.length(); i++) {
    if (strchr(acceptable, input[i]) == nullptr) {
      return false;
    }
  }
  return true;
}
