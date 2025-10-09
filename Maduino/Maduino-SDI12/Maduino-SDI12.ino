#include <SDI12.h>

#define DATA_PIN 2
SDI12 sdi12(DATA_PIN);

void setup() {
  SerialUSB.begin(115200);
  while(!SerialUSB); // wait
  SerialUSB.println("=================== NEW TEST ===================");
  sdi12.begin();
}

void loop() {
  resetSDI();
  queryDevice("?!", 2000);
  resetSDI();
  queryDevice("CI!", 2000);
  delay(5000);
}

void resetSDI(){
  sdi12.end();
  delay(50);     // Allow bus to settle, experiment with 20-100ms
  sdi12.begin();
}


String queryDevice(String cmd, int timeout) {
  while (sdi12.available()){
    sdi12.read();  // clear buffer
  }
  sdi12.sendCommand(cmd);
  
  String response = "";
  unsigned long start = millis();
  
  while (millis() - start < timeout) {
    if (sdi12.available()) {
      char c = sdi12.read();
      if (c == '\n' || c == '\r') break;
      response += c;
    }
  }
  
  SerialUSB.print("Command: " + cmd + " Response: ");
  SerialUSB.println(response);
  
  return response;
}