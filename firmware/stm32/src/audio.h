#pragma once
#include <stdint.h>
#include <stddef.h>

namespace audio {
constexpr size_t SAMPLES_PER_BLOCK = 128;
bool start();
void stop();
bool take(int16_t *samples);
bool failed();
}
