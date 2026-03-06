
const int DONE_PIN = 4;
const int RELAY_PIN = 11;

void setup(){
  pinMode(DONE_PIN, OUTPUT);
  digitalWrite(DONE_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);


  Serial.begin(9600);
  
  Serial.println("Timer on for 10 seconds");
  delay(10000);  // wait 10 seconds

  Serial.println("Timer resetting");
  digitalWrite(RELAY_PIN, HIGH);
  delay(1000);
  digitalWrite(RELAY_PIN, LOW);
  delay(2000);  // wait 0.5 seconds for power to stabilize

  Serial.println("Timer sleeping for ~60 seconds");
  digitalWrite(DONE_PIN, HIGH);
  delay(500);
}

void loop(){

 
}