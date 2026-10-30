#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "audio_i2s.h"
#include "hmi_link.h"
#include "network.h"

#ifndef RECORD_SECONDS
#define RECORD_SECONDS 10
#endif

#if RECORD_SECONDS < 1 || RECORD_SECONDS > 300
#error "RECORD_SECONDS must be between 1 and 300"
#endif

namespace {
constexpr uint32_t kCommandPollMs = 5000;
constexpr uint32_t kWifiRetryMs = 5000;
constexpr uint32_t kCompletedHoldMs = 3000;
constexpr uint32_t kRecordingGraceMs = 5000;
constexpr uint32_t kWavBytes = RECORD_SECONDS * 32000UL;

uint32_t nextPollAt = 0;
uint32_t nextWifiAttemptAt = 0;
uint32_t completedUntil = 0;
bool wifiReady = false;

void showReady() {
  if (!wifiReady) return;
  String ip = network::localIp();
  hmi_link::setState(hmi::State::Ready, ip.c_str());
}

void showError(const String &message) {
  Serial.printf("Error: %s\n", message.c_str());
  hmi_link::setState(hmi::State::Error, message.c_str());
}

void recordAndUpload(const String &commandId) {
  network::NoteUpload upload;
  String error;

  // Open the HTTP request first. The server waits for the WAV body while the
  // microphone stream is captured, so RAM usage stays bounded.
  if (!upload.begin(commandId, kWavBytes, error)) {
    showError(error);
    return;
  }

  // The I2S DMA buffers keep filling while the network connection is opened.
  // Discard those stale samples so the note starts with the user's speech.
  if (!audio::discard(150)) {
    upload.abort();
    showError("I2S read failed");
    return;
  }

  hmi_link::setState(hmi::State::Recording, "Speak now");

  int16_t block[audio::kMaxBlockSamples];
  uint32_t sentBytes = 0;
  uint32_t deadline = millis() + RECORD_SECONDS * 1000UL + kRecordingGraceMs;

  while (sentBytes < kWavBytes) {
    size_t remainingSamples = (kWavBytes - sentBytes) / 2;
    size_t wantedSamples =
        remainingSamples < audio::kMaxBlockSamples
            ? remainingSamples
            : audio::kMaxBlockSamples;

    size_t samples = 0;
    if (!audio::readPcm16(block, wantedSamples, samples, 100)) {
      upload.abort();
      showError("I2S read failed");
      return;
    }

    if (samples == 0) {
      if (static_cast<int32_t>(millis() - deadline) >= 0) {
        upload.abort();
        showError("Microphone timeout");
        return;
      }
      continue;
    }

    size_t bytes = samples * sizeof(int16_t);
    if (bytes > kWavBytes - sentBytes) bytes = kWavBytes - sentBytes;

    if (!upload.write(reinterpret_cast<const uint8_t *>(block), bytes, error)) {
      showError(error);
      return;
    }
    sentBytes += bytes;
  }

  // All audio has been transmitted. The final chunk and the server's JSON
  // response are the only remaining steps.
  hmi_link::setState(hmi::State::Uploading, "Saving note");

  String displayText;
  if (!upload.finish(displayText, error)) {
    showError(error);
    return;
  }

  Serial.printf("Note uploaded: %s\n", displayText.c_str());
  hmi_link::setState(hmi::State::Completed,
                     displayText.length() ? displayText.c_str() : "Saved");
  completedUntil = millis() + kCompletedHoldMs;
}

void retryWifiIfNeeded() {
  if (wifiReady || static_cast<int32_t>(millis() - nextWifiAttemptAt) < 0) {
    return;
  }

  hmi_link::setState(hmi::State::Wifi, "Connecting Wi-Fi");
  wifiReady = network::connect();
  if (wifiReady) {
    showReady();
    nextPollAt = millis() + 500;
  } else {
    showError("Wi-Fi failed");
    nextWifiAttemptAt = millis() + kWifiRetryMs;
  }
}

void pollServer() {
  if (static_cast<int32_t>(millis() - nextPollAt) < 0) return;
  nextPollAt = millis() + kCommandPollMs;

  String commandId;
  String error;
  switch (network::pollCommand(commandId, error)) {
    case network::PollResult::Command:
      Serial.printf("Command: record %s\n", commandId.c_str());
      recordAndUpload(commandId);
      break;

    case network::PollResult::Idle:
      showReady();
      break;

    case network::PollResult::NoWifi:
      wifiReady = false;
      showError(error);
      nextWifiAttemptAt = millis() + 2000;
      break;

    case network::PollResult::ServerError:
    case network::PollResult::BadReply:
      showError(error);
      break;
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== ESP32 voice recorder + Blue Pill HMI ===");
  Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  Serial.printf("Record length: %u s\n", static_cast<unsigned>(RECORD_SECONDS));

  hmi_link::begin();
  hmi_link::setState(hmi::State::Boot, "ESP32 starting");

  if (!audio::begin()) {
    showError("I2S init failed");
    while (true) {
      hmi_link::loop();
      delay(100);
    }
  }

  hmi_link::setState(hmi::State::Wifi, "Connecting Wi-Fi");
  wifiReady = network::connect();
  if (wifiReady) {
    showReady();
  } else {
    showError("Wi-Fi failed");
    nextWifiAttemptAt = millis() + kWifiRetryMs;
  }

  nextPollAt = millis() + 500;
}

void loop() {
  hmi_link::loop();

  if (wifiReady && WiFi.status() != WL_CONNECTED) {
    wifiReady = false;
    showError("Wi-Fi lost");
    nextWifiAttemptAt = millis() + 2000;
  }

  if (completedUntil != 0 &&
      static_cast<int32_t>(millis() - completedUntil) >= 0) {
    completedUntil = 0;
    showReady();
  }

  retryWifiIfNeeded();

  // Keep "Recorded" on the OLED briefly before returning to Ready.
  if (wifiReady && completedUntil == 0) {
    pollServer();
  }

  delay(20);
}
