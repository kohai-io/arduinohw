#ifndef DISPLAY_H
#define DISPLAY_H

#include <M5Unified.h>

// External references
extern int WIDTH;
extern int HEIGHT;
extern const int VAD_SILENCE_THRESHOLD;

// Real-time audio display state
extern volatile bool isRecording;
extern volatile int currentRmsLevel;
extern volatile int recordingSecondsLeft;

// Track previous values to avoid unnecessary redraws
static int lastDisplayedSeconds = -1;
static int lastDisplayedBars = -1;
static bool lastSpeakingState = false;
static bool audioLevelInitialized = false;

void drawScreen(const String &text) {
  Serial.println("Drawing to screen: " + text);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextFont(2);

  int lineCount = 1;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (text[i] == '\n')
      lineCount++;
  }

  int lineHeight = M5.Display.fontHeight() + 4;
  int startY = (HEIGHT - lineCount * lineHeight) / 2 + lineHeight / 2;

  int lineNum = 0;
  int lineStart = 0;
  for (unsigned int i = 0; i <= text.length(); i++) {
    if (i == text.length() || text[i] == '\n') {
      String line = text.substring(lineStart, i);
      M5.Display.drawString(line, WIDTH / 2, startY + lineNum * lineHeight);
      lineNum++;
      lineStart = i + 1;
    }
  }
}

void drawProgress(int seconds) {
  String msg = "Recording... " + String(seconds);
  drawScreen(msg);
}

void initAudioLevelDisplay() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextFont(2);
  
  // Draw static title
  M5.Display.drawString("Recording...", WIDTH / 2, 15);
  
  // Draw inactive bars as background
  int barHeight = 12;
  int barSpacing = 3;
  int maxBars = 10;
  int barStartY = 75;
  int totalWidth = WIDTH - 40;
  int barWidth = (totalWidth / maxBars) - barSpacing;
  int startX = (WIDTH - (maxBars * (barWidth + barSpacing) - barSpacing)) / 2;
  
  for (int i = 0; i < maxBars; i++) {
    int x = startX + i * (barWidth + barSpacing);
    M5.Display.fillRect(x, barStartY, barWidth, barHeight, TFT_DARKGREY);
  }
  
  lastDisplayedSeconds = -1;
  lastDisplayedBars = -1;
  lastSpeakingState = false;
  audioLevelInitialized = true;
}

void drawAudioLevel(int seconds, int rmsLevel) {
  // Initialize on first call
  if (!audioLevelInitialized) {
    initAudioLevelDisplay();
  }
  
  // Bar dimensions
  int barHeight = 12;
  int barSpacing = 3;
  int maxBars = 10;
  int barStartY = 75;
  int totalWidth = WIDTH - 40;
  int barWidth = (totalWidth / maxBars) - barSpacing;
  int startX = (WIDTH - (maxBars * (barWidth + barSpacing) - barSpacing)) / 2;
  
  // Only update countdown if changed
  if (seconds != lastDisplayedSeconds) {
    // Clear previous countdown area
    M5.Display.fillRect(WIDTH/2 - 30, 30, 60, 35, TFT_BLACK);
    
    M5.Display.setTextFont(4);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(seconds <= 1 ? TFT_RED : TFT_GREEN);
    M5.Display.drawString(String(seconds), WIDTH / 2, 45);
    lastDisplayedSeconds = seconds;
  }
  
  // Calculate active bars
  int activeBars = map(constrain(rmsLevel, 0, 3000), 0, 3000, 0, maxBars);
  
  // Only update bars if changed
  if (activeBars != lastDisplayedBars) {
    for (int i = 0; i < maxBars; i++) {
      int x = startX + i * (barWidth + barSpacing);
      uint16_t color;
      
      if (i < activeBars) {
        if (i < 6) {
          color = TFT_GREEN;
        } else if (i < 8) {
          color = TFT_YELLOW;
        } else {
          color = TFT_RED;
        }
      } else {
        color = TFT_DARKGREY;
      }
      
      M5.Display.fillRect(x, barStartY, barWidth, barHeight, color);
    }
    lastDisplayedBars = activeBars;
  }
  
  // Only update status text if speaking state changed
  bool isSpeaking = (rmsLevel >= VAD_SILENCE_THRESHOLD);
  if (isSpeaking != lastSpeakingState) {
    // Clear status area
    M5.Display.fillRect(0, HEIGHT - 25, WIDTH, 20, TFT_BLACK);
    
    M5.Display.setTextFont(1);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(isSpeaking ? TFT_GREEN : TFT_DARKGREY);
    M5.Display.drawString(isSpeaking ? "Speaking" : "Listening...", WIDTH / 2, HEIGHT - 15);
    lastSpeakingState = isSpeaking;
  }
}

// Display task runs on core 0, updates display in real-time
void displayTask(void *parameter) {
  while (true) {
    if (isRecording) {
      drawAudioLevel(recordingSecondsLeft, currentRmsLevel);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS); // ~20fps update rate
  }
}

String wordWrap(const String &text, int maxChars) {
  String result;
  String word;
  int lineLen = 0;

  for (unsigned int i = 0; i <= text.length(); i++) {
    char c = (i < text.length()) ? text[i] : ' ';

    if (c == ' ' || c == '\n') {
      if (lineLen + word.length() > maxChars) {
        result += '\n';
        lineLen = 0;
      }
      result += word;
      lineLen += word.length();
      word = "";

      if (c == ' ' && lineLen > 0) {
        result += ' ';
        lineLen++;
      }
      if (c == '\n') {
        result += '\n';
        lineLen = 0;
      }
    } else {
      word += c;
    }
  }

  return result;
}

#endif // DISPLAY_H
