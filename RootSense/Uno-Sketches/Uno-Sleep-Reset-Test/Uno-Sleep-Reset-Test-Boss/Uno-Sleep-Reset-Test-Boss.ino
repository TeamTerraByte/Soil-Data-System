#define SLEEP_PIN 3
#define TIMER_RESET_PIN 12
// Analog maximum and minimum values
#define A_MAX 255
#define A_MIN 0

void setup(){
  pinMode(TIMER_RESET_PIN, OUTPUT);
  digitalWrite(TIMER_RESET_PIN, LOW);
  pinMode(SLEEP_PIN, OUTPUT);
  digitalWrite(SLEEP_PIN, LOW);
  delay(10000); // wait 10 seconds
  digitalWrite(TIMER_RESET_PIN, HIGH);
  delay(1000);
  digitalWrite(TIMER_RESET_PIN, LOW);
}


// void setup(){
//   Serial.begin(9600);
//   pinMode(SLEEP_PIN, OUTPUT);
//   digitalWrite(SLEEP_PIN, LOW);
//   delay(10000);   
//   digitalWrite(SLEEP_PIN, HIGH);
//   delay(1);
//   digitalWrite(SLEEP_PIN, LOW);
// }

// void setup(){
//   Serial.begin(9600);
//   pinMode(SLEEP_PIN, OUTPUT);
//   analogWrite(SLEEP_PIN, A_MIN);
//   delay(10000);
//   analogWrite(SLEEP_PIN, A_MAX);
//   delay(1000);
//   analogWrite(SLEEP_PIN, A_MIN);
// }


void loop(){

}