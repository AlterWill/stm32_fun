#pragma once

#include <stddef.h>
#include <stdint.h>

// All integer fields are little-endian. Payload length is bounded so a damaged
// header cannot cause an unbounded allocation on either microcontroller.
namespace voice_link {
constexpr uint8_t MAGIC0 = 0x56, MAGIC1 = 0x4e, VERSION = 1;
constexpr size_t MAX_PAYLOAD = 512;
enum Type : uint8_t { HELLO = 1, START = 2, AUDIO = 3, END = 4, ABORT = 5, STATUS = 6 };

inline uint16_t crc16(const uint8_t *data, size_t n, uint16_t crc = 0xffff) {
  while (n--) {
    crc ^= uint16_t(*data++) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}

struct Frame {
  uint8_t type;
  uint32_t sequence;
  uint16_t length;
  uint8_t payload[MAX_PAYLOAD];
};

// Header: magic[2], version, type, sequence[4], length[2], then payload
// and CRC-16/CCITT-FALSE over version through payload, low byte first.
template <typename Stream> void send(Stream &stream, const Frame &frame) {
  if (frame.length > MAX_PAYLOAD) return;
  uint8_t h[10] = {MAGIC0, MAGIC1, VERSION, frame.type,
    uint8_t(frame.sequence), uint8_t(frame.sequence >> 8),
    uint8_t(frame.sequence >> 16), uint8_t(frame.sequence >> 24),
    uint8_t(frame.length), uint8_t(frame.length >> 8)};
  uint16_t crc = crc16(h + 2, 8);
  crc = crc16(frame.payload, frame.length, crc);
  stream.write(h, sizeof(h));
  stream.write(frame.payload, frame.length);
  uint8_t tail[2] = {uint8_t(crc), uint8_t(crc >> 8)};
  stream.write(tail, sizeof(tail));
}

class Parser {
 public:
  // Returns true only for a complete valid frame. A malformed frame is
  // discarded; the caller detects missing audio via the sequence number.
  bool push(uint8_t b, Frame &out) {
    if (pos_ == 0) { if (b == MAGIC0) bytes_[pos_++] = b; return false; }
    if (pos_ == 1 && b != MAGIC1) { pos_ = b == MAGIC0 ? 1 : 0; return false; }
    bytes_[pos_++] = b;
    if (pos_ == 10) {
      length_ = uint16_t(bytes_[8]) | uint16_t(bytes_[9]) << 8;
      if (bytes_[2] != VERSION || length_ > MAX_PAYLOAD) { reset(); return false; }
    }
    if (pos_ < 10 || pos_ != size_t(12 + length_)) return false;
    uint16_t expected = uint16_t(bytes_[10 + length_]) | uint16_t(bytes_[11 + length_]) << 8;
    bool valid = crc16(bytes_ + 2, 8 + length_) == expected;
    if (valid) {
      out.type = bytes_[3];
      out.sequence = uint32_t(bytes_[4]) | uint32_t(bytes_[5]) << 8 |
                     uint32_t(bytes_[6]) << 16 | uint32_t(bytes_[7]) << 24;
      out.length = length_;
      for (size_t i = 0; i < length_; ++i) out.payload[i] = bytes_[10 + i];
    }
    reset();
    return valid;
  }
  void reset() { pos_ = 0; length_ = 0; }
 private:
  uint8_t bytes_[12 + MAX_PAYLOAD] = {};
  size_t pos_ = 0;
  uint16_t length_ = 0;
};
}
