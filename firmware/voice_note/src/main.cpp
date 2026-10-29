#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <cstring>

#include "secrets.h"

#define I2S_SCK 33
#define I2S_WS 25
#define I2S_SD 32
#define I2S_PORT I2S_NUM_0

#define SAMPLE_RATE 16000
// Override with a PlatformIO build_flags definition, or change this default.
// Audio is sent in HTTP chunks as it is sampled; this is not a RAM buffer.
#ifndef RECORD_SECONDS
#define RECORD_SECONDS 60
#endif
#define COMMAND_POLL_MS 5000
#define STATUS_POLL_MS 2000

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool displayReady = false;
constexpr size_t BYTES_PER_SECOND = SAMPLE_RATE * sizeof(int16_t);
const size_t wavDataLength = BYTES_PER_SECOND * RECORD_SECONDS;

bool pollStatusOnce(const String &noteId);

void showLines(const String &line1, const String &line2 = "", const String &line3 = "") {
  Serial.print(line1);
  if (line2.length() > 0) {
    Serial.print(" | ");
    Serial.print(line2);
  }
  if (line3.length() > 0) {
    Serial.print(" | ");
    Serial.print(line3);
  }
  Serial.println();

  if (!displayReady) {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(true);
  display.setCursor(0, 0);
  display.println(line1);
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);
  display.setCursor(0, 14);
  if (line2.length() > 0) {
    display.println(line2);
  }
  if (line3.length() > 0) {
    display.println(line3);
  }
  display.display();
}

void showReply(const char *face, const char *text) {
  String header = "Voice note";
  if (strcmp(face, "done") == 0) {
    header = "Done";
  } else if (strcmp(face, "wait") == 0) {
    header = "Please wait";
  } else if (strcmp(face, "sad") == 0) {
    header = "Problem";
  }
  showLines(header, text);
}

void put16(uint8_t *p, uint16_t value) {
  p[0] = value & 0xFF;
  p[1] = (value >> 8) & 0xFF;
}

void put32(uint8_t *p, uint32_t value) {
  p[0] = value & 0xFF;
  p[1] = (value >> 8) & 0xFF;
  p[2] = (value >> 16) & 0xFF;
  p[3] = (value >> 24) & 0xFF;
}

void writeWavHeader(uint8_t *p, uint32_t dataLength) {
  memcpy(p, "RIFF", 4);
  put32(p + 4, 36 + dataLength);
  memcpy(p + 8, "WAVEfmt ", 8);
  put32(p + 16, 16);
  put16(p + 20, 1);
  put16(p + 22, 1);
  put32(p + 24, SAMPLE_RATE);
  put32(p + 28, SAMPLE_RATE * 2);
  put16(p + 32, 2);
  put16(p + 34, 16);
  memcpy(p + 36, "data", 4);
  put32(p + 40, dataLength);
}

bool setupI2S() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;

  if (i2s_driver_install(I2S_PORT, &config, 0, NULL) != ESP_OK) {
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_SCK;
  pins.ws_io_num = I2S_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = I2S_SD;

  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
    return false;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  showLines("Connecting Wi-Fi", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    Serial.printf("Wi-Fi OK: %s\n", ip.c_str());
    showLines("Wi-Fi connected", ip, "Use phone to record");
    return true;
  }

  Serial.println("Wi-Fi failed");
  showLines("Wi-Fi failed", "Check secrets.h");
  return false;
}

bool fetchCommand(String &commandId) {
  if (!connectWiFi()) {
    return false;
  }

  HTTPClient http;
  http.begin(String(SERVER_URL) + "/api/device/command");
  http.setTimeout(15000);
  http.addHeader("X-API-Key", API_KEY);

  int status = http.GET();
  String body = http.getString();
  http.end();

  if (status != 200) {
    Serial.printf("GET /api/device/command -> %d\n", status);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    Serial.println("Bad command reply");
    return false;
  }

  const char *command = doc["command"] | "";
  const char *id = doc["id"] | "";
  if (strcmp(command, "record") == 0 && id[0] != '\0') {
    commandId = id;
    return true;
  }
  return false;
}

bool parseServerUrl(String &host, uint16_t &port) {
  String url = SERVER_URL;
  int scheme = url.indexOf("://");
  if (scheme < 0) {
    return false;
  }

  String rest = url.substring(scheme + 3);
  int slash = rest.indexOf('/');
  if (slash >= 0) {
    rest = rest.substring(0, slash);
  }

  int colon = rest.indexOf(':');
  if (colon >= 0) {
    host = rest.substring(0, colon);
    port = rest.substring(colon + 1).toInt();
  } else {
    host = rest;
    port = 80;
  }
  return host.length() > 0 && port > 0;
}

bool sendChunk(WiFiClient &client, const uint8_t *data, size_t length) {
  if (length == 0) {
    return true;
  }

  client.printf("%X\r\n", static_cast<unsigned int>(length));
  size_t sent = 0;
  while (sent < length) {
    size_t written = client.write(data + sent, length - sent);
    if (written == 0) {
      return false;
    }
    sent += written;
  }
  client.print("\r\n");
  return true;
}

String httpBody(const String &response) {
  int headerEnd = response.indexOf("\r\n\r\n");
  if (headerEnd < 0) {
    return "";
  }
  return response.substring(headerEnd + 4);
}

String readHttpResponse(WiFiClient &client, int &statusCode) {
  String response;
  uint32_t started = millis();
  statusCode = 0;

  while (millis() - started < 15000) {
    while (client.available()) {
      response += static_cast<char>(client.read());
      if (response.length() > 8192) {
        break;
      }
    }

    int headerEnd = response.indexOf("\r\n\r\n");
    if (headerEnd >= 0) {
      String headers = response.substring(0, headerEnd);
      int contentLengthPos = headers.indexOf("Content-Length:");
      if (contentLengthPos < 0) {
        contentLengthPos = headers.indexOf("content-length:");
      }
      if (contentLengthPos >= 0) {
        int lineEnd = headers.indexOf("\r\n", contentLengthPos);
        if (lineEnd < 0) {
          lineEnd = headers.length();
        }
        int contentLength = headers.substring(contentLengthPos + 15, lineEnd).toInt();
        if (contentLength >= 0 &&
            response.length() >= static_cast<size_t>(headerEnd + 4 + contentLength)) {
          break;
        }
      }
    }

    if (!client.connected() && !client.available()) {
      break;
    }
    delay(10);
  }

  if (response.startsWith("HTTP/1.")) {
    int space = response.indexOf(' ');
    if (space >= 0) {
      statusCode = response.substring(space + 1, space + 4).toInt();
    }
  }
  return response;
}

bool streamNote(const String &commandId, String &noteId) {
  if (!connectWiFi()) {
    return false;
  }

  String host;
  uint16_t port = 0;
  if (!parseServerUrl(host, port)) {
    showLines("Bad URL", "Check secrets.h");
    return false;
  }

  showLines("Connecting", host);
  WiFiClient client;
  client.setTimeout(15000);
  client.setNoDelay(true);
  if (!client.connect(host.c_str(), port)) {
    showReply("sad", "No server");
    return false;
  }

  client.print("POST /api/note HTTP/1.1\r\n");
  client.print("Host: " + host + ":" + String(port) + "\r\n");
  client.print("X-API-Key: " API_KEY "\r\n");
  if (commandId.length() > 0) {
    client.print("X-Command-Id: " + commandId + "\r\n");
  }
  client.print("Content-Type: audio/wav\r\n");
  client.print("Transfer-Encoding: chunked\r\n");
  client.print("Connection: close\r\n\r\n");

  uint8_t header[44];
  writeWavHeader(header, wavDataLength);
  if (!sendChunk(client, header, sizeof(header))) {
    client.stop();
    showReply("sad", "Send error");
    return false;
  }

  showLines("Recording", "Speak now");
  size_t sent = 0;
  uint32_t started = millis();
  uint32_t lastDisplay = started;

  while (sent < wavDataLength) {
    int32_t raw[256];
    size_t bytesRead = 0;
    esp_err_t result = i2s_read(
        I2S_PORT, raw, sizeof(raw), &bytesRead, 100 / portTICK_PERIOD_MS);

    if (result != ESP_OK || bytesRead == 0) {
      if (millis() - started > RECORD_SECONDS * 1000UL + 3000) {
        uint8_t silence[256] = {};
        while (sent < wavDataLength) {
          size_t remaining = wavDataLength - sent;
          size_t count = remaining < sizeof(silence) ? remaining : sizeof(silence);
          if (!sendChunk(client, silence, count)) {
            break;
          }
          sent += count;
        }
        break;
      }
      continue;
    }

    size_t count = bytesRead / sizeof(int32_t);
    int16_t pcm[256];
    for (size_t i = 0; i < count; i++) {
      int32_t value = raw[i] >> 16;
      if (value > 32767) {
        value = 32767;
      } else if (value < -32768) {
        value = -32768;
      }
      pcm[i] = static_cast<int16_t>(value);
    }

    size_t bytes = count * 2;
    if (sent + bytes > wavDataLength) {
      bytes = wavDataLength - sent;
    }
    if (!sendChunk(client, reinterpret_cast<uint8_t *>(pcm), bytes)) {
      client.stop();
      showReply("sad", "Send error");
      return false;
    }
    sent += bytes;

    if (millis() - lastDisplay > 1000) {
      lastDisplay = millis();
      uint32_t elapsed = (millis() - started) / 1000;
      showLines("Recording", String(elapsed) + " / " + String(RECORD_SECONDS) + " s");
    }
  }

  client.print("0\r\n\r\n");
  showLines("Uploading", "Processing");

  int status = 0;
  String response = readHttpResponse(client, status);
  client.stop();

  Serial.printf("POST /api/note -> %d\n%s\n", status, response.c_str());

  JsonDocument doc;
  if (deserializeJson(doc, httpBody(response))) {
    showReply("sad", "Bad reply");
    return false;
  }

  const char *state = doc["status"] | "";
  const char *face = doc["face"] | "sad";
  const char *text = doc["display_text"] | "No reply";
  showReply(face, text);

  if (strcmp(state, "ok") != 0) {
    return false;
  }

  const char *id = doc["id"] | "";
  noteId = id;
  return true;
}

bool pollStatusOnce(const String &noteId) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  HTTPClient http;
  http.begin(String(SERVER_URL) + "/api/notes/" + noteId + "/status");
  http.setTimeout(15000);
  http.addHeader("X-API-Key", API_KEY);

  int status = http.GET();
  String body = http.getString();
  http.end();

  if (status != 200) {
    showLines("Status error", String(status));
    return true;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }

  const char *state = doc["state"] | "";
  const char *face = doc["face"] | "wait";
  const char *text = doc["display_text"] | "Processing";
  const char *title = doc["title"] | "";

  if (strcmp(state, "completed") == 0 && title[0] != '\0') {
    showLines("Done", title);
  } else {
    showReply(face, text);
  }

  return strcmp(state, "processing") != 0;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Voice Note Client ===");
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

  Wire.begin(OLED_SDA, OLED_SCL);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (displayReady) {
    display.clearDisplay();
    display.display();
  }
  showLines("Voice Note", "Starting...");

  if (!setupI2S()) {
    Serial.println("I2S setup failed");
    showLines("I2S error", "Check wiring");
    while (true) {
      delay(1000);
    }
  }

  connectWiFi();
  showLines("Ready", "Use phone to record");
}

void loop() {
  static String lastNoteId;
  static uint32_t nextCommandPoll = 0;
  static uint32_t nextStatusPoll = 0;

  if (millis() >= nextCommandPoll) {
    nextCommandPoll = millis() + COMMAND_POLL_MS;
    String commandId;
    if (fetchCommand(commandId)) {
      String noteId;
      if (streamNote(commandId, noteId) && noteId.length() > 0) {
        lastNoteId = noteId;
        nextStatusPoll = millis() + STATUS_POLL_MS;
      }
    }
  }

  if (lastNoteId.length() > 0 && millis() >= nextStatusPoll) {
    nextStatusPoll = millis() + STATUS_POLL_MS;
    if (pollStatusOnce(lastNoteId)) {
      lastNoteId = "";
    }
  }

  delay(100);
}
