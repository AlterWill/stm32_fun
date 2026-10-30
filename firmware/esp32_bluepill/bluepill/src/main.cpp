#include <Arduino.h>

#include "display.h"
#include "../../common/hmi_protocol.h"

namespace {
// Onboard Blue Pill LED, active LOW. Optional buzzer on PB0 (active HIGH).
constexpr int kLedPin = LED_BUILTIN;
constexpr int kBuzzerPin = PB0;
constexpr bool kBuzzerActiveHigh = true;

constexpr uint32_t kBeepOnMs = 70;
constexpr uint32_t kBeepOffMs = 120;

char lineBuffer[hmi::kMaxLine];
size_t lineLength = 0;
bool lineOverflow = false;

hmi::State lastState = hmi::State::Unknown;
char lastDetail[48] = "";

uint32_t ledIntervalMs = 1000;
uint32_t lastLedToggleAt = 0;
bool ledOn = false;

int beepsRemaining = 0;
uint32_t buzzerNextAt = 0;
bool buzzerOn = false;

struct StateView {
  const char *title;
  const char *detail;
};

StateView viewFor(hmi::State state) {
  switch (state) {
    case hmi::State::Boot: return {"STARTING", "Waiting for ESP32"};
    case hmi::State::Wifi: return {"WI-FI", "Connecting..."};
    case hmi::State::Ready: return {"READY", "Use phone to record"};
    case hmi::State::Recording: return {"RECORDING", "Speak now"};
    case hmi::State::Uploading: return {"UPLOADING", "Sending audio"};
    case hmi::State::Completed: return {"COMPLETED", "Note saved"};
    case hmi::State::Error: return {"ERROR", "Check ESP32 log"};
    default: return {"UNKNOWN", ""};
  }
}

uint32_t ledIntervalFor(hmi::State state) {
  switch (state) {
    case hmi::State::Completed: return 0;  // solid on
    case hmi::State::Error: return 150;
    case hmi::State::Uploading: return 120;
    case hmi::State::Recording: return 250;
    case hmi::State::Ready: return 1000;
    default: return 500;
  }
}

int beepCountFor(hmi::State state) {
  switch (state) {
    case hmi::State::Ready: return 1;
    case hmi::State::Recording: return 1;
    case hmi::State::Completed: return 2;
    case hmi::State::Error: return 3;
    default: return 0;
  }
}

void writeLed(bool on) {
  digitalWrite(kLedPin, on ? LOW : HIGH);  // active-low onboard LED
}

void writeBuzzer(bool on) {
  digitalWrite(kBuzzerPin, (on == kBuzzerActiveHigh) ? HIGH : LOW);
}

void trim(char *text) {
  if (text == nullptr) return;

  char *start = text;
  while (*start == ' ' || *start == '\t') ++start;
  if (start != text) memmove(text, start, strlen(start) + 1);

  size_t length = strlen(text);
  while (length > 0) {
    char last = text[length - 1];
    if (last != ' ' && last != '\t' && last != '\r') break;
    text[--length] = '\0';
  }
}

void startIndicators(hmi::State state) {
  ledIntervalMs = ledIntervalFor(state);
  lastLedToggleAt = millis();
  ledOn = false;
  writeLed(false);

  beepsRemaining = beepCountFor(state);
  buzzerOn = false;
  writeBuzzer(false);
  buzzerNextAt = millis();
}

void applyState(hmi::State state, const char *detail) {
  if (state == hmi::State::Unknown) return;

  StateView view = viewFor(state);
  const char *text = (detail != nullptr && detail[0] != '\0') ? detail : view.detail;

  // Heartbeats repeat the same state. Ignore them here so the display does not
  // flicker and the buzzer is not retriggered.
  if (state == lastState && strcmp(text, lastDetail) == 0) return;

  lastState = state;
  strncpy(lastDetail, text, sizeof(lastDetail) - 1);
  lastDetail[sizeof(lastDetail) - 1] = '\0';

  display::show(view.title, text);
  startIndicators(state);
}

void handleLine(char *line) {
  const size_t prefixLength = strlen(hmi::kStatusPrefix);
  if (strncmp(line, hmi::kStatusPrefix, prefixLength) != 0) return;

  char *stateText = line + prefixLength;
  char *detail = strchr(stateText, ':');
  if (detail != nullptr) {
    *detail = '\0';
    ++detail;
    trim(detail);
  }
  trim(stateText);

  hmi::State state = hmi::stateFromToken(stateText, strlen(stateText));
  applyState(state, detail);
}

void pollLink() {
  while (Serial1.available() > 0) {
    char c = static_cast<char>(Serial1.read());

    if (c == '\r') continue;
    if (c == '\n') {
      if (!lineOverflow) {
        lineBuffer[lineLength] = '\0';
        if (lineLength > 0) handleLine(lineBuffer);
      }
      lineLength = 0;
      lineOverflow = false;
      continue;
    }

    if (lineOverflow) continue;
    if (lineLength + 1 < sizeof(lineBuffer)) {
      lineBuffer[lineLength++] = c;
    } else {
      // Discard the rest of an overlong line instead of wrapping it into a
      // bogus status update.
      lineOverflow = true;
      lineLength = 0;
    }
  }
}

void updateLed(uint32_t now) {
  if (ledIntervalMs == 0) {
    if (!ledOn) {
      ledOn = true;
      writeLed(true);
    }
    return;
  }

  if (static_cast<int32_t>(now - lastLedToggleAt) >=
      static_cast<int32_t>(ledIntervalMs)) {
    lastLedToggleAt = now;
    ledOn = !ledOn;
    writeLed(ledOn);
  }
}

void updateBuzzer(uint32_t now) {
  if (beepsRemaining <= 0) return;

  if (static_cast<int32_t>(now - buzzerNextAt) < 0) return;

  if (!buzzerOn) {
    buzzerOn = true;
    writeBuzzer(true);
    buzzerNextAt = now + kBeepOnMs;
  } else {
    buzzerOn = false;
    writeBuzzer(false);
    --beepsRemaining;
    buzzerNextAt = now + kBeepOffMs;
  }
}
}  // namespace

void setup() {
  pinMode(kLedPin, OUTPUT);
  writeLed(false);
  pinMode(kBuzzerPin, OUTPUT);
  writeBuzzer(false);

  // USART1: PA10 = RX (from ESP32 GPIO18), PA9 = TX (to ESP32 GPIO16).
  // On this core, Serial is USART1 by default, so the link uses Serial1
  // explicitly and no debug output is sent on the ESP32 link.
  Serial1.setRx(PA10);
  Serial1.setTx(PA9);
  Serial1.begin(hmi::kSerialBaud, SERIAL_8N1);

  bool displayReady = display::begin();
  if (displayReady) {
    applyState(hmi::State::Boot, nullptr);
  } else {
    applyState(hmi::State::Error, "OLED init failed");
  }
}

void loop() {
  pollLink();

  uint32_t now = millis();
  updateLed(now);
  updateBuzzer(now);
  delay(1);
}
