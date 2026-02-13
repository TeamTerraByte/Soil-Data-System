#define TEST_PIN 3

void setup() {
  Serial.begin(9600);
  delay(200);
  pinMode(TEST_PIN, OUTPUT);
  delay(200);
}

// void loop() {
//   digitalWrite(TEST_PIN, HIGH);
//   delay(2000);
//   digitalWrite(TEST_PIN, LOW);
//   delay(2000);
// }

void loop() {
  analogWrite(TEST_PIN, 255);
  delay(2000);
  analogWrite(TEST_PIN, 0);
  delay(2000);
}
