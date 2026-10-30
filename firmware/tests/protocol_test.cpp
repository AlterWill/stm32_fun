#include "../common/protocol.h"
#include <cassert>
#include <cstring>
#include <vector>

struct Buffer {
  std::vector<uint8_t> bytes;
  size_t write(const uint8_t *p, size_t n) { bytes.insert(bytes.end(), p, p + n); return n; }
};

int main() {
  const char *check = "123456789";
  assert(voice_link::crc16(reinterpret_cast<const uint8_t *>(check), 9) == 0x29b1);
  voice_link::Frame source{};
  source.type = voice_link::AUDIO;
  source.sequence = 0x12345678;
  source.length = voice_link::MAX_PAYLOAD;
  for (size_t i = 0; i < source.length; ++i) source.payload[i] = uint8_t(i);
  Buffer wire;
  voice_link::send(wire, source);
  voice_link::Parser parser;
  voice_link::Frame received{};
  bool got = false;
  for (auto byte : wire.bytes) got |= parser.push(byte, received);
  assert(got && received.sequence == source.sequence && received.type == source.type);
  assert(received.length == source.length && !memcmp(received.payload, source.payload, source.length));
  wire.bytes[25] ^= 1;
  got = false;
  for (auto byte : wire.bytes) got |= parser.push(byte, received);
  assert(!got);
}
