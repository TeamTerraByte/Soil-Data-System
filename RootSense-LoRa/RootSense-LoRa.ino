#include <SDI12.h>
#include <SoftwareSerial.h>

static const uint8_t UNO_RX = 10;
static const uint8_t UNO_TX = 11; 
SoftwareSerial meshSerial(UNO_RX, UNO_TX); // RX, TX

#define SOIL_SENSOR_PIN 2
SDI12 enviroPro(SOIL_SENSOR_PIN);

String probeAddress = "C";
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 180000; // 3 minutes
const unsigned long POWER_STABILIZATION_DELAY = 5000; // 5 seconds for voltage stabilization


void setup() {
  Serial.begin(9600);
  meshSerial.begin(38400);
  delay(POWER_STABILIZATION_DELAY);

  // Initialize SDI-12 after sensor is powered and stabilized
  enviroPro.begin();
  while(!initializeProbe()) {
    delay(1000);
  }

  // Take first measurement immediately
  takeMeasurements();
  lastMeasurement = millis();
}

void loop() {
  // Check if it's time for next measurement
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {

    // Re-initialize SDI-12 communication
    enviroPro.begin();
    
    // Take measurements
    takeMeasurements();
    lastMeasurement = millis();
  }
  
  // Check for manual commands from Serial Monitor
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      
      sendCommand(command);
    }
  }

  delay(100); // Small delay to prevent overwhelming the system
}

void sendMesh(const String& s) {
  // In TEXTMSG mode, a newline-terminated line is a message.
  meshSerial.print(s);
  meshSerial.print('\n');
  Serial.print(F("Sent: "));
  Serial.println(s);
}

bool initializeProbe() {
  // Query probe address
  String response = sendCommand("?!");
  if (response.length() > 0) {
    probeAddress = response;
    return true;
  } else {
    Serial.println("Soil Probe C not detected");
    probeAddress = "C";
    return false;
  }
  
  delay(1000);
}

void takeMeasurements() {
  // Measure soil moisture with salinity compensation
  measureSoilMoisture();
  
  delay(500); // Small delay between measurements
  
  // Measure temperature in Celsius
  measureTemperature();
}

void measureSoilMoisture() {
  // Send measurement command (C0 = moisture with salinity compensation)
  String measureCommand = probeAddress + "C0!";
  String response = sendCommand(measureCommand);
  
  if (response.length() > 0) {
    // Parse timing from response (format: TTTNNN where TTT is time, NNN is number of values)
    // Wait for measurement to complete (add extra time for safety)
    int measureTime = 3000; // Default 3 seconds if parsing fails
    if (response.length() >= 6) {
      String timeStr = response.substring(0, 3);
      measureTime = timeStr.toInt() * 1000 + 1000; // Convert to ms and add 1s buffer
    }
    
    delay(measureTime);
    
    // Request data
    String dataCommand = probeAddress + "D0!";
    String dataResponse = sendCommand(dataCommand);
    
    if (dataResponse.length() > 0) {
      parseMoistureData(dataResponse);
    }
  }
}

void measureTemperature() {
  // Send measurement command (C2 = temperature in Celsius)
  String measureCommand = probeAddress + "C2!";
  String response = sendCommand(measureCommand);
  
  if (response.length() > 0) {
    // Wait for measurement to complete
    int measureTime = 3000; // Default 3 seconds
    if (response.length() >= 6) {
      String timeStr = response.substring(0, 3);
      measureTime = timeStr.toInt() * 1000 + 1000; // Convert to ms and add 1s buffer
    }
    
    delay(measureTime);
    
    // Request data
    String dataCommand = probeAddress + "D0!";
    String dataResponse = sendCommand(dataCommand);
    
    if (dataResponse.length() > 0) {
      parseTemperatureData(dataResponse);
    }
  }
}

void parseMoistureData(String data) {
  // Data format: address + moisture values separated by + or -
  String outputData = "Moist";
  Serial.print(outputData);
  
  int startIndex = 1; // Skip the address character
  
  while (startIndex < data.length()) {
    int nextPlus = data.indexOf('+', startIndex);
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
      // Find the end of this number
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
  Serial.println(); // End the CSV line
  
  // TODO: transmit data over UART to Meshtastic
  sendMesh(outputData);
}

void parseTemperatureData(String data) {
  // Data format: address + temperature values separated by + or -
  String outputData = "Temp";
  Serial.print(outputData);
  
  int startIndex = 1; // Skip the address character
  
  while (startIndex < data.length()) {
    int nextPlus = data.indexOf('+', startIndex);
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
      // Find the end of this number
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
  Serial.println(); // End the CSV line
  
  // TODO: transmit data over UART to Meshtastic
  sendMesh(outputData);
}

String sendCommand(String command) {
  enviroPro.sendCommand(command);
  
  // Wait for response with timeout
  String response = "";
  unsigned long startTime = millis();
  const unsigned long timeout = 2000; // 2 second timeout
  
  while (millis() - startTime < timeout) {
    if (enviroPro.available()) {
      char c = enviroPro.read();
      if (c == '\n' || c == '\r') {
        if (response.length() > 0) {
          return response;
        }
      } else {
        response += c;
      }
    }
    delay(10);
  }
  
  return response;
}
