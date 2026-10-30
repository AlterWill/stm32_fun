#include "network.h"
#include "secrets.h"
#include "../../common/protocol.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

extern HardwareSerial bridge;
namespace {
voice_link::Parser parser;
voice_link::Frame frame;

void status(const char *message, uint8_t type = voice_link::STATUS) {
  voice_link::Frame f{}; f.type = type;
  f.length = min(strlen(message), voice_link::MAX_PAYLOAD);
  memcpy(f.payload, message, f.length); voice_link::send(bridge, f);
  Serial.println(message);
}

bool nextFrame(uint32_t timeout) {
  uint32_t start = millis();
  while (millis() - start < timeout) {
    while (bridge.available()) if (parser.push(bridge.read(), frame)) return true;
    delay(1);
  }
  return false;
}

bool writeAll(WiFiClient &client, const uint8_t *p, size_t n) {
  while (n) { size_t sent = client.write(p, n); if (!sent) return false; p += sent; n -= sent; }
  return true;
}
bool chunk(WiFiClient &client, const uint8_t *p, size_t n) {
  char h[16]; int size = snprintf(h, sizeof(h), "%x\r\n", unsigned(n));
  return writeAll(client, reinterpret_cast<uint8_t *>(h), size) && writeAll(client, p, n) &&
         writeAll(client, reinterpret_cast<const uint8_t *>("\r\n"), 2);
}
void put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
void put32(uint8_t *p, uint32_t n) { put16(p, n); put16(p + 2, n >> 16); }
void wavHeader(uint8_t *p, uint32_t n) {
  memcpy(p, "RIFF", 4); put32(p + 4, n + 36); memcpy(p + 8, "WAVEfmt ", 8);
  put32(p + 16, 16); put16(p + 20, 1); put16(p + 22, 1); put32(p + 24, 16000);
  put32(p + 28, 32000); put16(p + 32, 2); put16(p + 34, 16);
  memcpy(p + 36, "data", 4); put32(p + 40, n);
}
bool server(String &host, uint16_t &port) {
  String url = SERVER_URL;
  if (!url.startsWith("http://")) return false;
  url.remove(0, 7);
  if (url.indexOf('/') >= 0) return false;
  int colon = url.lastIndexOf(':');
  host = colon < 0 ? url : url.substring(0, colon);
  long parsed = colon < 0 ? 80 : url.substring(colon + 1).toInt();
  if (!host.length() || parsed < 1 || parsed > 65535) return false;
  port = parsed; return true;
}
}

bool network::connect() {
  if (WiFi.status() == WL_CONNECTED) return true;
  status("Connecting Wi-Fi");
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  if (WiFi.status() != WL_CONNECTED) { status("Wi-Fi failed"); return false; }
  status("Ready - use phone"); return true;
}

bool network::command(String &id) {
  if (!connect()) return false;
  HTTPClient http; http.begin(String(SERVER_URL) + "/api/device/command");
  http.setTimeout(15000); http.addHeader("X-API-Key", API_KEY);
  int code = http.GET(); String body = http.getString(); http.end();
  if (code != 200) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  const char *kind = doc["command"] | "";
  const char *value = doc["id"] | "";
  if (strcmp(kind, "record") || !*value) return false;
  id = value; return true;
}

bool network::upload(const String &id, uint32_t seconds) {
  String host; uint16_t port;
  if (!server(host, port)) { status("Invalid HTTP URL"); return false; }
  WiFiClient client; client.setTimeout(5000); client.setNoDelay(true);
  if (!client.connect(host.c_str(), port)) { status("Server unavailable"); return false; }
  client.print("POST /api/note HTTP/1.1\r\nHost: " + host + ":" + String(port) + "\r\n");
  client.print("X-API-Key: " API_KEY "\r\nX-Command-Id: " + id + "\r\n");
  client.print("Content-Type: audio/wav\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
  uint32_t length = seconds * 32000UL;
  uint8_t header[44]; wavHeader(header, length);
  if (!chunk(client, header, sizeof(header))) { client.stop(); status("Upload failed"); return false; }
  while (bridge.available()) bridge.read();
  parser.reset();
  voice_link::Frame start{}; start.type = voice_link::START; start.length = 4;
  put32(start.payload, seconds); voice_link::send(bridge, start);
  if (!nextFrame(5000) || frame.type != voice_link::HELLO) {
    client.stop(); status("STM32 not ready", voice_link::ABORT); return false;
  }
  uint32_t expected = 0, received = 0;
  bool ok = true;
  while (received < length) {
    if (!nextFrame(3000) || frame.type != voice_link::AUDIO || frame.sequence != expected ||
        frame.length == 0 || (frame.length & 1) || frame.length > length - received ||
        !chunk(client, frame.payload, frame.length)) { ok = false; break; }
    received += frame.length; ++expected;
  }
  if (ok) ok = nextFrame(3000) && frame.type == voice_link::END && frame.sequence == expected;
  if (!ok) { client.stop(); status("Recording failed", voice_link::ABORT); return false; }
  client.print("0\r\n\r\n");
  String response; uint32_t started = millis();
  while (millis() - started < 15000 && (client.connected() || client.available())) {
    while (client.available()) { response += char(client.read()); if (response.length() > 8192) break; }
    if (response.length() > 8192) break;
    delay(10);
  }
  client.stop();
  int body = response.indexOf("\r\n\r\n");
  JsonDocument doc;
  if (!response.startsWith("HTTP/1.1 200") || body < 0 || deserializeJson(doc, response.substring(body + 4)) ||
      strcmp(doc["status"] | "", "ok")) { status("Server error"); return false; }
  status(doc["display_text"] | "Recorded");
  return true;
}
