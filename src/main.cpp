#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Eye geometry constants
const int EYE_WIDTH = 32;
const int EYE_HEIGHT = 38;
const int EYE_RADIUS = 8;
const int LEFT_EYE_X = 36;
const int RIGHT_EYE_X = 92;
const int EYE_Y = 32;

// Available Expressions
enum Emotion {
  EMOTION_NORMAL,
  EMOTION_HAPPY,
  EMOTION_SURPRISED,
  EMOTION_ANGRY,
  EMOTION_SAD,
  EMOTION_WINK,
  EMOTION_SLEEPY,
  EMOTION_LOVE,
  EMOTION_SQUINT,
  EMOTION_COUNT
};

Emotion currentEmotion = EMOTION_NORMAL;
bool autoMode = true;
unsigned long lastEmotionChange = 0;
unsigned long emotionDuration = 3500;

// Dynamic eye animation states
int eyeOffsetX = 0;
int eyeOffsetY = 0;
int targetOffsetX = 0;
int targetOffsetY = 0;
int eyeHeightFactor = 100; // 0 to 100 (%)
bool isBlinking = false;
unsigned long nextBlinkTime = 0;
int sleepZIndex = 0;
unsigned long lastZAnimTime = 0;

// Helper to draw a single eye based on shape & emotion
void drawEye(int cx, int cy, int w, int h, int r, Emotion emotion, bool isLeft) {
  if (h <= 2) {
    // Completely closed / flat line blink
    display.fillRoundRect(cx - w / 2, cy - 1, w, 3, 1, SSD1306_WHITE);
    return;
  }

  int x0 = cx - w / 2;
  int y0 = cy - h / 2;

  switch (emotion) {
    case EMOTION_HAPPY: {
      // Inverted crescent / happy squint (arc-like shape)
      display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      // Cut off bottom curved part to form happy upward smile eyes
      display.fillCircle(cx, cy + h / 2 + 2, w / 2 + 2, SSD1306_BLACK);
      // Cute blush dots
      int blushX = isLeft ? (cx - 8) : (cx + 8);
      display.fillCircle(blushX, cy + h / 2 + 4, 2, SSD1306_WHITE);
      break;
    }

    case EMOTION_SURPRISED: {
      // Big wide circular eyes with a hollow glowing center
      display.fillRoundRect(x0 - 2, y0 - 2, w + 4, h + 4, 12, SSD1306_WHITE);
      display.fillCircle(cx, cy, 5, SSD1306_BLACK);
      break;
    }

    case EMOTION_ANGRY: {
      // Standard eye with aggressive diagonal cut from top
      display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      if (isLeft) {
        // Cut top-right down to top-left
        display.fillTriangle(x0 - 1, y0 - 1, x0 + w + 1, y0 - 1, x0 + w + 1, y0 + h / 2 + 2, SSD1306_BLACK);
        // Angled brow
        display.drawLine(x0 - 2, y0 + 1, x0 + w + 2, y0 + h / 2 + 4, SSD1306_WHITE);
        display.drawLine(x0 - 2, y0 + 2, x0 + w + 2, y0 + h / 2 + 5, SSD1306_WHITE);
      } else {
        // Cut top-left down to top-right
        display.fillTriangle(x0 - 1, y0 - 1, x0 + w + 1, y0 - 1, x0 - 1, y0 + h / 2 + 2, SSD1306_BLACK);
        // Angled brow
        display.drawLine(x0 - 2, y0 + h / 2 + 4, x0 + w + 2, y0 + 1, SSD1306_WHITE);
        display.drawLine(x0 - 2, y0 + h / 2 + 5, x0 + w + 2, y0 + 2, SSD1306_WHITE);
      }
      break;
    }

    case EMOTION_SAD: {
      // Eyeballs drooped outward
      display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      if (isLeft) {
        display.fillTriangle(x0 - 1, y0 - 1, x0 + w + 1, y0 - 1, x0 - 1, y0 + h / 2 + 2, SSD1306_BLACK);
      } else {
        display.fillTriangle(x0 - 1, y0 - 1, x0 + w + 1, y0 - 1, x0 + w + 1, y0 + h / 2 + 2, SSD1306_BLACK);
      }
      break;
    }

    case EMOTION_WINK: {
      if (isLeft) {
        // Left eye winks (smiling line)
        display.drawLine(x0, cy + 2, cx, cy - 4, SSD1306_WHITE);
        display.drawLine(cx, cy - 4, x0 + w, cy + 2, SSD1306_WHITE);
        display.drawLine(x0, cy + 3, cx, cy - 3, SSD1306_WHITE);
        display.drawLine(cx, cy - 3, x0 + w, cy + 3, SSD1306_WHITE);
      } else {
        // Right eye wide open
        display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      }
      break;
    }

    case EMOTION_SLEEPY: {
      // Narrow peaceful slit
      display.fillRoundRect(x0, cy - 2, w, 4, 2, SSD1306_WHITE);
      break;
    }

    case EMOTION_LOVE: {
      // Heart eyes
      int hx = cx;
      int hy = cy - 2;
      display.fillCircle(hx - 6, hy - 4, 7, SSD1306_WHITE);
      display.fillCircle(hx + 6, hy - 4, 7, SSD1306_WHITE);
      display.fillTriangle(hx - 13, hy - 2, hx + 13, hy - 2, hx, hy + 13, SSD1306_WHITE);
      break;
    }

    case EMOTION_SQUINT: {
      // Narrowed focused rectangular slit
      display.fillRoundRect(x0, cy - 4, w, 9, 3, SSD1306_WHITE);
      break;
    }

    case EMOTION_NORMAL:
    default: {
      // Classic cute rounded robot eye
      display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      // Shiny reflection dot in the top corner
      display.fillCircle(x0 + 6, y0 + 6, 2, SSD1306_BLACK);
      break;
    }
  }
}

// Render extra elements (mouth / floating Zzz / antenna signals)
void drawExtraFeatures(Emotion emotion) {
  switch (emotion) {
    case EMOTION_HAPPY:
      // Cute small smile mouth
      display.drawCircle(64, 52, 6, SSD1306_WHITE);
      display.fillRect(57, 46, 14, 6, SSD1306_BLACK);
      break;

    case EMOTION_SURPRISED:
      // Small "o" mouth
      display.drawCircle(64, 55, 3, SSD1306_WHITE);
      break;

    case EMOTION_LOVE:
      // Small happy smile mouth
      display.drawCircle(64, 54, 5, SSD1306_WHITE);
      display.fillRect(58, 48, 12, 6, SSD1306_BLACK);
      break;

    case EMOTION_SLEEPY: {
      // Animated floating Zzz
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      if (sleepZIndex >= 0) {
        display.setCursor(102, 18);
        display.print(F("z"));
      }
      if (sleepZIndex >= 1) {
        display.setCursor(110, 11);
        display.print(F("Z"));
      }
      if (sleepZIndex >= 2) {
        display.setCursor(118, 4);
        display.print(F("Z"));
      }
      break;
    }

    default:
      break;
  }
}

void updateBlinkAnimation() {
  unsigned long now = millis();

  // Handle blink triggers
  if (!isBlinking && currentEmotion != EMOTION_SLEEPY && currentEmotion != EMOTION_LOVE) {
    if (now >= nextBlinkTime) {
      isBlinking = true;
    }
  }

  if (isBlinking) {
    // Quick blink down then up
    static int blinkPhase = 0; // 0: closing, 1: opening
    if (blinkPhase == 0) {
      eyeHeightFactor -= 25;
      if (eyeHeightFactor <= 5) {
        eyeHeightFactor = 5;
        blinkPhase = 1;
      }
    } else {
      eyeHeightFactor += 25;
      if (eyeHeightFactor >= 100) {
        eyeHeightFactor = 100;
        blinkPhase = 0;
        isBlinking = false;
        // Next blink in 2.5 to 5.5 seconds (random organic feel)
        nextBlinkTime = now + random(2500, 5500);
      }
    }
  } else {
    eyeHeightFactor = 100;
  }
}

void updateGazeMovement() {
  // Smooth gaze interpolation
  if (eyeOffsetX < targetOffsetX) eyeOffsetX++;
  else if (eyeOffsetX > targetOffsetX) eyeOffsetX--;

  if (eyeOffsetY < targetOffsetY) eyeOffsetY++;
  else if (eyeOffsetY > targetOffsetY) eyeOffsetY--;

  // Periodically pick a new looking direction in Normal / Squint mode
  if (currentEmotion == EMOTION_NORMAL || currentEmotion == EMOTION_SQUINT) {
    static unsigned long nextGazeShift = 0;
    if (millis() > nextGazeShift) {
      int dir = random(0, 6);
      switch (dir) {
        case 0: targetOffsetX = 0;  targetOffsetY = 0;  break; // Center
        case 1: targetOffsetX = -6; targetOffsetY = 0;  break; // Left
        case 2: targetOffsetX = 6;  targetOffsetY = 0;  break; // Right
        case 3: targetOffsetX = 0;  targetOffsetY = -3; break; // Up
        case 4: targetOffsetX = 0;  targetOffsetY = 3;  break; // Down
        default: targetOffsetX = 0; targetOffsetY = 0; break;
      }
      nextGazeShift = millis() + random(1200, 3000);
    }
  } else {
    targetOffsetX = 0;
    targetOffsetY = 0;
  }
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd <= 32) continue; // Ignore whitespace, CR, LF, etc.

    switch (cmd) {
      case 'n': currentEmotion = EMOTION_NORMAL; autoMode = false; break;
      case 'h': currentEmotion = EMOTION_HAPPY; autoMode = false; break;
      case 's': currentEmotion = EMOTION_SURPRISED; autoMode = false; break;
      case 'a': currentEmotion = EMOTION_ANGRY; autoMode = false; break;
      case 'x': currentEmotion = EMOTION_SAD; autoMode = false; break;
      case 'w': currentEmotion = EMOTION_WINK; autoMode = false; break;
      case 'z': currentEmotion = EMOTION_SLEEPY; autoMode = false; break;
      case 'l': currentEmotion = EMOTION_LOVE; autoMode = false; break;
      case 'c': currentEmotion = EMOTION_SQUINT; autoMode = false; break;
      case 'r':
        autoMode = !autoMode;
        Serial.print(F("[Mode] Auto cycle: "));
        Serial.println(autoMode ? F("ON") : F("OFF"));
        return;
      default:
        Serial.print(F("Unknown cmd: '"));
        Serial.print(cmd);
        Serial.println(F("' | Available: [n]ormal, [h]appy, [s]urprised, [a]ngry, [x]sad, [w]ink, [z]sleepy, [l]ove, s[c]uint, [r]toggle auto"));
        return;
    }
    Serial.print(F("[Emotion] Changed to: "));
    switch(currentEmotion) {
      case EMOTION_NORMAL: Serial.println(F("NORMAL")); break;
      case EMOTION_HAPPY: Serial.println(F("HAPPY")); break;
      case EMOTION_SURPRISED: Serial.println(F("SURPRISED")); break;
      case EMOTION_ANGRY: Serial.println(F("ANGRY")); break;
      case EMOTION_SAD: Serial.println(F("SAD")); break;
      case EMOTION_WINK: Serial.println(F("WINK")); break;
      case EMOTION_SLEEPY: Serial.println(F("SLEEPY")); break;
      case EMOTION_LOVE: Serial.println(F("LOVE")); break;
      case EMOTION_SQUINT: Serial.println(F("SQUINT")); break;
      default: break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n======================================="));
  Serial.println(F("   STM32 Expressive Robot Face OLED   "));
  Serial.println(F("======================================="));
  Serial.println(F("Serial Commands:"));
  Serial.println(F(" [n] Normal    [h] Happy     [s] Surprised"));
  Serial.println(F(" [a] Angry     [x] Sad       [w] Wink"));
  Serial.println(F(" [z] Sleepy    [l] Love      [c] Squint"));
  Serial.println(F(" [r] Toggle Auto cycle"));
  Serial.println(F("======================================="));

  Wire.begin();
  Wire.setClock(400000); // 400kHz Fast I2C for fluid 60fps animations

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed!"));
    for (;;);
  }

  display.clearDisplay();
  display.display();
  nextBlinkTime = millis() + 2000;
  lastEmotionChange = millis();
}

void loop() {
  handleSerialCommands();

  unsigned long now = millis();

  // Automatic emotion cycling when autoMode is active
  if (autoMode && (now - lastEmotionChange > emotionDuration)) {
    // Sequence through expressions
    currentEmotion = (Emotion)((currentEmotion + 1) % EMOTION_COUNT);
    lastEmotionChange = now;

    // Vary emotion durations
    if (currentEmotion == EMOTION_SLEEPY) emotionDuration = 5000;
    else if (currentEmotion == EMOTION_NORMAL) emotionDuration = 4500;
    else emotionDuration = 3000;
  }

  // Animation updates
  updateBlinkAnimation();
  updateGazeMovement();

  // Sleep Zzz animation update
  if (currentEmotion == EMOTION_SLEEPY && (now - lastZAnimTime > 400)) {
    sleepZIndex = (sleepZIndex + 1) % 4;
    lastZAnimTime = now;
  }

  // Clear buffer
  display.clearDisplay();

  // Calculate dynamic dimensions
  int curH = (EYE_HEIGHT * eyeHeightFactor) / 100;
  int curRadius = (EYE_RADIUS * eyeHeightFactor) / 100;
  if (curRadius < 1) curRadius = 1;

  // Draw Left and Right eyes
  drawEye(LEFT_EYE_X + eyeOffsetX, EYE_Y + eyeOffsetY, EYE_WIDTH, curH, curRadius, currentEmotion, true);
  drawEye(RIGHT_EYE_X + eyeOffsetX, EYE_Y + eyeOffsetY, EYE_WIDTH, curH, curRadius, currentEmotion, false);

  // Draw accessories / mouth / sleep indicators
  drawExtraFeatures(currentEmotion);

  // Render to OLED screen
  display.display();

  // Smooth frame timing (~40 FPS)
  delay(25);
}
