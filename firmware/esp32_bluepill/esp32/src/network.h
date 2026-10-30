#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace network {

enum class PollResult {
  Command,
  Idle,
  NoWifi,
  ServerError,
  BadReply,
};

// Connects to Wi-Fi, blocking for at most ~20 seconds. Safe to call repeatedly.
bool connect();

String localIp();

// Polls GET /api/device/command. On PollResult::Command, commandId contains the
// server command id. On failure, error receives a short human-readable reason.
PollResult pollCommand(String &commandId, String &error);

// Streams one WAV note to POST /api/note using HTTP chunked transfer encoding.
// The caller opens the upload, feeds PCM bytes as they arrive from I2S, then
// finishes the upload and reads the server response. Audio is never buffered
// in full on the ESP32.
class NoteUpload {
 public:
  bool begin(const String &commandId, uint32_t wavDataBytes, String &error);
  bool write(const uint8_t *data, size_t length, String &error);
  bool finish(String &displayText, String &error);
  void abort();

 private:
  WiFiClient client_;
  bool active_ = false;
  uint32_t expectedBytes_ = 0;
  uint32_t sentBytes_ = 0;
};

}  // namespace network
