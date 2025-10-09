#include <SDI12.h>
#include <AltSoftSerial.h>      

// AltSoftSerial on Uno uses fixed pins: RX=8, TX=9
AltSoftSerial meshSerial;        // no pin arguments

#define SOIL_SENSOR_PIN 2
SDI12 enviroPro(SOIL_SENSOR_PIN);

String probeAddress = "C";
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 180000; // 3 minutes
const unsigned long POWER_STABILIZATION_DELAY = 5000; // 5 seconds

void setup() {
  Serial.begin(9600);
  meshSerial.begin(38400);   // Mesh on D8/D9 via AltSoftSerial
  delay(POWER_STABILIZATION_DELAY);

  enviroPro.begin();
  while(!initializeProbe()) {
    delay(1000);
  }

  takeMeasurements();
  lastMeasurement = millis();
}

void loop() {
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {
    enviroPro.begin();  // re-init SDI-12 if you power-cycle the sensor between reads
    takeMeasurements();
    lastMeasurement = millis();
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      sendCommand(command);
    }
  }

  delay(100);
}

void sendMesh(const String& s) {
  meshSerial.print(s);
  meshSerial.print('\n');
  Serial.print(F("Sent: "));
  Serial.println(s);
}

bool initializeProbe() {
  String response = sendCommand("?!");
  if (response.length() > 0) {
    probeAddress = response;
    return true;
  } else {
    Serial.println(F("Soil Probe C not detected"));
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
    int measureTime = 3000;
    if (response.length() >= 6) {
      String timeStr = response.substring(0, 3);
      measureTime = timeStr.toInt() * 1000 + 1000;
    }
    delay(measureTime);

    String dataCommand = probeAddress + "D0!";
    String dataResponse = sendCommand(dataCommand);

    if (dataResponse.length() > 0) {
      parseMoistureData(dataResponse);
    }
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

    if (dataResponse.length() > 0) {
      parseTemperatureData(dataResponse);
    }
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
      outputData += ",";
      outputData += value;
      Serial.print(",");
      Serial.print(value);
    }

    startIndex = nextDelim + 1;
    if (startIndex < data.length() && (data.charAt(startIndex-1) == '+' || data.charAt(startIndex-1) == '-')) {
      int endNum = startIndex;
      while (endNum < data.length() && data.charAt(endNum) != '+' && data.charAt(endNum) != '-') {
        endNum++;
      }
      String value = String(data.charAt(startIndex-1)) + data.substring(startIndex, endNum);
      outputData += ",";
      outputData += value;
      Serial.print(",");
      Serial.print(value);
      startIndex = endNum;
    }
  }
  Serial.println();
  sendMesh(outputData);
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
      outputData += ",";
      outputData += value;
      Serial.print(",");
      Serial.print(value);
    }

    startIndex = nextDelim + 1;
    if (startIndex < data.length() && (data.charAt(startIndex-1) == '+' || data.charAt(startIndex-1) == '-')) {
      int endNum = startIndex;
      while (endNum < data.length() && data.charAt(endNum) != '+' && data.charAt(endNum) != '-') {
        endNum++;
      }
      String value = String(data.charAt(startIndex-1)) + data.substring(startIndex, endNum);
      outputData += ",";
      outputData += value;
      Serial.print(",");
      Serial.print(value);
      startIndex = endNum;
    }
  }
  Serial.println();
  sendMesh(outputData);
}

String sendCommand(String command) {
  enviroPro.sendCommand(command);

  String response = "";
  unsigned long startTime = millis();
  const unsigned long timeout = 2000;

  while (millis() - startTime < timeout) {
    if (enviroPro.available()) {
      char c = enviroPro.read();
      if (c == '\n' || c == '\r') {
        if (response.length() > 0) return response;
      } else {
        response += c;
      }
    }
    delay(10);
  }
  return response;
}
