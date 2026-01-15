void setup(){
  Serial.begin(9600);
  delay(200);
  Serial.println("Serial init");
}

void loop(){
  static int i = 0;
  static unsigned long start = millis();
  if (millis() - start > 2000){
    Serial.print("Hello ");
    Serial.println(i);
    i++;
    start = millis();
  }
}