#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

#ifndef USER_BTN
#define USER_BTN PC13
#endif

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==========================================
// OPERATING MODES
// ==========================================
enum SystemMode {
  MODE_ROBOT_PET,
  MODE_STOPWATCH
};

SystemMode currentMode = MODE_ROBOT_PET;

// ==========================================
// ROBOT PET EMOTIONS & GRAPHICS CONSTANTS
// ==========================================
const int EYE_WIDTH = 32;
const int EYE_HEIGHT = 38;
const int EYE_RADIUS = 8;
const int LEFT_EYE_X = 36;
const int RIGHT_EYE_X = 92;
const int EYE_Y = 32;

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

// Mode transition visual effect
bool isTransitioning = false;
unsigned long transitionStartTime = 0;
const unsigned long TRANSITION_DURATION = 300;

// ==========================================
// STOPWATCH STATE & METRICS
// ==========================================
enum StopwatchState {
  SW_RESET,
  SW_RUNNING,
  SW_PAUSED
};

StopwatchState swState = SW_RESET;
unsigned long swStartTime = 0;
unsigned long swElapsedTime = 0;
unsigned long swLastPausedTime = 0;

// Mini eye animation states for stopwatch header
int miniEyeOffsetX = 0;
int miniEyeOffsetY = 0;
int miniBlinkFactor = 100;
bool miniBlinking = false;
unsigned long nextMiniBlinkTime = 0;

// ==========================================
// BUTTON EVENT DETECTION (Single, Double, Long)
// ==========================================
enum ButtonEvent {
  BTN_NONE,
  BTN_SINGLE_CLICK,
  BTN_DOUBLE_CLICK,
  BTN_LONG_PRESS
};

const unsigned long DEBOUNCE_MS = 35;
const unsigned long LONG_PRESS_MS = 600;
const unsigned long DOUBLE_CLICK_GAP_MS = 280;

bool lastRawBtnState = HIGH;
bool debouncedBtnState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long btnPressStartTime = 0;
bool longPressTriggered = false;
int clickCount = 0;
unsigned long lastReleaseTime = 0;

ButtonEvent checkButton() {
  bool reading = digitalRead(USER_BTN);
  unsigned long now = millis();
  ButtonEvent event = BTN_NONE;

  if (reading != lastRawBtnState) {
    lastDebounceTime = now;
  }
  lastRawBtnState = reading;

  if ((now - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != debouncedBtnState) {
      debouncedBtnState = reading;

      // Button pressed down (Active LOW)
      if (debouncedBtnState == LOW) {
        btnPressStartTime = now;
        longPressTriggered = false;
      } 
      // Button released
      else {
        if (!longPressTriggered) {
          clickCount++;
          lastReleaseTime = now;
        }
      }
    }
  }

  // Check for Long Press while holding down
  if (debouncedBtnState == LOW && !longPressTriggered) {
    if (now - btnPressStartTime >= LONG_PRESS_MS) {
      longPressTriggered = true;
      clickCount = 0;
      event = BTN_LONG_PRESS;
    }
  }

  // Check for Single vs Double Click timeout
  if (debouncedBtnState == HIGH && clickCount > 0) {
    if (clickCount == 2) {
      clickCount = 0;
      event = BTN_DOUBLE_CLICK;
    } else if (now - lastReleaseTime > DOUBLE_CLICK_GAP_MS) {
      clickCount = 0;
      event = BTN_SINGLE_CLICK;
    }
  }

  return event;
}

// ==========================================
// ROBOT EYE RENDERING (FULL SCREEN PET)
// ==========================================
void drawEye(int cx, int cy, int w, int h, int r, Emotion emotion, bool isLeft) {
  if (h <= 2) {
    display.fillRoundRect(cx - w / 2, cy - 1, w, 3, 1, SSD1306_WHITE);
    return;
  }

  int x0 = cx - w / 2;
  int y0 = cy - h / 2;

  switch (emotion) {
    case EMOTION_HAPPY: {
      display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      display.fillCircle(cx, cy + h / 2 + 2, w / 2 + 2, SSD1306_BLACK);
      int blushX = isLeft ? (cx - 8) : (cx + 8);
      display.fillCircle(blushX, cy + h / 2 + 4, 2, SSD1306_WHITE);
      break;
    }

    case EMOTION_SURPRISED: {
      display.fillRoundRect(x0 - 2, y0 - 2, w + 4, h + 4, 12, SSD1306_WHITE);
      display.fillCircle(cx, cy, 5, SSD1306_BLACK);
      break;
    }

    case EMOTION_ANGRY: {
      display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      if (isLeft) {
        display.fillTriangle(x0 - 1, y0 - 1, x0 + w + 1, y0 - 1, x0 + w + 1, y0 + h / 2 + 2, SSD1306_BLACK);
        display.drawLine(x0 - 2, y0 + 1, x0 + w + 2, y0 + h / 2 + 4, SSD1306_WHITE);
        display.drawLine(x0 - 2, y0 + 2, x0 + w + 2, y0 + h / 2 + 5, SSD1306_WHITE);
      } else {
        display.fillTriangle(x0 - 1, y0 - 1, x0 + w + 1, y0 - 1, x0 - 1, y0 + h / 2 + 2, SSD1306_BLACK);
        display.drawLine(x0 - 2, y0 + h / 2 + 4, x0 + w + 2, y0 + 1, SSD1306_WHITE);
        display.drawLine(x0 - 2, y0 + h / 2 + 5, x0 + w + 2, y0 + 2, SSD1306_WHITE);
      }
      break;
    }

    case EMOTION_SAD: {
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
        display.drawLine(x0, cy + 2, cx, cy - 4, SSD1306_WHITE);
        display.drawLine(cx, cy - 4, x0 + w, cy + 2, SSD1306_WHITE);
        display.drawLine(x0, cy + 3, cx, cy - 3, SSD1306_WHITE);
        display.drawLine(cx, cy - 3, x0 + w, cy + 3, SSD1306_WHITE);
      } else {
        display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      }
      break;
    }

    case EMOTION_SLEEPY: {
      display.fillRoundRect(x0, cy - 2, w, 4, 2, SSD1306_WHITE);
      break;
    }

    case EMOTION_LOVE: {
      int hx = cx;
      int hy = cy - 2;
      display.fillCircle(hx - 6, hy - 4, 7, SSD1306_WHITE);
      display.fillCircle(hx + 6, hy - 4, 7, SSD1306_WHITE);
      display.fillTriangle(hx - 13, hy - 2, hx + 13, hy - 2, hx, hy + 13, SSD1306_WHITE);
      break;
    }

    case EMOTION_SQUINT: {
      display.fillRoundRect(x0, cy - 4, w, 9, 3, SSD1306_WHITE);
      break;
    }

    case EMOTION_NORMAL:
    default: {
      display.fillRoundRect(x0, y0, w, h, r, SSD1306_WHITE);
      display.fillCircle(x0 + 6, y0 + 6, 2, SSD1306_BLACK);
      break;
    }
  }
}

void drawExtraFeatures(Emotion emotion) {
  switch (emotion) {
    case EMOTION_HAPPY:
      display.drawCircle(64, 52, 6, SSD1306_WHITE);
      display.fillRect(57, 46, 14, 6, SSD1306_BLACK);
      break;

    case EMOTION_SURPRISED:
      display.drawCircle(64, 55, 3, SSD1306_WHITE);
      break;

    case EMOTION_LOVE:
      display.drawCircle(64, 54, 5, SSD1306_WHITE);
      display.fillRect(58, 48, 12, 6, SSD1306_BLACK);
      break;

    case EMOTION_SLEEPY: {
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      if (sleepZIndex >= 0) { display.setCursor(102, 18); display.print(F("z")); }
      if (sleepZIndex >= 1) { display.setCursor(110, 11); display.print(F("Z")); }
      if (sleepZIndex >= 2) { display.setCursor(118, 4);  display.print(F("Z")); }
      break;
    }

    default:
      break;
  }
}

void updateBlinkAnimation() {
  unsigned long now = millis();
  if (!isBlinking && currentEmotion != EMOTION_SLEEPY && currentEmotion != EMOTION_LOVE) {
    if (now >= nextBlinkTime) {
      isBlinking = true;
    }
  }

  if (isBlinking) {
    static int blinkPhase = 0;
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
        nextBlinkTime = now + random(2500, 5500);
      }
    }
  } else {
    eyeHeightFactor = 100;
  }
}

void updateGazeMovement() {
  if (eyeOffsetX < targetOffsetX) eyeOffsetX++;
  else if (eyeOffsetX > targetOffsetX) eyeOffsetX--;

  if (eyeOffsetY < targetOffsetY) eyeOffsetY++;
  else if (eyeOffsetY > targetOffsetY) eyeOffsetY--;

  if (currentEmotion == EMOTION_NORMAL || currentEmotion == EMOTION_SQUINT) {
    static unsigned long nextGazeShift = 0;
    if (millis() > nextGazeShift) {
      int dir = random(0, 6);
      switch (dir) {
        case 0: targetOffsetX = 0;  targetOffsetY = 0;  break;
        case 1: targetOffsetX = -6; targetOffsetY = 0;  break;
        case 2: targetOffsetX = 6;  targetOffsetY = 0;  break;
        case 3: targetOffsetX = 0;  targetOffsetY = -3; break;
        case 4: targetOffsetX = 0;  targetOffsetY = 3;  break;
        default: targetOffsetX = 0; targetOffsetY = 0; break;
      }
      nextGazeShift = millis() + random(1200, 3000);
    }
  } else {
    targetOffsetX = 0;
    targetOffsetY = 0;
  }
}

// ==========================================
// STOPWATCH LOGIC & CUTE ROBOTIC UI
// ==========================================
void updateMiniPetAnimation() {
  unsigned long now = millis();

  // Mini pet blinks periodically
  if (!miniBlinking && now >= nextMiniBlinkTime) {
    miniBlinking = true;
  }

  if (miniBlinking) {
    static int phase = 0;
    if (phase == 0) {
      miniBlinkFactor -= 35;
      if (miniBlinkFactor <= 10) {
        miniBlinkFactor = 10;
        phase = 1;
      }
    } else {
      miniBlinkFactor += 35;
      if (miniBlinkFactor >= 100) {
        miniBlinkFactor = 100;
        phase = 0;
        miniBlinking = false;
        nextMiniBlinkTime = now + random(2000, 4500);
      }
    }
  }

  // Reactive gaze based on stopwatch state
  if (swState == SW_RUNNING) {
    // Excited, rapidly looking back and forth
    static unsigned long nextMiniShift = 0;
    if (now > nextMiniShift) {
      miniEyeOffsetX = (miniEyeOffsetX == 0) ? (random(0, 2) == 0 ? -3 : 3) : 0;
      nextMiniShift = now + 400;
    }
  } else {
    miniEyeOffsetX = 0;
    miniEyeOffsetY = 0;
  }
}

void drawMiniPetFace(int cx, int cy, StopwatchState state) {
  const int miniW = 12;
  const int miniH = (12 * miniBlinkFactor) / 100;
  const int spacing = 10;

  int leftX = cx - spacing + miniEyeOffsetX;
  int rightX = cx + spacing + miniEyeOffsetX;
  int eyeY = cy + miniEyeOffsetY;

  if (miniH <= 2) {
    display.drawFastHLine(leftX - miniW / 2, eyeY, miniW, SSD1306_WHITE);
    display.drawFastHLine(rightX - miniW / 2, eyeY, miniW, SSD1306_WHITE);
    return;
  }

  if (state == SW_RUNNING) {
    // Energetic / Cheering happy curved eyes with cute animated blush
    display.fillRoundRect(leftX - miniW / 2, eyeY - miniH / 2, miniW, miniH, 3, SSD1306_WHITE);
    display.fillCircle(leftX, eyeY + miniH / 2 + 1, miniW / 2 + 1, SSD1306_BLACK);

    display.fillRoundRect(rightX - miniW / 2, eyeY - miniH / 2, miniW, miniH, 3, SSD1306_WHITE);
    display.fillCircle(rightX, eyeY + miniH / 2 + 1, miniW / 2 + 1, SSD1306_BLACK);

    // Cute tiny open mouth
    display.fillCircle(cx, cy + 6, 2, SSD1306_WHITE);
  } else if (state == SW_PAUSED) {
    // Curious / Squinting eyes waiting for resume
    display.fillRoundRect(leftX - miniW / 2, eyeY - 2, miniW, 5, 2, SSD1306_WHITE);
    display.fillRoundRect(rightX - miniW / 2, eyeY - 2, miniW, 5, 2, SSD1306_WHITE);

    // "Puzzled" dot mouth
    display.drawPixel(cx, cy + 5, SSD1306_WHITE);
  } else {
    // Normal friendly mini eyes with reflection
    display.fillRoundRect(leftX - miniW / 2, eyeY - miniH / 2, miniW, miniH, 3, SSD1306_WHITE);
    display.drawPixel(leftX - 2, eyeY - 2, SSD1306_BLACK);

    display.fillRoundRect(rightX - miniW / 2, eyeY - miniH / 2, miniW, miniH, 3, SSD1306_WHITE);
    display.drawPixel(rightX - 2, eyeY - 2, SSD1306_BLACK);

    // Cute tiny smile
    display.drawPixel(cx - 2, cy + 5, SSD1306_WHITE);
    display.drawPixel(cx - 1, cy + 6, SSD1306_WHITE);
    display.drawPixel(cx,     cy + 6, SSD1306_WHITE);
    display.drawPixel(cx + 1, cy + 6, SSD1306_WHITE);
    display.drawPixel(cx + 2, cy + 5, SSD1306_WHITE);
  }
}

void renderStopwatchScreen() {
  unsigned long now = millis();
  unsigned long currentElapsed = swElapsedTime;
  if (swState == SW_RUNNING) {
    currentElapsed += (now - swStartTime);
  }

  // Breakdown time
  unsigned long totalCentis = currentElapsed / 10;
  unsigned long centis = totalCentis % 100;
  unsigned long totalSeconds = currentElapsed / 1000;
  unsigned long seconds = totalSeconds % 60;
  unsigned long minutes = (totalSeconds / 60) % 100;

  // Header Bar with decorative robot frames
  display.drawFastHLine(0, 0, 128, SSD1306_WHITE);
  display.drawFastHLine(0, 21, 128, SSD1306_WHITE);

  // Mini robot antenna on header
  display.drawFastVLine(64, 0, 3, SSD1306_WHITE);
  display.fillCircle(64, 0, 2, SSD1306_WHITE);

  // Render reactive mini robot companion
  updateMiniPetAnimation();
  drawMiniPetFace(64, 11, swState);

  // Left & Right corner status badges
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Mode label
  display.setCursor(3, 7);
  display.print(F("BOT"));

  // State text
  display.setCursor(95, 7);
  if (swState == SW_RUNNING) {
    // Blinking animated RUN indicator
    if ((now / 250) % 2 == 0) {
      display.print(F("RUN >"));
    } else {
      display.print(F("RUN  "));
    }
  } else if (swState == SW_PAUSED) {
    display.print(F("PAUSE"));
  } else {
    display.print(F("READY"));
  }

  // Main Digital Stopwatch Display (Large & Crisp)
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu", minutes, seconds);

  display.setTextSize(2);
  display.setCursor(14, 27);
  display.print(timeBuf);

  // Smaller hundredths-of-a-second readout
  snprintf(timeBuf, sizeof(timeBuf), ".%02lu", centis);
  display.setTextSize(1);
  display.setCursor(80, 34);
  display.print(timeBuf);

  // Animated futuristic bottom progress track / HUD tick
  display.drawRoundRect(4, 49, 120, 12, 3, SSD1306_WHITE);

  if (swState == SW_RUNNING || swState == SW_PAUSED) {
    // Fill gauge smoothly based on seconds (0 to 59)
    int barWidth = (seconds * 114) / 59;
    if (barWidth > 114) barWidth = 114;
    display.fillRect(7, 52, barWidth, 6, SSD1306_WHITE);

    // Running spinner indicator
    if (swState == SW_RUNNING) {
      int spinnerTick = (now / 120) % 4;
      const char spinChars[] = {'|', '/', '-', '\\'};
      display.setCursor(108, 34);
      display.print(spinChars[spinnerTick]);
    }
  } else {
    // Subtle breathing pulse in READY state
    display.setTextSize(1);
    display.setCursor(20, 52);
    display.print(F("[ CLICK TO START ]"));
  }
}

// Mode switch transition animation
void triggerModeTransition() {
  isTransitioning = true;
  transitionStartTime = millis();
}

void renderTransition() {
  unsigned long elapsed = millis() - transitionStartTime;
  if (elapsed >= TRANSITION_DURATION) {
    isTransitioning = false;
    return;
  }

  // Cool digital curtain sweep effect
  int progress = (elapsed * 64) / TRANSITION_DURATION;
  display.fillRect(0, 0, 128, progress, SSD1306_BLACK);
  display.drawFastHLine(0, progress, 128, SSD1306_WHITE);
  display.drawFastHLine(0, 63 - progress, 128, SSD1306_WHITE);
}

// ==========================================
// SERIAL INTERFACE
// ==========================================
void handleSerialCommands() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd <= 32) continue;

    switch (cmd) {
      // Mode toggle
      case 'm':
      case 'M':
        currentMode = (currentMode == MODE_ROBOT_PET) ? MODE_STOPWATCH : MODE_ROBOT_PET;
        triggerModeTransition();
        Serial.print(F("[Mode] Switched to: "));
        Serial.println(currentMode == MODE_ROBOT_PET ? F("ROBOT PET") : F("STOPWATCH"));
        return;

      // Stopwatch controls
      case ' ':
      case 'p':
      case 'P':
        if (currentMode == MODE_STOPWATCH) {
          if (swState == SW_RUNNING) {
            swState = SW_PAUSED;
            swElapsedTime += (millis() - swStartTime);
            Serial.println(F("[Stopwatch] Paused"));
          } else {
            swStartTime = millis();
            swState = SW_RUNNING;
            Serial.println(F("[Stopwatch] Started/Resumed"));
          }
        }
        return;

      case '0':
      case 'k':
      case 'K':
        if (currentMode == MODE_STOPWATCH) {
          swState = SW_RESET;
          swElapsedTime = 0;
          Serial.println(F("[Stopwatch] Reset to 00:00.00"));
        }
        return;

      // Emotion controls
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
        Serial.print(F("[Pet] Auto cycle: "));
        Serial.println(autoMode ? F("ON") : F("OFF"));
        return;

      default:
        Serial.print(F("Cmd '")); Serial.print(cmd); Serial.println(F("' | [m]ode, [space]start/pause, [0]reset, [n,h,s,a,x,w,z,l,c,r]"));
        return;
    }

    Serial.print(F("[Pet Emotion] Changed to: "));
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

// ==========================================
// ARDUINO SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(300);

  // Setup User Button on STM32 Nucleo
  pinMode(USER_BTN, INPUT_PULLUP);

  Serial.println(F("\n========================================================"));
  Serial.println(F("   STM32 Expressive Robot Face + Pet Animated Stopwatch  "));
  Serial.println(F("========================================================"));
  Serial.println(F("Hardware Button Controls (USER_BTN / Blue Button PC13):"));
  Serial.println(F(" • LONG PRESS  (> 0.6s) : Switch Mode (Robot Pet <-> Stopwatch)"));
  Serial.println(F(" • In Stopwatch Mode:"));
  Serial.println(F("    - Single Click       : Start / Pause Stopwatch"));
  Serial.println(F("    - Double Click       : Reset Stopwatch to 00:00.00"));
  Serial.println(F(" • In Robot Pet Mode:"));
  Serial.println(F("    - Single Click       : Next Emotion"));
  Serial.println(F("    - Double Click       : Toggle Auto Emotion Cycle"));
  Serial.println(F("--------------------------------------------------------"));
  Serial.println(F("Serial Shortcuts:"));
  Serial.println(F(" [m] Switch Mode    [space/p] Start/Pause    [0/k] Reset"));
  Serial.println(F(" [n] Normal         [h] Happy                [s] Surprised"));
  Serial.println(F(" [a] Angry          [x] Sad                  [w] Wink"));
  Serial.println(F(" [z] Sleepy         [l] Love                 [c] Squint"));
  Serial.println(F(" [r] Toggle Auto cycle"));
  Serial.println(F("========================================================\n"));

  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed!"));
    for (;;);
  }

  display.clearDisplay();
  display.display();

  nextBlinkTime = millis() + 2000;
  nextMiniBlinkTime = millis() + 2500;
  lastEmotionChange = millis();
}

void loop() {
  // 1. Process Serial Inputs
  handleSerialCommands();

  // 2. Process Button Gestures
  ButtonEvent btn = checkButton();
  if (btn == BTN_LONG_PRESS) {
    currentMode = (currentMode == MODE_ROBOT_PET) ? MODE_STOPWATCH : MODE_ROBOT_PET;
    triggerModeTransition();
    Serial.print(F("[Button] Mode changed -> "));
    Serial.println(currentMode == MODE_ROBOT_PET ? F("ROBOT PET") : F("STOPWATCH"));
  } else if (btn == BTN_SINGLE_CLICK) {
    if (currentMode == MODE_STOPWATCH) {
      if (swState == SW_RUNNING) {
        swState = SW_PAUSED;
        swElapsedTime += (millis() - swStartTime);
        Serial.println(F("[Button] Stopwatch: PAUSED"));
      } else {
        swStartTime = millis();
        swState = SW_RUNNING;
        Serial.println(F("[Button] Stopwatch: STARTED"));
      }
    } else {
      // In Pet Face mode: single click advances to next emotion
      currentEmotion = (Emotion)((currentEmotion + 1) % EMOTION_COUNT);
      autoMode = false;
      Serial.print(F("[Button] Pet Emotion -> "));
      Serial.println(currentEmotion);
    }
  } else if (btn == BTN_DOUBLE_CLICK) {
    if (currentMode == MODE_STOPWATCH) {
      swState = SW_RESET;
      swElapsedTime = 0;
      Serial.println(F("[Button] Stopwatch: RESET"));
    } else {
      autoMode = !autoMode;
      Serial.print(F("[Button] Pet Auto Cycle -> "));
      Serial.println(autoMode ? F("ON") : F("OFF"));
    }
  }

  // 3. Clear Screen Buffer
  display.clearDisplay();

  unsigned long now = millis();

  // 4. Render Current Mode
  if (currentMode == MODE_ROBOT_PET) {
    // Auto cycle emotions
    if (autoMode && (now - lastEmotionChange > emotionDuration)) {
      currentEmotion = (Emotion)((currentEmotion + 1) % EMOTION_COUNT);
      lastEmotionChange = now;
      if (currentEmotion == EMOTION_SLEEPY) emotionDuration = 5000;
      else if (currentEmotion == EMOTION_NORMAL) emotionDuration = 4500;
      else emotionDuration = 3000;
    }

    // Dynamic eyes animation
    updateBlinkAnimation();
    updateGazeMovement();

    // Sleep Zzz animation
    if (currentEmotion == EMOTION_SLEEPY && (now - lastZAnimTime > 400)) {
      sleepZIndex = (sleepZIndex + 1) % 4;
      lastZAnimTime = now;
    }

    int curH = (EYE_HEIGHT * eyeHeightFactor) / 100;
    int curRadius = (EYE_RADIUS * eyeHeightFactor) / 100;
    if (curRadius < 1) curRadius = 1;

    drawEye(LEFT_EYE_X + eyeOffsetX, EYE_Y + eyeOffsetY, EYE_WIDTH, curH, curRadius, currentEmotion, true);
    drawEye(RIGHT_EYE_X + eyeOffsetX, EYE_Y + eyeOffsetY, EYE_WIDTH, curH, curRadius, currentEmotion, false);
    drawExtraFeatures(currentEmotion);
  } 
  else if (currentMode == MODE_STOPWATCH) {
    renderStopwatchScreen();
  }

  // 5. Render mode transition sweep if active
  if (isTransitioning) {
    renderTransition();
  }

  // 6. Push to OLED Screen
  display.display();

  // ~40 FPS animation loop
  delay(25);
}

