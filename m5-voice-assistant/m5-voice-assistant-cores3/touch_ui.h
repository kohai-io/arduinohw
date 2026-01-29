#ifndef TOUCH_UI_H
#define TOUCH_UI_H

// Touch button definitions for CoreS3 touchscreen
struct TouchButton {
  int x, y, w, h;
  String label;
  uint16_t color;
  bool visible;
};

// Button IDs
enum ButtonID {
  BTN_VOICE = 0,
  BTN_CAMERA = 1,
  BTN_NEW_CHAT = 2,
  BTN_PROFILE = 3,
  BTN_COUNT = 4
};

TouchButton touchButtons[BTN_COUNT];
bool touchButtonsInitialized = false;

// Initialize touch buttons for CoreS3 (320x240 screen)
void initTouchButtons() {
  // Main action buttons - smaller, positioned at bottom
  // Using darker colors for better contrast with white text
  touchButtons[BTN_VOICE] = {10, 195, 140, 40, "Voice", 0x0320, true};      // Dark green
  touchButtons[BTN_CAMERA] = {170, 195, 140, 40, "Camera", 0x0014, true};   // Dark blue
  
  // Secondary buttons (smaller, bottom area)
  touchButtons[BTN_NEW_CHAT] = {10, 190, 95, 40, "New Chat", 0xC300, false};  // Dark orange
  touchButtons[BTN_PROFILE] = {115, 190, 95, 40, "Profile", 0x8010, false};   // Dark purple
  
  touchButtonsInitialized = true;
}

// Draw a single touch button
void drawTouchButton(const TouchButton &btn) {
  if (!btn.visible) return;
  
  // Draw shadow for depth effect
  M5.Display.fillRoundRect(btn.x + 2, btn.y + 2, btn.w, btn.h, 8, TFT_DARKGREY);
  
  // Draw button background
  M5.Display.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 8, btn.color);
  
  // Draw bright border for better visibility
  M5.Display.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 8, TFT_WHITE);
  M5.Display.drawRoundRect(btn.x + 1, btn.y + 1, btn.w - 2, btn.h - 2, 7, TFT_LIGHTGREY);
  
  // Draw button label
  M5.Display.setTextColor(TFT_WHITE, btn.color);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextFont(2);  // Smaller font for compact buttons
  M5.Display.drawString(btn.label, btn.x + btn.w/2, btn.y + btn.h/2);
}

// Draw all visible touch buttons
void drawTouchButtons() {
  if (!touchButtonsInitialized) {
    initTouchButtons();
  }
  
  for (int i = 0; i < BTN_COUNT; i++) {
    drawTouchButton(touchButtons[i]);
  }
}

// Check if touch point is within button bounds
bool isTouchInButton(int touchX, int touchY, const TouchButton &btn) {
  if (!btn.visible) return false;
  return (touchX >= btn.x && touchX <= btn.x + btn.w &&
          touchY >= btn.y && touchY <= btn.y + btn.h);
}

// Get which button was touched (-1 if none)
int getTouchedButton(int touchX, int touchY) {
  for (int i = 0; i < BTN_COUNT; i++) {
    if (isTouchInButton(touchX, touchY, touchButtons[i])) {
      return i;
    }
  }
  return -1;
}

// Draw screen with touch buttons at bottom
void drawScreenWithButtons(const String &text) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextFont(2);

  // Calculate text area (leave space for buttons at bottom)
  int textAreaHeight = HEIGHT - 50;  // Reserve 50px for smaller buttons
  
  // Count lines in text
  int lineCount = 1;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (text[i] == '\n') lineCount++;
  }

  int lineHeight = M5.Display.fontHeight() + 4;
  int startY = (textAreaHeight - lineCount * lineHeight) / 2 + lineHeight / 2;

  // Draw text lines
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
  
  // Draw touch buttons
  drawTouchButtons();
}

// Show/hide specific buttons
void setButtonVisible(ButtonID btnId, bool visible) {
  if (btnId < BTN_COUNT) {
    touchButtons[btnId].visible = visible;
  }
}

// Set button layout mode
void setButtonLayout(const String &mode) {
  if (mode == "main") {
    // Main screen: Voice and Camera buttons
    setButtonVisible(BTN_VOICE, true);
    setButtonVisible(BTN_CAMERA, true);
    setButtonVisible(BTN_NEW_CHAT, false);
    setButtonVisible(BTN_PROFILE, false);
  } else if (mode == "settings") {
    // Settings: New Chat and Profile buttons
    setButtonVisible(BTN_VOICE, false);
    setButtonVisible(BTN_CAMERA, false);
    setButtonVisible(BTN_NEW_CHAT, true);
    setButtonVisible(BTN_PROFILE, true);
  }
}

#endif // TOUCH_UI_H
