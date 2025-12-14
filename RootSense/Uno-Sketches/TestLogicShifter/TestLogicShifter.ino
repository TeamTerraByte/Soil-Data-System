/*
  Logic Level Shifter Test
  - Drives pins 8 and 9 HIGH to verify the shifter outputs.
  - Comment/uncomment the indicated lines to drive pins LOW.
*/

static const uint8_t PIN_A = 8;
static const uint8_t PIN_B = 9;

void setup() {
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);

  // --- Set outputs HIGH ---
  digitalWrite(PIN_A, HIGH);
  digitalWrite(PIN_B, HIGH);

  // --- To test LOW output instead, uncomment these and comment out the HIGH lines ---
  // digitalWrite(PIN_A, LOW);
  // digitalWrite(PIN_B, LOW);
}

void loop() {
  // Nothing required — signals are static.
}
