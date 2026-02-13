#define SLEEP_PIN 3
// Analog maximum and minimum values
#define A_MAX 255
#define A_MIN 0

// void setup(){
//   Serial.begin(9600);
//   pinMode(SLEEP_PIN, OUTPUT);
//   digitalWrite(SLEEP_PIN, LOW);
//   delay(10000);
//   digitalWrite(SLEEP_PIN, HIGH);
//   delay(1);
//   digitalWrite(SLEEP_PIN, LOW);
// }

void setup(){
  Serial.begin(9600);
  pinMode(SLEEP_PIN, OUTPUT);
  analogWrite(SLEEP_PIN, A_MIN);
  delay(10000);
  analogWrite(SLEEP_PIN, A_MAX);
  delay(1000);
  analogWrite(SLEEP_PIN, A_MIN);
}


void loop(){

}