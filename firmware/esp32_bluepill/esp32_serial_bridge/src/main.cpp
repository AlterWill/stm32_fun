// Pure transparent serial bridge for STM32 flashing: USB <-> Serial2 (pins 16/17)
#include <Arduino.h>

void setup() {
  Serial.begin(115200, SERIAL_8E1);
  Serial2.begin(115200, SERIAL_8E1, 16, 17);
}

void loop() {
  while (Serial.available()) {
    Serial2.write(Serial.read());
  }
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}
