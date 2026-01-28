#ifndef M5GO_LEDS_H
#define M5GO_LEDS_H

// Note: FastLED.h must be included before this header in the main .ino file

// M5GO-Bottom2 LED configuration
#define M5GO_NUM_LEDS 10
#define M5GO_DATA_PIN 25

// Global LED state
extern CRGB leds[M5GO_NUM_LEDS];
extern bool hasM5GOBottom2;

// M5GO-Bottom2 LED control functions
void detectM5GOBottom2(bool isLargeDevice) {
  if (!isLargeDevice) {
    hasM5GOBottom2 = false;
    return;
  }
  
  // Try to initialize FastLED on GPIO25
  FastLED.addLeds<NEOPIXEL, M5GO_DATA_PIN>(leds, M5GO_NUM_LEDS);
  FastLED.setBrightness(50);
  
  // Test pattern - flash all LEDs briefly
  fill_solid(leds, M5GO_NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(100);
  fill_solid(leds, M5GO_NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  hasM5GOBottom2 = true;
  Serial.println("M5GO-Bottom2 detected and initialized");
}

void setM5GOLEDs(CRGB color) {
  if (!hasM5GOBottom2) return;
  fill_solid(leds, M5GO_NUM_LEDS, color);
  FastLED.show();
}

void setM5GOLEDsPattern(int activeLeds, CRGB color) {
  if (!hasM5GOBottom2) return;
  for (int i = 0; i < M5GO_NUM_LEDS; i++) {
    leds[i] = (i < activeLeds) ? color : CRGB::Black;
  }
  FastLED.show();
}

void pulseM5GOLEDs(CRGB color, int delayMs = 50) {
  if (!hasM5GOBottom2) return;
  
  // Pulse from center outward
  for (int i = 0; i < 5; i++) {
    leds[4 - i] = color;
    leds[5 + i] = color;
    FastLED.show();
    delay(delayMs);
  }
  
  delay(delayMs);
  
  // Fade out
  for (int i = 4; i >= 0; i--) {
    leds[4 - i] = CRGB::Black;
    leds[5 + i] = CRGB::Black;
    FastLED.show();
    delay(delayMs);
  }
}

void breatheM5GOLEDs(CRGB color, int cycles = 1) {
  if (!hasM5GOBottom2) return;
  
  for (int c = 0; c < cycles; c++) {
    // Breathe in
    for (int brightness = 0; brightness <= 255; brightness += 5) {
      CRGB dimColor = color;
      dimColor.nscale8(brightness);
      fill_solid(leds, M5GO_NUM_LEDS, dimColor);
      FastLED.show();
      delay(10);
    }
    
    // Breathe out
    for (int brightness = 255; brightness >= 0; brightness -= 5) {
      CRGB dimColor = color;
      dimColor.nscale8(brightness);
      fill_solid(leds, M5GO_NUM_LEDS, dimColor);
      FastLED.show();
      delay(10);
    }
  }
  
  fill_solid(leds, M5GO_NUM_LEDS, CRGB::Black);
  FastLED.show();
}

void clearM5GOLEDs() {
  if (!hasM5GOBottom2) return;
  fill_solid(leds, M5GO_NUM_LEDS, CRGB::Black);
  FastLED.show();
}

#endif // M5GO_LEDS_H
