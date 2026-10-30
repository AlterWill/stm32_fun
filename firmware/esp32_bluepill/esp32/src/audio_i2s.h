#pragma once

#include <Arduino.h>

namespace audio {

constexpr int kSampleRate = 16000;
constexpr size_t kMaxBlockSamples = 256;

// Installs and starts I2S0 in master receive mode. The INMP441 needs 32-bit
// slots even though this project uploads 16-bit PCM, so the driver uses 32-bit
// slots and readPcm16() converts the samples.
bool begin();

// Throws away the currently queued microphone data. Call this immediately
// before a recording so stale DMA samples do not end up in the WAV file.
bool discard(uint32_t milliseconds);

// Reads up to maxSamples mono 16-bit samples. Returns false on a driver error.
// A timeout with no data is reported as true with count == 0.
bool readPcm16(int16_t *destination, size_t maxSamples, size_t &count,
               uint32_t timeoutMs);

}  // namespace audio
