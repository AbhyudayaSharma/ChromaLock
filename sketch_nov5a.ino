#include <Servo.h>

int isButtonPressed = LOW;
Servo myservo;

void setup() {
  // put your setup code here, to run once:
  pinMode(6, INPUT);
  myservo.attach(5);
  Serial.begin(9600);
  while (!Serial);
}

void loop() {
  // put your main code here, to run repeatedly:
  int status = digitalRead(6);

  // Serial.println(status);
  if (status != isButtonPressed) {
    isButtonPressed = status;
    Serial.println(status == HIGH ? "Button pressed" : "Button released");
  }

  myservo.write(isButtonPressed == HIGH ? 180 : 0);
  delay(100);
}
