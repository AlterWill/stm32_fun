#pragma once
#include <Arduino.h>

namespace network {
bool connect();
bool command(String &id);
bool upload(const String &commandId, uint32_t seconds);
}
