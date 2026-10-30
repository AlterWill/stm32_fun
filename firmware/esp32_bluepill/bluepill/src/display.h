#pragma once

namespace display {

// Initialises I2C1 and probes PB7/PB6 then PB9/PB8 at 0x3C or 0x3D.
bool begin();

// Draws a two-section screen: state title on top, optional detail below.
void show(const char *title, const char *detail);

}  // namespace display
