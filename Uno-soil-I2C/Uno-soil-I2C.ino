#include <SDI12.h>
#include <Wire.h>

#define DATA_PIN 2        // Blue wire connected to digital pin 2
#define POWER_PIN 3       // Controls relay power to sensor
#define SLAVE_ADDRESS 0x08  // I2C address of the Maduino Zero

SDI12 mySDI12(DATA_PIN);

String probeAddress = "C";  // Default probe address
unsigned long lastMeasurement = 0;
const unsigned long MEASUREMENT_INTERVAL = 30000; // 30 seconds
const unsigned long POWER_STABILIZATION_DELAY = 5000; // 5 seconds for voltage stabilization
bool sensorPowered = false;

void setup() {
  Serial.begin(9600);
  Wire.begin(); // Initialize I2C as master
  
  // Configure power control pin
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, HIGH); // Start with sensor powered off (relay logic inverted)
  sensorPowered = false;
  
  // Small delay to let everything initialize
  delay(500);
  
  // Power on sensor and initialize
  powerOnSensor();
  
  // Initialize SDI-12 after sensor is powered and stabilized
  mySDI12.begin();
  
  // Initialize and find probe address
  initializeProbe();
  
  // Take first measurement immediately
  takeMeasurements();
  lastMeasurement = millis();
  
  // Power off sensor after first measurement
  powerOffSensor();
}

void loop() {
  // Check if it's time for next measurement
  if (millis() - lastMeasurement >= MEASUREMENT_INTERVAL) {
    // Power on sensor before measurement
    powerOnSensor();
    
    // Re-initialize SDI-12 communication
    mySDI12.begin();
    
    // Take measurements
    takeMeasurements();
    lastMeasurement = millis();
    
    // Power off sensor after measurement to save power
    powerOffSensor();
  }
  
  // Check for manual commands from Serial Monitor
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      // Power on sensor for manual commands
      if (!sensorPowered) {
        powerOnSensor();
        mySDI12.begin();
      }
      
      sendCommand(command);
    }
  }
  
  delay(100); // Small delay to prevent overwhelming the system
}

void powerOnSensor() {
  Serial.println("Powering Sensor On");
  if (!sensorPowered) {
    digitalWrite(POWER_PIN, LOW); // Turn on relay (inverted logic)
    sensorPowered = true;
    
    // Wait for voltage to stabilize
    delay(POWER_STABILIZATION_DELAY);
  }
}

void powerOffSensor() {
  Serial.println("Powering Sensor Off");
  if (sensorPowered) {
    digitalWrite(POWER_PIN, HIGH); // Turn off relay (inverted logic)
    sensorPowered = false;
  }
}

void initializeProbe() {
  // Query probe address
  String response = sendCommandWithResponse("?!");
  if (response.length() > 0) {
    probeAddress = response;
  } else {
    Serial.println("Soil Probe C not detected");
    probeAddress = "C";
  }
  
  delay(1000);
}

void takeMeasurements() {
  // Ensure sensor is powered before measurements
  if (!sensorPowered) {
    return;
  }
  
  // Measure soil moisture with salinity compensation
  measureSoilMoisture();
  
  delay(500); // Small delay between measurements
  
  // Measure temperature in Celsius
  measureTemperature();
}

void measureSoilMoisture() {
  // Send measurement command (C0 = moisture with salinity compensation)
  String measureCommand = probeAddress + "C0!";
  String response = sendCommandWithResponse(measureCommand);
  
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
    String dataResponse = sendCommandWithResponse(dataCommand);
    
    if (dataResponse.length() > 0) {
      parseMoistureData(dataResponse);
    }
  }
}

void measureTemperature() {
  // Send measurement command (C2 = temperature in Celsius)
  String measureCommand = probeAddress + "C2!";
  String response = sendCommandWithResponse(measureCommand);
  
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
    String dataResponse = sendCommandWithResponse(dataCommand);
    
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
  
  // Transmit via I2C
  transmitI2C(outputData);
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
  
  // Transmit via I2C
  transmitI2C(outputData);
}

void transmitI2C(String data) {
  // Split data into chunks if it's too long (I2C buffer limit is typically 32 bytes)
  int maxChunkSize = 30; // Leave some buffer space
  int dataLength = data.length();
  
  for (int i = 0; i < dataLength; i += maxChunkSize) {
    int chunkEnd = min(i + maxChunkSize, dataLength);
    String chunk = data.substring(i, chunkEnd);
    
    // Begin transmission to slave device
    Wire.beginTransmission(SLAVE_ADDRESS);
    delayMicroseconds(10);
    
    // Send chunk as bytes
    Wire.write(chunk.c_str(), chunk.length());
    
    // If this is the last chunk, add newline character
    if (chunkEnd >= dataLength) {
      Wire.write('\n');
    }
    
    // End transmission
    byte error = Wire.endTransmission();
    
    // Check for transmission errors
    if (error != 0) {
      Serial.print("I2C Transmission error: ");
      Serial.println(error);
    }
    
    // Small delay between chunks if there are more
    if (chunkEnd < dataLength) {
      delay(10);
    }
  }
}

String sendCommandWithResponse(String command) {
  mySDI12.sendCommand(command);
  
  // Wait for response with timeout
  String response = "";
  unsigned long startTime = millis();
  const unsigned long timeout = 2000; // 2 second timeout
  
  while (millis() - startTime < timeout) {
    if (mySDI12.available()) {
      char c = mySDI12.read();
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

void sendCommand(String command) {
  sendCommandWithResponse(command);
}