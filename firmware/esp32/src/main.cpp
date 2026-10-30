#include <Arduino.h>
#include "network.h"

#ifndef RECORD_SECONDS
#define RECORD_SECONDS 60
#endif

HardwareSerial bridge(2);
void setup() {
  Serial.begin(115200);
  bridge.setRxBufferSize(32768);
  bridge.begin(921600, SERIAL_8N1, 16, 17);
}
void loop() {
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll >= 5000) {
    lastPoll = millis();
    String id;
    if (network::command(id)) network::upload(id, RECORD_SECONDS);
  }
  delay(20);
}
