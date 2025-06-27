#include <Wire.h>

#define SLAVE_ADDRESS 0x08  // This device's I2C address

// Variables to store assembled data
String moistData = "";
String tempData = "";
String currentDataType = "";
bool assemblingData = false;

void setup() {
  Wire.begin(SLAVE_ADDRESS);     // Initialize I2C as slave with address 0x08
  Wire.onReceive(receiveEvent);  // Register function to handle incoming data
  SerialUSB.begin(115200);            // Initialize SerialUSB communication
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
        processMoistureData(moistData);
        moistData = ""; // Clear for next transmission
      } 
      else if (currentDataType == "Temp") {
        SerialUSB.println("=== COMPLETE TEMPERATURE DATA ===");
        SerialUSB.println(tempData);
        SerialUSB.println("==================================");
        // You can process the complete temperature CSV string here
        processTemperatureData(tempData);
        tempData = ""; // Clear for next transmission
      }
      
      currentDataType = "";
    }
  }
}

void processMoistureData(String completeData) {
  // Process the complete moisture CSV string
  // You can add your specific processing logic here

  // Example: prepare for LoRa transmission, store in memory, etc.
}

void processTemperatureData(String completeData) {
  // Process the complete temperature CSV string
  // You can add your specific processing logic here

  // Example: prepare for LoRa transmission, store in memory, etc.
}