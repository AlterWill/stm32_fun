#include "display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr int kWidth = 128;
constexpr int kHeight = 64;
constexpr int kResetPin = -1;
constexpr uint8_t kAddresses[] = {0x3C, 0x3D};

Adafruit_SSD1306 screen(kWidth, kHeight, &Wire, kResetPin);
bool displayReady = false;
bool busStarted = false;

bool devicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool tryBus(int sda, int scl) {
  if (busStarted) Wire.end();
  Wire.setSDA(sda);
  Wire.setSCL(scl);
  Wire.begin();
  Wire.setClock(400000);
  busStarted = true;
  delay(20);

  for (uint8_t address : kAddresses) {
    if (!devicePresent(address)) continue;
    if (screen.begin(SSD1306_SWITCHCAPVCC, address)) {
      screen.clearDisplay();
      screen.display();
      return true;
    }
  }
  return false;
}
}  // namespace

bool display::begin() {
  // Support both common Blue Pill I2C1 wiring choices:
  //   PB7 SDA / PB6 SCL (default) and PB9 SDA / PB8 SCL (remap).
  // Most SSD1306 breakouts include pull-ups; add external 4.7 kOhm if needed.
  if (tryBus(PB7, PB6) || tryBus(PB9, PB8)) {
    displayReady = true;
    return true;
  }
  displayReady = false;
  return false;
}

void display::show(const char *title, const char *detail) {
  if (!displayReady) return;

  screen.clearDisplay();
  screen.setTextWrap(true);
  screen.setTextSize(1);
  screen.setTextColor(SSD1306_WHITE);

  screen.setCursor(0, 0);
  screen.print(title);
  screen.drawLine(0, 10, kWidth - 1, 10, SSD1306_WHITE);

  screen.setCursor(0, 16);
  screen.print(detail != nullptr ? detail : "");
  screen.display();
}
