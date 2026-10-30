#include "audio_i2s.h"

#include <driver/i2s.h>

namespace {
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr int kPinBck = 33;
constexpr int kPinWs = 25;
constexpr int kPinData = 32;

bool installed = false;

int16_t convertSample(int32_t raw) {
  // The INMP441 returns 24-bit two's-complement audio in a 32-bit slot.
  // Dropping the low 16 bits leaves a signed 16-bit sample; clamp protects
  // against a slightly over-range value.
  int32_t value = raw >> 16;
  if (value > 32767) return 32767;
  if (value < -32768) return -32768;
  return static_cast<int16_t>(value);
}
}  // namespace

bool audio::begin() {
  if (installed) return true;

  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = kSampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;  // INMP441 L/R pin is GND.
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  // 16 x 256 frames gives the network writes roughly 250 ms of slack before
  // the DMA queue overruns.
  config.dma_buf_count = 16;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;

  if (i2s_driver_install(kI2sPort, &config, 0, nullptr) != ESP_OK) {
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.bck_io_num = kPinBck;
  pins.ws_io_num = kPinWs;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = kPinData;

  if (i2s_set_pin(kI2sPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kI2sPort);
    return false;
  }

  i2s_zero_dma_buffer(kI2sPort);
  installed = true;
  return true;
}

bool audio::discard(uint32_t milliseconds) {
  if (!installed) return false;

  int16_t scratch[kMaxBlockSamples];
  uint32_t deadline = millis() + milliseconds;
  do {
    size_t count = 0;
    if (!readPcm16(scratch, kMaxBlockSamples, count, 50)) return false;
  } while (static_cast<int32_t>(millis() - deadline) < 0);
  return true;
}

bool audio::readPcm16(int16_t *destination, size_t maxSamples, size_t &count,
                      uint32_t timeoutMs) {
  count = 0;
  if (!installed || destination == nullptr || maxSamples == 0) return installed;

  if (maxSamples > kMaxBlockSamples) maxSamples = kMaxBlockSamples;

  int32_t raw[kMaxBlockSamples];
  size_t bytesRead = 0;
  const size_t bytesWanted = maxSamples * sizeof(int32_t);
  const TickType_t ticks = timeoutMs == 0 ? 0 : pdMS_TO_TICKS(timeoutMs);

  esp_err_t result =
      i2s_read(kI2sPort, raw, bytesWanted, &bytesRead, ticks);
  if (result != ESP_OK) return false;

  const size_t samples = bytesRead / sizeof(int32_t);
  for (size_t i = 0; i < samples; ++i) {
    destination[i] = convertSample(raw[i]);
  }
  count = samples;
  return true;
}
