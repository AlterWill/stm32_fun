/**
 * INMP441 Microphone Test — Bit-Bang I²S
 *
 * Wiring (INMP441 -> Nucleo F446RE):
 *   SD  -> A2  (PA4)   — Serial Data (mic output)
 *   SCK -> D13 (PA5)   — Bit Clock
 *   WS  -> D14 (PB9)   — Word Select (L/R clock)
 *   VDD -> 3.3V
 *   GND -> GND
 *   L/R -> GND          (selects LEFT channel)
 *
 * What to expect in Serial Monitor (115200 baud):
 *   - Values are 24-bit signed samples from the mic (range ~±8388607)
 *   - In silence: small values near 0 (noise floor)
 *   - Blow on / tap the mic: values should jump significantly
 *   - Peak amplitude is printed every 100ms so you can see it react
 */

#include <Arduino.h>

// ── Pin Definitions ────────────────────────────────────────────────────────
#define MIC_SD  A2   // PA4 — INMP441 Serial Data output
#define MIC_SCK D13  // PA5 — Bit Clock (we drive this)
#define MIC_WS  D14  // PB9 — Word Select / LRCK (we drive this)

// ── Timing ─────────────────────────────────────────────────────────────────
// INMP441 supports 1–4 MHz SCK. We're bit-banging so it'll be slower (~100kHz),
// which is fine for a sanity-check test.
#define SCK_HALF_PERIOD_US 5   // 5µs half-period → ~100kHz SCK

// ── Sample Collection ──────────────────────────────────────────────────────
#define SAMPLES_PER_REPORT 64  // collect this many samples then report stats
#define REPORT_INTERVAL_MS 200 // how often to print (ms)

// ──────────────────────────────────────────────────────────────────────────
// Read one 24-bit signed sample from INMP441 (LEFT channel)
//
// I²S protocol (INMP441 left-justified):
//   WS LOW  = LEFT channel data
//   WS HIGH = RIGHT channel (we ignore it)
//   Data is 24 bits, MSB first, clocked on SCK rising edge
// ──────────────────────────────────────────────────────────────────────────
int32_t readI2SSample() {
    int32_t sample = 0;

    // ── LEFT channel (WS low) ───────────────────────────────────────────
    // Pull WS low to signal LEFT channel start
    digitalWrite(MIC_WS, LOW);
    delayMicroseconds(SCK_HALF_PERIOD_US);

    // Clock in 24 bits, MSB first
    for (int i = 0; i < 24; i++) {
        // Rising edge — mic shifts out the next bit
        digitalWrite(MIC_SCK, HIGH);
        delayMicroseconds(SCK_HALF_PERIOD_US);

        // Sample the data line
        sample = (sample << 1) | (digitalRead(MIC_SD) ? 1 : 0);

        // Falling edge
        digitalWrite(MIC_SCK, LOW);
        delayMicroseconds(SCK_HALF_PERIOD_US);
    }

    // Clock through remaining bits of the LEFT frame (I²S frame = 32 bits each side)
    // INMP441 outputs 24 bits then zeros — clock through 8 more to complete frame
    for (int i = 0; i < 8; i++) {
        digitalWrite(MIC_SCK, HIGH);
        delayMicroseconds(SCK_HALF_PERIOD_US);
        digitalWrite(MIC_SCK, LOW);
        delayMicroseconds(SCK_HALF_PERIOD_US);
    }

    // ── RIGHT channel (WS high) — skip it entirely ──────────────────────
    digitalWrite(MIC_WS, HIGH);
    for (int i = 0; i < 32; i++) {
        digitalWrite(MIC_SCK, HIGH);
        delayMicroseconds(SCK_HALF_PERIOD_US);
        digitalWrite(MIC_SCK, LOW);
        delayMicroseconds(SCK_HALF_PERIOD_US);
    }

    // Sign-extend from 24-bit to 32-bit
    if (sample & 0x800000) {
        sample |= 0xFF000000;
    }

    return sample;
}

// ──────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);  // give Serial monitor time to connect

    // Configure pins
    pinMode(MIC_SCK, OUTPUT);
    pinMode(MIC_WS,  OUTPUT);
    pinMode(MIC_SD,  INPUT);

    // Start with clock low, WS high (idle state)
    digitalWrite(MIC_SCK, LOW);
    digitalWrite(MIC_WS,  HIGH);

    Serial.println("===========================================");
    Serial.println("  INMP441 Microphone Bit-Bang I²S Test");
    Serial.println("===========================================");
    Serial.println("  SD  -> A2  | SCK -> D13 | WS -> D14");
    Serial.println("  Make noise or tap the mic to see response");
    Serial.println("===========================================");
    Serial.println();

    // Give INMP441 startup time (it needs ~50ms after power-on)
    delay(100);
}

// ──────────────────────────────────────────────────────────────────────────
void loop() {
    int32_t minVal  =  0x7FFFFFFF;
    int32_t maxVal  = -0x7FFFFFFF;
    int64_t sum     = 0;
    int32_t absMax  = 0;

    // Collect a batch of samples
    for (int i = 0; i < SAMPLES_PER_REPORT; i++) {
        int32_t s = readI2SSample();
        sum += s;
        if (s < minVal) minVal = s;
        if (s > maxVal) maxVal = s;
        int32_t absS = abs(s);
        if (absS > absMax) absMax = absS;
    }

    int32_t avg       = (int32_t)(sum / SAMPLES_PER_REPORT);
    int32_t peakToPeak = maxVal - minVal;

    // ── Visual bar (amplitude meter) ──────────────────────────────────
    // Map peak-to-peak (0 to 16,777,215 max for 24-bit) to bar of 20 chars
    int barLen = map(constrain(peakToPeak, 0, 800000), 0, 800000, 0, 20);
    char bar[21];
    for (int i = 0; i < 20; i++) bar[i] = (i < barLen) ? '#' : '-';
    bar[20] = '\0';

    // ── Print report ──────────────────────────────────────────────────
    Serial.print("[");
    Serial.print(bar);
    Serial.print("]  peak-to-peak: ");
    Serial.print(peakToPeak);
    Serial.print("  avg: ");
    Serial.print(avg);
    Serial.print("  min: ");
    Serial.print(minVal);
    Serial.print("  max: ");
    Serial.println(maxVal);

    delay(REPORT_INTERVAL_MS);
}
