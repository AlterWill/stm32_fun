#pragma once

#include "../../common/hmi_protocol.h"

namespace hmi_link {

// Starts UART2 on GPIO16 (RX) and GPIO18 (TX) at 115200 8N1.
void begin();

// Publishes a state change. Repeated calls with the same state and detail are
// suppressed, so callers may call this from a polling loop without flooding
// the UART or retriggering the Blue Pill's buzzer.
void setState(hmi::State state, const char *detail = nullptr);

// Resends the current state every few seconds. This lets a Blue Pill that was
// reset (or powered up later) resynchronise without an explicit handshake.
void loop();

}  // namespace hmi_link
