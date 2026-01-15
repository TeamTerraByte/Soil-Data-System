#define batteryPin A0
// WARNING: This method only works for batteries producing less than 5V
// Tutorial used for this code: https://www.youtube.com/watch?v=lec7kPv3VS8
// For voltages > 5V, see this video: https://www.youtube.com/watch?v=hixEGmf1y5c 

// Analog pins reads between 0 and 1023
// 0 = 0V and 1023 = 5V

const float mv_per_value = 4.882; 

void setup(){
  Serial.begin(9600);
  pinMode(batteryPin, INPUT);
}

void loop(){
  int batteryReading = analogRead(batteryPin);
  // convert reading -> ratio -> 4.77 V max 
  float batteryVolts = batteryReading / 1023.0 * 4.77;
  Serial.println("Battery reading " + String(batteryReading));
  Serial.println("Battery voltage " + String(batteryVolts));
  Serial.println();  // blank line
  delay(5000);
}