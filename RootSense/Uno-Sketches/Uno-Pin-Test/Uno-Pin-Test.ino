#define TEST_PIN 9

void setup() {
  Serial.begin(9600);
  delay(200);
  pinMode(TEST_PIN, OUTPUT);
  delay(200);
}

void loop() {
  digitalWrite(TEST_PIN, HIGH);
  delay(2000);
  digitalWrite(TEST_PIN, LOW);
  delay(2000);

}
