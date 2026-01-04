#include <SDI12.h>
#include <AltSoftSerial.h>

#define DEBUG true
// #define DEBUG false

AltSoftSerial meshSerial;  // RX=8, TX=9 on Uno
#define SOIL_SENSOR_PIN 2
SDI12 enviroPro(SOIL_SENSOR_PIN);

String probeAddress = "C";
const unsigned long POWER_STABILIZATION_DELAY = 5000; // 5 sec

// Cached values (updated on demand)
String lastMoist = "Moist";
String lastTemp  = "Temp";
const String workerNum = "1";

void setup() {
  Serial.begin(9600);
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
  takeMeasurements();

  String payload = "@w" + workerNum + "r\t" + lastMoist + "\t" + lastTemp;
  sendMesh(payload);
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

void takeMeasurements() {
  measureSoilMoisture();
  delay(500);
  measureTemperature();
}
void measureSoilMoisture() {
  String measureCommand = probeAddress + "C0!";
  String response = sendCommand(measureCommand);
  if (response.length() > 0) {
    int measureTime = 2000;
    // if (response.length() >= 6) {
    //   String timeStr = response.substring(0, 3);
    //   measureTime = timeStr.toInt() * 1000 + 1000;
    // }
    delay(measureTime);

    String dataCommand = probeAddress + "D0!";
    String dataResponse = sendCommand(dataCommand);
    if (dataResponse.length() > 0) parseMoistureData(dataResponse);
  }
}

void measureTemperature() {
  String measureCommand = probeAddress + "C2!";
  String response = sendCommand(measureCommand);
  if (response.length() > 0) {
    int measureTime = 3000;
    if (response.length() >= 6) {
      String timeStr = response.substring(0, 3);
      measureTime = timeStr.toInt() * 1000 + 1000;
    }
    delay(measureTime);

    String dataCommand = probeAddress + "D0!";
    String dataResponse = sendCommand(dataCommand);
    if (dataResponse.length() > 0) parseTemperatureData(dataResponse);
  }
}

void parseMoistureData(String data) {
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
    if (startIndex < data.length() && (data.charAt(startIndex-1) == '+' || data.charAt(startIndex-1) == '-')) {
      int endNum = startIndex;
      while (endNum < data.length() && data.charAt(endNum) != '+' && data.charAt(endNum) != '-') endNum++;
      String value = String(data.charAt(startIndex-1)) + data.substring(startIndex, endNum);
      outputData += "," + value;
      Serial.print("," + value);
      startIndex = endNum;
    }
  }
  Serial.println();
  lastMoist = outputData;  // update cache only
}

void parseTemperatureData(String data) {
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
    if (startIndex < data.length() && (data.charAt(startIndex-1) == '+' || data.charAt(startIndex-1) == '-')) {
      int endNum = startIndex;
      while (endNum < data.length() && data.charAt(endNum) != '+' && data.charAt(endNum) != '-') endNum++;
      String value = String(data.charAt(startIndex-1)) + data.substring(startIndex, endNum);
      outputData += "," + value;
      Serial.print("," + value);
      startIndex = endNum;
    }
  }
  Serial.println();
  lastTemp = outputData;  // update cache only
}

String sendCommand(String command) {
  if (DEBUG) {
    Serial.println("Sending " + command + " to EnviroPro sensor");
  }
  enviroPro.sendCommand(command);
  
  String response = "";
  unsigned long startTime = millis();
  const unsigned long timeout = 3000;  // TODO: timing may need to be tweaked
  if (DEBUG){
    Serial.print("EnviroPro response: ");
  }
  while (millis() - startTime < timeout) {
    if (enviroPro.available()) {
      char c = enviroPro.read();
      response += c;
      if (DEBUG){  // print characters as they come
        Serial.print(c);
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
