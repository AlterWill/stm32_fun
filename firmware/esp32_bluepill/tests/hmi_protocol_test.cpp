#include <cassert>
#include <cstring>
#include <initializer_list>

#include "hmi_protocol.h"

int main() {
  for (auto state : {hmi::State::Boot, hmi::State::Wifi, hmi::State::Ready,
                     hmi::State::Recording, hmi::State::Uploading,
                     hmi::State::Completed, hmi::State::Error}) {
    const char *token = hmi::stateToken(state);
    assert(hmi::stateFromToken(token, std::strlen(token)) == state);
  }

  assert(hmi::stateFromToken("BOGUS", 5) == hmi::State::Unknown);
  assert(hmi::stateFromToken("READY", 5) == hmi::State::Ready);
  assert(hmi::stateFromToken("READ", 4) == hmi::State::Unknown);

  return 0;
}
