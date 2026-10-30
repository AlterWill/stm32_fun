#include "network.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstring>

#include "secrets.h"

namespace {
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint32_t kHttpTimeoutMs = 15000;
constexpr size_t kMaxResponseBytes = 8192;

bool writeAll(WiFiClient &client, const uint8_t *data, size_t length) {
  while (length > 0) {
    size_t written = client.write(data, length);
    if (written == 0) return false;
    data += written;
    length -= written;
  }
  return true;
}

bool sendChunk(WiFiClient &client, const uint8_t *data, size_t length) {
  if (length == 0) return true;

  char header[16];
  int headerLength = snprintf(header, sizeof(header), "%x\r\n",
                              static_cast<unsigned int>(length));
  if (headerLength <= 0) return false;

  return writeAll(client, reinterpret_cast<const uint8_t *>(header),
                  static_cast<size_t>(headerLength)) &&
         writeAll(client, data, length) &&
         writeAll(client, reinterpret_cast<const uint8_t *>("\r\n"), 2);
}

void put16(uint8_t *destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t *destination, uint32_t value) {
  put16(destination, static_cast<uint16_t>(value));
  put16(destination + 2, static_cast<uint16_t>(value >> 16));
}

void writeWavHeader(uint8_t *header, uint32_t dataBytes) {
  memcpy(header, "RIFF", 4);
  put32(header + 4, dataBytes + 36);
  memcpy(header + 8, "WAVEfmt ", 8);
  put32(header + 16, 16);        // fmt chunk size
  put16(header + 20, 1);         // PCM
  put16(header + 22, 1);         // mono
  put32(header + 24, 16000);     // sample rate
  put32(header + 28, 32000);     // byte rate (16000 * 2)
  put16(header + 32, 2);         // block align
  put16(header + 34, 16);        // bits per sample
  memcpy(header + 36, "data", 4);
  put32(header + 40, dataBytes);
}

bool parseServerUrl(String &host, uint16_t &port) {
  String url = SERVER_URL;
  if (!url.startsWith("http://")) return false;

  url.remove(0, 7);
  if (url.length() == 0 || url.indexOf('/') >= 0) return false;

  int colon = url.lastIndexOf(':');
  host = colon < 0 ? url : url.substring(0, colon);
  long parsedPort = colon < 0 ? 80 : url.substring(colon + 1).toInt();
  if (host.length() == 0 || parsedPort < 1 || parsedPort > 65535) return false;

  port = static_cast<uint16_t>(parsedPort);
  return true;
}

int findContentLength(const String &headers) {
  const char *needles[] = {"Content-Length:", "content-length:"};
  for (const char *needle : needles) {
    int position = headers.indexOf(needle);
    if (position < 0) continue;

    position += strlen(needle);
    while (position < static_cast<int>(headers.length()) &&
           headers[position] == ' ') {
      ++position;
    }
    return headers.substring(position).toInt();
  }
  return -1;
}

bool readHttpResponse(WiFiClient &client, int &statusCode, String &body) {
  String response;
  response.reserve(2048);
  statusCode = 0;

  uint32_t started = millis();
  while (millis() - started < kHttpTimeoutMs) {
    while (client.available()) {
      response += static_cast<char>(client.read());
      if (response.length() >= kMaxResponseBytes) break;
    }
    if (response.length() >= kMaxResponseBytes) break;

    int headerEnd = response.indexOf("\r\n\r\n");
    if (headerEnd >= 0) {
      int contentLength = findContentLength(response.substring(0, headerEnd));
      if (contentLength >= 0) {
        size_t expected = static_cast<size_t>(headerEnd) + 4 +
                          static_cast<size_t>(contentLength);
        if (response.length() >= expected) break;
      }
    }

    if (!client.connected() && !client.available()) break;
    delay(5);
  }

  if (!response.startsWith("HTTP/1.")) return false;

  int firstSpace = response.indexOf(' ');
  if (firstSpace < 0) return false;
  statusCode = response.substring(firstSpace + 1).toInt();

  int headerEnd = response.indexOf("\r\n\r\n");
  body = headerEnd < 0 ? "" : response.substring(headerEnd + 4);
  return true;
}
void printWifiScan() {
  int count = WiFi.scanNetworks();
  Serial.printf("Wi-Fi scan: %d network(s)\n", count);
  for (int i = 0; i < count; ++i) {
    Serial.printf("  %s  %d dBm  %s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
  }
  WiFi.scanDelete();
}

}  // namespace

bool network::connect() {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kWifiTimeoutMs) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection failed");
    printWifiScan();
    return false;
  }

  Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

String network::localIp() { return WiFi.localIP().toString(); }

network::PollResult network::pollCommand(String &commandId, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "Wi-Fi lost";
    return PollResult::NoWifi;
  }

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(String(SERVER_URL) + "/api/device/command");
  http.addHeader("X-API-Key", API_KEY);

  int statusCode = http.GET();
  String responseBody = http.getString();
  http.end();

  if (statusCode != 200) {
    error = "Server HTTP " + String(statusCode);
    return PollResult::ServerError;
  }

  JsonDocument document;
  if (deserializeJson(document, responseBody)) {
    error = "Bad command reply";
    return PollResult::BadReply;
  }

  const char *command = document["command"] | "";
  const char *id = document["id"] | "";
  if (strcmp(command, "record") == 0 && id[0] != '\0') {
    commandId = id;
    return PollResult::Command;
  }
  if (command[0] == '\0') return PollResult::Idle;

  error = "Unknown command";
  return PollResult::BadReply;
}

bool network::NoteUpload::begin(const String &commandId, uint32_t wavDataBytes,
                                String &error) {
  abort();

  String host;
  uint16_t port = 0;
  if (!parseServerUrl(host, port)) {
    error = "Invalid SERVER_URL";
    return false;
  }

  client_.setTimeout(kHttpTimeoutMs);
  client_.setNoDelay(true);
  if (!client_.connect(host.c_str(), port)) {
    error = "Server unreachable";
    return false;
  }
  active_ = true;

  client_.print("POST /api/note HTTP/1.1\r\n");
  client_.print("Host: " + host + ":" + String(port) + "\r\n");
  client_.print("X-API-Key: " API_KEY "\r\n");
  client_.print("X-Command-Id: " + commandId + "\r\n");
  client_.print("Content-Type: audio/wav\r\n");
  client_.print("Transfer-Encoding: chunked\r\n");
  client_.print("Connection: close\r\n\r\n");

  uint8_t header[44];
  writeWavHeader(header, wavDataBytes);
  if (!sendChunk(client_, header, sizeof(header))) {
    error = "Server connection lost";
    abort();
    return false;
  }

  expectedBytes_ = wavDataBytes;
  sentBytes_ = 0;
  return true;
}

bool network::NoteUpload::write(const uint8_t *data, size_t length,
                                String &error) {
  if (!active_) {
    error = "Upload not open";
    return false;
  }

  if (length > expectedBytes_ - sentBytes_) {
    length = expectedBytes_ - sentBytes_;
  }
  if (!sendChunk(client_, data, length)) {
    error = "Server connection lost";
    abort();
    return false;
  }
  sentBytes_ += length;
  return true;
}

bool network::NoteUpload::finish(String &displayText, String &error) {
  if (!active_) {
    error = "Upload not open";
    return false;
  }
  if (sentBytes_ != expectedBytes_) {
    error = "Audio length mismatch";
    abort();
    return false;
  }

  if (!client_.print("0\r\n\r\n")) {
    error = "Server connection lost";
    abort();
    return false;
  }

  int statusCode = 0;
  String responseBody;
  bool responseOk = readHttpResponse(client_, statusCode, responseBody);
  client_.stop();
  active_ = false;

  if (!responseOk) {
    error = "No server response";
    return false;
  }

  JsonDocument document;
  DeserializationError jsonError = deserializeJson(document, responseBody);

  if (statusCode != 200 || jsonError) {
    if (!jsonError) {
      const char *serverText = document["display_text"] | "";
      if (serverText[0] != '\0') {
        error = serverText;
      } else {
        error = "Server HTTP " + String(statusCode);
      }
    } else {
      error = "Bad server reply";
    }
    return false;
  }

  if (strcmp(document["status"] | "", "ok") != 0) {
    const char *serverText = document["display_text"] | "Server error";
    error = serverText;
    return false;
  }

  displayText = document["display_text"] | "Recorded";
  return true;
}

void network::NoteUpload::abort() {
  client_.stop();
  active_ = false;
  expectedBytes_ = 0;
  sentBytes_ = 0;
}
