#include "audio.h"
#include <Arduino.h>

// SPI2 RX is DMA1 stream 3, channel 0 on the STM32F446.
namespace {
I2S_HandleTypeDef i2s;
DMA_HandleTypeDef dma;
constexpr size_t HALF_WORDS = audio::SAMPLES_PER_BLOCK * 4;
uint16_t raw[HALF_WORDS * 2];
constexpr unsigned QUEUE_SIZE = 16;
int16_t blocks[QUEUE_SIZE][audio::SAMPLES_PER_BLOCK];
volatile unsigned head = 0, tail = 0;
volatile bool overflow = false;

void capture(unsigned offset) {
  unsigned next = (head + 1) % QUEUE_SIZE;
  if (next == tail) { overflow = true; return; }
  // I2S delivers left and right 32-bit slots as MSW followed by LSW.
  for (unsigned i = 0; i < audio::SAMPLES_PER_BLOCK; ++i)
    blocks[head][i] = static_cast<int16_t>(raw[offset + i * 4]);
  head = next;
}
}

extern "C" void DMA1_Stream3_IRQHandler() { HAL_DMA_IRQHandler(&dma); }
extern "C" void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *h) {
  if (h == &i2s) capture(0);
}
extern "C" void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *h) {
  if (h == &i2s) capture(HALF_WORDS);
}
extern "C" void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *h) {
  if (h == &i2s) overflow = true;
}

bool audio::start() {
  head = tail = 0; overflow = false;
  __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_SPI2_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE();
  GPIO_InitTypeDef gpio{};
  gpio.Mode = GPIO_MODE_AF_PP; gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate = GPIO_AF5_SPI2;
  gpio.Pin = GPIO_PIN_10 | GPIO_PIN_12; HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Pin = GPIO_PIN_3; HAL_GPIO_Init(GPIOC, &gpio);
  RCC_PeriphCLKInitTypeDef clock{};
  clock.PeriphClockSelection = RCC_PERIPHCLK_I2S_APB1;
  clock.I2sApb1ClockSelection = RCC_I2SAPB1CLKSOURCE_PLLI2S;
  clock.PLLI2S.PLLI2SM = 16; clock.PLLI2S.PLLI2SN = 256; clock.PLLI2S.PLLI2SR = 5;
  if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return false;
  dma.Instance = DMA1_Stream3;
  dma.Init.Channel = DMA_CHANNEL_0;
  dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
  dma.Init.PeriphInc = DMA_PINC_DISABLE; dma.Init.MemInc = DMA_MINC_ENABLE;
  dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  dma.Init.Mode = DMA_CIRCULAR; dma.Init.Priority = DMA_PRIORITY_HIGH;
  dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&dma) != HAL_OK) return false;
  i2s.Instance = SPI2;
  i2s.Init.Mode = I2S_MODE_MASTER_RX;
  i2s.Init.Standard = I2S_STANDARD_PHILIPS;
  i2s.Init.DataFormat = I2S_DATAFORMAT_32B;
  i2s.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  i2s.Init.AudioFreq = 16000;
  i2s.Init.CPOL = I2S_CPOL_LOW;
  i2s.Init.ClockSource = I2S_CLOCK_PLL;
  i2s.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  __HAL_LINKDMA(&i2s, hdmarx, dma);
  if (HAL_I2S_Init(&i2s) != HAL_OK) return false;
  HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
  // HAL doubles the size for 32-bit slots.
  return HAL_I2S_Receive_DMA(&i2s, raw, HALF_WORDS) == HAL_OK;
}

void audio::stop() { HAL_I2S_DMAStop(&i2s); HAL_NVIC_DisableIRQ(DMA1_Stream3_IRQn); HAL_I2S_DeInit(&i2s); HAL_DMA_DeInit(&dma); }
bool audio::take(int16_t *out) {
  if (tail == head) return false;
  memcpy(out, blocks[tail], sizeof(blocks[tail]));
  tail = (tail + 1) % QUEUE_SIZE;
  return true;
}
bool audio::failed() { return overflow; }
