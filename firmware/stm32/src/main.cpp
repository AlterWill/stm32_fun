#include <Arduino.h>
#include "audio.h"
#include "display.h"
#include "../../common/protocol.h"

Uart bridge(PC7, PC6); // USART6 RX, TX
namespace {
voice_link::Parser parser;
voice_link::Frame incoming;
void sendText(uint8_t type, const char *text) {
  voice_link::Frame f{};
  f.type = type;
  f.length = min(strlen(text), voice_link::MAX_PAYLOAD);
  memcpy(f.payload, text, f.length);
  voice_link::send(bridge, f);
}

void record(uint32_t seconds) {
  if (seconds == 0 || seconds > 300 || !audio::start()) {
    sendText(voice_link::ABORT, "Audio init failed");
    display::show("Problem", "Audio init failed");
    return;
  }
  display::show("Recording", "Speak now");
  // Discard the microphone's startup period before acknowledging readiness.
  uint32_t deadline = millis() + 3000;
  int16_t samples[audio::SAMPLES_PER_BLOCK];
  unsigned discarded = 0;
  while (discarded < 40 && !audio::failed() && int32_t(millis() - deadline) < 0) {
    if (audio::take(samples)) ++discarded;
  }
  if (discarded != 40 || audio::failed()) {
    audio::stop(); sendText(voice_link::ABORT, "Audio startup failed"); return;
  }
  sendText(voice_link::HELLO, "ready");
  uint32_t remaining = seconds * 16000UL;
  uint32_t sequence = 0;
  deadline = millis() + seconds * 1000UL + 5000;
  while (remaining && !audio::failed() && int32_t(millis() - deadline) < 0) {
    while (bridge.available()) {
      if (parser.push(bridge.read(), incoming) && incoming.type == voice_link::ABORT) {
        audio::stop(); display::show("Problem", "Upload stopped"); return;
      }
    }
    if (!audio::take(samples)) continue;
    size_t count = min(remaining, uint32_t(audio::SAMPLES_PER_BLOCK));
    voice_link::Frame f{}; f.type = voice_link::AUDIO; f.sequence = sequence++;
    f.length = count * 2;
    for (size_t i = 0; i < count; ++i) {
      f.payload[i * 2] = uint16_t(samples[i]);
      f.payload[i * 2 + 1] = uint16_t(samples[i]) >> 8;
    }
    voice_link::send(bridge, f);
    remaining -= count;
  }
  bool failed = remaining || audio::failed();
  audio::stop();
  if (failed) { sendText(voice_link::ABORT, "Audio overrun"); display::show("Problem", "Audio overrun"); }
  else { voice_link::Frame f{}; f.type = voice_link::END; f.sequence = sequence; voice_link::send(bridge, f); display::show("Uploading", "Please wait"); }
}
}

void setup() {
  Serial.begin(115200);
  bridge.begin(921600);
  display::begin();
  display::show("Voice note", "Waiting for network");
}

void loop() {
  while (bridge.available()) {
    if (!parser.push(bridge.read(), incoming)) continue;
    if (incoming.type == voice_link::START && incoming.length == 4) {
      uint32_t seconds = uint32_t(incoming.payload[0]) | uint32_t(incoming.payload[1]) << 8 |
                         uint32_t(incoming.payload[2]) << 16 | uint32_t(incoming.payload[3]) << 24;
      record(seconds);
    } else if (incoming.type == voice_link::STATUS) {
      char text[65]; size_t n = min(size_t(incoming.length), sizeof(text) - 1);
      memcpy(text, incoming.payload, n); text[n] = 0;
      display::show("Voice note", text);
    }
  }
}
