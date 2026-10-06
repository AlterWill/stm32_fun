#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <math.h>

// ==========================================
// PIN & HARDWARE DEFINITIONS
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

#define OLED_SDA 21
#define OLED_SCL 22

#define I2S_WS   25
#define I2S_SCK  33
#define I2S_SD   32
#define I2S_PORT I2S_NUM_0

#define SAMPLES_BLOCK 128

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledFound = false;
uint8_t oledAddress = 0x3C;
bool i2sActive = false;

// Audio processing buffers & variables
int32_t i2sBuffer[SAMPLES_BLOCK];
int16_t waveHistory[SCREEN_WIDTH];

float rmsLeft = 0.0f;
float rmsRight = 0.0f;
float activeRMS = 0.0f;
float smoothedRMS = 0.0f;
float noiseFloor = 5.0f;
int32_t peakVal = 0;
bool activeIsRight = false;

// UI & Display states
int uiMode = 0; // 0 = Waveform & VU Meter, 1 = Expressive Pet Face
unsigned long lastModeSwitch = 0;
unsigned long lastSerialTime = 0;
unsigned long frameCount = 0;
float fps = 0.0f;
unsigned long lastFpsTime = 0;

// Robot face animation
int eyeOffsetX = 0;
int eyeOffsetY = 0;
bool isBlinking = false;
unsigned long nextBlinkTime = 0;

// ==========================================
// I2C & OLED INITIALIZATION
// ==========================================
bool initOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      if (address == 0x3C || address == 0x3D) {
        oledAddress = address;
        oledFound = true;
      }
    }
  }

  if (oledFound) {
    display.begin(SSD1306_SWITCHCAPVCC, oledAddress);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println("ESP32 MIC & OLED");
    display.setCursor(10, 35);
    display.println("System Ready!");
    display.display();
    delay(500);
  }
  return oledFound;
}

// ==========================================
// I2S MICROPHONE DRIVER SETUP
// ==========================================
bool initI2S() {
  i2s_driver_uninstall(I2S_PORT);

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Captures both Left & Right slots
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = SAMPLES_BLOCK,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) return false;

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) return false;

  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

// ==========================================
// AUDIO SAMPLING & PROCESSING
// ==========================================
void processAudio() {
  size_t bytesRead = 0;
  esp_err_t result = i2s_read(I2S_PORT, i2sBuffer, sizeof(i2sBuffer), &bytesRead, 100 / portTICK_PERIOD_MS);

  if (result == ESP_OK && bytesRead > 0) {
    int totalWords = bytesRead / sizeof(int32_t);
    int pairs = totalWords / 2;

    double sumL = 0, sumR = 0;
    int32_t pkL = 0, pkR = 0;
    int32_t latestSample = 0;

    for (int i = 0; i < pairs; i++) {
      int32_t rawL = i2sBuffer[2 * i] >> 14;
      int32_t rawR = i2sBuffer[2 * i + 1] >> 14;

      if (labs(rawL) > pkL) pkL = labs(rawL);
      if (labs(rawR) > pkR) pkR = labs(rawR);

      sumL += (double)rawL * (double)rawL;
      sumR += (double)rawR * (double)rawR;
    }

    if (pairs > 0) {
      rmsLeft = sqrt(sumL / pairs);
      rmsRight = sqrt(sumR / pairs);

      // Auto-detect channel with audio data
      if (rmsRight > rmsLeft * 1.5f && rmsRight > 5.0f) {
        activeIsRight = true;
      } else if (rmsLeft > rmsRight * 1.5f && rmsLeft > 5.0f) {
        activeIsRight = false;
      }

      activeRMS = activeIsRight ? rmsRight : rmsLeft;
      peakVal = activeIsRight ? pkR : pkL;

      // Adaptive noise floor tracking
      if (activeRMS > 0.1f) {
        noiseFloor = noiseFloor * 0.99f + activeRMS * 0.01f;
      }

      float effectiveLevel = activeRMS - noiseFloor;
      if (effectiveLevel < 0) effectiveLevel = 0;
      smoothedRMS = smoothedRMS * 0.8f + effectiveLevel * 0.2f;

      latestSample = activeIsRight ? (i2sBuffer[1] >> 14) : (i2sBuffer[0] >> 14);
    }

    // Update oscilloscope rolling buffer
    for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
      waveHistory[i] = waveHistory[i + 1];
    }
    int16_t plotY = (int16_t)(latestSample / 6);
    if (plotY > 20) plotY = 20;
    if (plotY < -20) plotY = -20;
    waveHistory[SCREEN_WIDTH - 1] = plotY;
  }
}

// ==========================================
// UI DRAWING: WAVEFORM & LEVEL MONITOR
// ==========================================
void drawWaveformUI() {
  display.clearDisplay();

  // Header Title
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("AUDIO OSCILLOSCOPE");

  // State Tag
  display.setCursor(98, 0);
  if (smoothedRMS > 150.0f) {
    display.print("LOUD");
  } else if (smoothedRMS > 25.0f) {
    display.print("VOICE");
  } else {
    display.print("IDLE");
  }

  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Oscilloscope waveform (y: 10 to 46)
  int centerY = 27;
  display.drawFastHLine(0, centerY, 128, SSD1306_WHITE);
  for (int x = 0; x < SCREEN_WIDTH - 1; x++) {
    int y1 = centerY - waveHistory[x];
    int y2 = centerY - waveHistory[x + 1];
    display.drawLine(x, y1, x + 1, y2, SSD1306_WHITE);
  }

  // Dynamic Level VU Bar (y: 48 to 55)
  display.drawRect(0, 48, 128, 7, SSD1306_WHITE);
  int barWidth = (int)((smoothedRMS / 300.0f) * 124.0f);
  if (barWidth > 124) barWidth = 124;
  if (barWidth < 0) barWidth = 0;
  if (barWidth > 0) {
    display.fillRect(2, 50, barWidth, 3, SSD1306_WHITE);
  }

  // Footer Stats
  display.setCursor(0, 57);
  display.printf("RMS:%-4d PK:%-4d %2.0fFPS", (int)activeRMS, (int)peakVal, fps);

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println("==================================================");
  Serial.println("   ESP32 Audio & Display Interactive System       ");
  Serial.println("==================================================");

  initOLED();
  i2sActive = initI2S();

  for (int i = 0; i < SCREEN_WIDTH; i++) {
    waveHistory[i] = 0;
  }
}

void loop() {
  if (i2sActive) {
    processAudio();
  }

  if (oledFound) {
    drawWaveformUI();
  }

  // Calculate FPS
  frameCount++;
  unsigned long now = millis();
  if (now - lastFpsTime >= 1000) {
    fps = (frameCount * 1000.0f) / (now - lastFpsTime);
    frameCount = 0;
    lastFpsTime = now;
  }

  // Serial Diagnostics
  if (now - lastSerialTime >= 400) {
    lastSerialTime = now;
    Serial.printf("[AUDIO] RMS: %6.1f | Peak: %5ld | Status: %s | Active Channel: %s\n",
                  activeRMS, (long)peakVal,
                  (smoothedRMS > 150.0f) ? "LOUD" : ((smoothedRMS > 25.0f) ? "VOICE/SOUND" : "IDLE"),
                  activeIsRight ? "RIGHT (VDD)" : "LEFT (GND)");
  }

  delay(4);
}
