#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Simple, newline-delimited ASCII link between the ESP32 and the Blue Pill.
//
// ESP32 -> Blue Pill:
//   STATUS:<STATE>[:<detail>]\n
//
// STATE tokens are uppercase and must match the values below. The optional
// detail is free-form text and may itself contain colons. Newlines are never
// sent inside a detail string.
namespace hmi {

constexpr uint32_t kSerialBaud = 115200;
constexpr size_t kMaxLine = 96;
constexpr const char *kStatusPrefix = "STATUS:";

enum class State : uint8_t {
  Boot,
  Wifi,
  Ready,
  Recording,
  Uploading,
  Completed,
  Error,
  Unknown,
};

inline const char *stateToken(State state) {
  switch (state) {
    case State::Boot: return "BOOT";
    case State::Wifi: return "WIFI";
    case State::Ready: return "READY";
    case State::Recording: return "RECORDING";
    case State::Uploading: return "UPLOADING";
    case State::Completed: return "COMPLETED";
    case State::Error: return "ERROR";
    default: return "UNKNOWN";
  }
}

inline bool tokenEquals(const char *text, size_t length, const char *expected) {
  return strlen(expected) == length && memcmp(text, expected, length) == 0;
}

inline State stateFromToken(const char *text, size_t length) {
  if (tokenEquals(text, length, "BOOT")) return State::Boot;
  if (tokenEquals(text, length, "WIFI")) return State::Wifi;
  if (tokenEquals(text, length, "READY")) return State::Ready;
  if (tokenEquals(text, length, "RECORDING")) return State::Recording;
  if (tokenEquals(text, length, "UPLOADING")) return State::Uploading;
  if (tokenEquals(text, length, "COMPLETED")) return State::Completed;
  if (tokenEquals(text, length, "ERROR")) return State::Error;
  return State::Unknown;
}

}  // namespace hmi
