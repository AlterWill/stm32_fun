#include "display.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {
Adafruit_SSD1306 screen(128, 64, &Wire, -1);
bool available = false;
}

bool display::begin() {
  Wire.setSDA(PB9);
  Wire.setSCL(PB8);
  Wire.begin();
  Serial.println("Scanning display I2C bus (SDA PB9/D14, SCL PB8/D15)...");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");
      if (address < 16) Serial.print('0');
      Serial.println(address, HEX);
    }
  }
  Serial.println("I2C scan finished; display firmware expects 0x3C");
  available = screen.begin(SSD1306_SWITCHCAPVCC, 0x3c);
  if (!available) Serial.println("Display initialization failed");
  return available;
}

void display::show(const char *title, const char *message) {
  Serial.print(title);
  Serial.print(" | ");
  Serial.println(message);
  if (!available) return;
  screen.clearDisplay();
  screen.setTextSize(1);
  screen.setTextColor(SSD1306_WHITE);
  screen.setCursor(0, 0);
  screen.println(title);
  screen.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  screen.setCursor(0, 14);
  screen.println(message);
  screen.display();
}
