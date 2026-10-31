#include "hmi_link.h"

#include <Arduino.h>

namespace {
constexpr int kUartRxPin = 16;  // ESP32 RX2  <- Blue Pill PA9 (TX)
constexpr int kUartTxPin = 17;  // ESP32 TX2  -> Blue Pill PA10 (RX)
constexpr uint32_t kHeartbeatMs = 5000;
constexpr size_t kDetailMax = 47;

hmi::State currentState = hmi::State::Boot;
char currentDetail[kDetailMax + 1] = "";
bool haveState = false;
uint32_t lastSentAt = 0;

void copyDetail(const char *source, char *destination, size_t capacity) {
  if (capacity == 0) return;
  size_t used = 0;
  if (source != nullptr) {
    for (; *source != '\0' && used + 1 < capacity; ++source) {
      char c = *source;
      if (c == '\r' || c == '\n') c = ' ';
      if (c == '\t') c = ' ';
      destination[used++] = c;
    }
  }
  destination[used] = '\0';
}

void sendState() {
  Serial2.print(hmi::kStatusPrefix);
  Serial2.print(hmi::stateToken(currentState));
  if (currentDetail[0] != '\0') {
    Serial2.print(':');
    Serial2.print(currentDetail);
  }
  Serial2.print('\n');
  Serial2.flush();

  Serial.print("HMI -> ");
  Serial.print(hmi::stateToken(currentState));
  if (currentDetail[0] != '\0') {
    Serial.print(": ");
    Serial.print(currentDetail);
  }
  Serial.println();

  lastSentAt = millis();
}
}  // namespace

void hmi_link::begin() {
  Serial2.begin(hmi::kSerialBaud, SERIAL_8N1, kUartRxPin, kUartTxPin);
}

void hmi_link::setState(hmi::State state, const char *detail) {
  if (state == hmi::State::Unknown) return;

  char cleaned[kDetailMax + 1];
  copyDetail(detail, cleaned, sizeof(cleaned));

  if (haveState && state == currentState &&
      strcmp(cleaned, currentDetail) == 0) {
    return;
  }

  currentState = state;
  strncpy(currentDetail, cleaned, kDetailMax);
  currentDetail[kDetailMax] = '\0';
  haveState = true;
  sendState();
}

void hmi_link::loop() {
  if (!haveState) return;
  if (millis() - lastSentAt >= kHeartbeatMs) {
    sendState();
  }
}
