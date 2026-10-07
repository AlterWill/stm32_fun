#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

// ==============================================================================
// HARDWARE DEFINITIONS & PINOUT
// ==============================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_SDA      21
#define OLED_SCL      22
#define OLED_ADDR     0x3C

#define I2S_WS        25
#define I2S_SCK       33
#define I2S_SD        32
#define I2S_PORT      I2S_NUM_0

#define SAMPLE_RATE   16000
#define DMA_BUF_COUNT 8
#define DMA_BUF_LEN   512
#define SAMPLES_PER_CHUNK 512

typedef struct {
  int16_t buffer[SAMPLES_PER_CHUNK];
} AudioChunk_t;

QueueHandle_t audioQueue = NULL;

// ==============================================================================
// WI-FI & WEBSOCKET CONFIGURATION
// ==============================================================================
const char* WIFI_SSID     = "A";
const char* WIFI_PASSWORD = "rishon11";

const char* WS_HOST       = "endurable-hardwood-hundredth.ngrok-free.dev";
const uint16_t WS_PORT    = 443;
const char* WS_PATH       = "/ws/audio";

// ==============================================================================
// GLOBAL INSTANCES & STATE
// ==============================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebSocketsClient webSocket;
WiFiClientSecure sslClient;

volatile bool isWsConnected = false;
String lastTranscription = "";
String connectionStatus = "Connecting Wi-Fi...";

TaskHandle_t audioTaskHandle = NULL;

// ==============================================================================
// OLED DISPLAY HELPER
// ==============================================================================
void updateDisplay() {
  display.clearDisplay();
  display.setTextWrap(true);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(connectionStatus);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 14);
  if (lastTranscription.length() > 0) {
    display.print(lastTranscription);
  } else if (isWsConnected) {
    display.print("Streaming audio...\nWaiting for speech...");
  }
  display.display();
}

// ==============================================================================
// I2S MICROPHONE INITIALIZATION (INMP441)
// ==============================================================================
bool setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
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

// ==============================================================================
// CORE 0: READ I2S AUDIO AND PUSH TO QUEUE
// ==============================================================================
void audioStreamingTask(void *pvParameters) {
  int32_t rawSamples[SAMPLES_PER_CHUNK];
  AudioChunk_t chunk;
  size_t bytesRead = 0;
  for (;;) {
    esp_err_t result = i2s_read(I2S_PORT, rawSamples, sizeof(rawSamples), &bytesRead, portMAX_DELAY);
    if (result == ESP_OK && bytesRead > 0) {
      int samplesCount = bytesRead / sizeof(int32_t);
      for (int i = 0; i < samplesCount; i++) {
        chunk.buffer[i] = (int16_t)(rawSamples[i] >> 14);
      }
      if (isWsConnected && audioQueue != NULL) {
        xQueueSend(audioQueue, &chunk, 0);
      }
    }
  }
}

// ==============================================================================
// WEBSOCKET EVENT CALLBACK
// ==============================================================================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      isWsConnected = false;
      connectionStatus = "WS Disconnected";
      Serial.println("[WSS] Disconnected!");
      if (audioQueue != NULL) xQueueReset(audioQueue);
      updateDisplay();
      break;
    case WStype_CONNECTED:
      isWsConnected = true;
      connectionStatus = "Connected (WSS)";
      Serial.printf("[WSS] Connected to: %s\n", payload);
      updateDisplay();
      break;
    case WStype_TEXT: {
      String text = String((char*)payload);
      Serial.printf("[WSS Text]: %s\n", text.c_str());
      lastTranscription = text;
      updateDisplay();
      break;
    }
    default: break;
  }
}

// ==============================================================================
// SETUP
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 Audio Streamer + STT (Ngrok WSS) ===");

  Wire.begin(OLED_SDA, OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Connecting Wi-Fi...");
    display.display();
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWi-Fi Connected! IP: %s\n", WiFi.localIP().toString().c_str());

  if (!setupI2S()) {
    Serial.println("I2S setup FAILED!");
  }

  audioQueue = xQueueCreate(10, sizeof(AudioChunk_t));

  // Configure explicit SSL Client settings
  sslClient.setInsecure();            // Bypass certificate verification
  sslClient.setHandshakeTimeout(10);  // Prevent handshake hangs

  // Pass custom sslClient directly to WebSocketsClient
  webSocket.beginSslWithCA(WS_HOST, WS_PORT, WS_PATH, nullptr, "");
  
  String headers = "Host: " + String(WS_HOST) + "\r\n";
  headers += "User-Agent: ESP32\r\n";
  headers += "ngrok-skip-browser-warning: 69420\r\n";
  webSocket.setExtraHeaders(headers.c_str());

  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(15000, 4000, 2);

  xTaskCreatePinnedToCore(audioStreamingTask, "AudioTask", 8192, NULL, 1, &audioTaskHandle, 0);

  connectionStatus = "Connecting WSS...";
  updateDisplay();
}

// ==============================================================================
// MAIN LOOP
// ==============================================================================
void loop() {
  webSocket.loop();

  if (isWsConnected && audioQueue != NULL) {
    AudioChunk_t chunk;
    if (xQueueReceive(audioQueue, &chunk, 0) == pdTRUE) {
      webSocket.sendBIN((uint8_t*)chunk.buffer, SAMPLES_PER_CHUNK * sizeof(int16_t));
    }
  }
}
