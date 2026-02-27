#include <AltSoftSerial.h>

AltSoftSerial meshSerial;  // RX=8, TX=9
const unsigned long POWER_STABILIZATION_DELAY = 15000; // 15 sec
const int TIMER_RESET_PIN = 12;
const int TIMER_DONE_PIN = 4;

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

    if (line.indexOf("@wtq Reset") != -1) {
      sendMesh("@wtr Resetting");
      delay(1000);
      digitalWrite(TIMER_RESET_PIN, HIGH);
      delay(1000);  // whole system should be off by now, but pulse JIC
      digitalWrite(TIMER_RESET_PIN, LOW);
    }
    else if (line.indexOf("@wtq Sleep") != -1){
      sendMesh("@wtr Sleeping");
      delay(10000);
      digitalWrite(TIMER_DONE_PIN, HIGH);
      delay(10000);  // whole system should be sleeping by now, but pulse JIC
      digitalWrite(TIMER_DONE_PIN, LOW);
    }
  }
}

void setup(){
  pinMode(TIMER_DONE_PIN, OUTPUT);
  digitalWrite(TIMER_DONE_PIN, LOW);
  pinMode(TIMER_RESET_PIN, OUTPUT);
  digitalWrite(TIMER_RESET_PIN, LOW);

  Serial.begin(9600);
  meshSerial.begin(38400);  
  delay(POWER_STABILIZATION_DELAY);
}

void loop(){
  checkMeshInbound();
  delay(50);
}