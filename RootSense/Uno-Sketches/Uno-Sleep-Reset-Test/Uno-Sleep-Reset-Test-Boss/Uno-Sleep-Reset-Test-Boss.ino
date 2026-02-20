#include <AltSoftSerial.h>

AltSoftSerial meshSerial;  // RX=8, TX=9
const unsigned long POWER_STABILIZATION_DELAY = 15000; // 15 sec
const int TIMER_RESET_PIN = 12;
const int TIMER_SLEEP_PIN = 4;
unsigned long lastTime = 0;

void sendMesh(const String& s) {
  meshSerial.print(s);
  meshSerial.print('\n');   // final newline after the multi-line payload
  Serial.print(F("Sent: "));
  Serial.println(s);
}

void checkMeshInbound() {
  while (meshSerial.available()) {
    String line = meshSerial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    Serial.print(F("Mesh RX: "));
    Serial.println(line);
  }
}

void checkTimedMeshInbound(unsigned long to){
  unsigned long now = millis();
  unsigned long end = now + to;
  while (now < end){
    checkMeshInbound();
    delay(50);
    now = millis();
  }
}

void setup(){
  pinMode(TIMER_SLEEP_PIN, OUTPUT);
  digitalWrite(TIMER_SLEEP_PIN, LOW);
  pinMode(TIMER_RESET_PIN, OUTPUT);
  digitalWrite(TIMER_RESET_PIN, LOW);

  Serial.begin(9600);
  meshSerial.begin(38400);  
  delay(POWER_STABILIZATION_DELAY);
  lastTime = millis();
}

void loop(){
  unsigned long thisTime = millis();
  if (thisTime - lastTime >= 120000){   // delta > 2 minutes
    lastTime = thisTime;
    sendMesh("@wtq Reset");
    checkTimedMeshInbound(20000);  // wait for a response
    sendMesh("@wtq Sleep");
  }

  checkMeshInbound();
  delay(50);
}