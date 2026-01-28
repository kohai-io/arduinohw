#ifndef CONFIG_H
#define CONFIG_H

#include <M5Unified.h>

// Audio quality profiles
struct AudioProfile {
  const char* name;
  int sampleRate;
  int recordSeconds;
  const char* quality;
};

// Profiles for M5StickC Plus2 (limited RAM ~120KB safe)
static const AudioProfile STICK_PROFILES[] = {
  {"Standard", 8000, 5, "Good"},      // 80KB - default
  {"HQ Short", 16000, 3, "Excellent"} // 96KB - high quality
};

// Profiles for Core2/CoreS3 (more RAM ~300KB+ safe)
static const AudioProfile CORE_PROFILES[] = {
  {"Standard", 8000, 8, "Good"},       // 128KB - balanced default
  {"Long", 8000, 15, "Good"},           // 240KB - extended recording
  {"HQ Short", 16000, 5, "Excellent"} // 160KB - high quality, quick

};

// Voice Activity Detection (VAD) settings - defined in main .ino
// const int VAD_SILENCE_THRESHOLD = 500;
// const float VAD_SILENCE_DURATION = 1.5;
// const bool VAD_ENABLED = true;

// Display dimensions - set dynamically in setup()
extern int WIDTH;
extern int HEIGHT;

// Profile management
extern int currentProfileIndex;
extern bool isLargeDevice;
extern int numProfiles;
extern const AudioProfile* deviceProfiles;

// Current audio settings (dynamic)
extern int SAMPLE_RATE;
extern int RECORD_SECONDS;
extern int RECORD_SAMPLES;
extern int16_t *audioBuffer;

// Dynamic system prompt
extern int currentMaxWords;
extern String systemPrompt;

// Detect device type and configure accordingly
void detectDeviceType() {
  // Get actual display dimensions (works for StickC Plus 2, Core 2, etc.)
  WIDTH = M5.Display.width();
  HEIGHT = M5.Display.height();
  Serial.printf("Display: %dx%d\n", WIDTH, HEIGHT);

  // Detect device type based on display size
  size_t freeHeap = ESP.getFreeHeap();
  Serial.printf("Free heap at startup: %d bytes\n", freeHeap);
  
  // Use display size as primary indicator
  // Core2/CoreS3 have 320x240 displays, StickC has 240x135
  if (WIDTH >= 320 && HEIGHT >= 240) {
    isLargeDevice = true;
    deviceProfiles = CORE_PROFILES;
    numProfiles = 3;
    Serial.println("Device: Core2/CoreS3 (large screen)");
  } else {
    isLargeDevice = false;
    deviceProfiles = STICK_PROFILES;
    numProfiles = 2;
    Serial.println("Device: StickC Plus2 (small screen)");
  }
}

// Build system prompt with device-appropriate word limit
void buildSystemPrompt(const char* LLM_SYSTEM_PROMPT_BASE, int LLM_MAX_WORDS_SMALL, int LLM_MAX_WORDS_LARGE) {
  currentMaxWords = isLargeDevice ? LLM_MAX_WORDS_LARGE : LLM_MAX_WORDS_SMALL;
  systemPrompt = String(LLM_SYSTEM_PROMPT_BASE) + " in " + String(currentMaxWords) + " words or less.";
  Serial.printf("System prompt: %s\n", systemPrompt.c_str());
}

// Apply selected audio profile
void applyAudioProfile(int profileIndex) {
  if (profileIndex < 0 || profileIndex >= numProfiles) return;
  
  const AudioProfile& profile = deviceProfiles[profileIndex];
  SAMPLE_RATE = profile.sampleRate;
  RECORD_SECONDS = profile.recordSeconds;
  RECORD_SAMPLES = SAMPLE_RATE * RECORD_SECONDS;
  currentProfileIndex = profileIndex;
  
  // Free old buffer if exists (will reallocate on next recording)
  if (audioBuffer != nullptr) {
    free(audioBuffer);
    audioBuffer = nullptr;
  }
  
  Serial.printf("Profile: %s (%dHz, %ds, %s)\n", 
                profile.name, profile.sampleRate, profile.recordSeconds, profile.quality);
}

// Cycle to next profile
void nextAudioProfile() {
  int nextIndex = (currentProfileIndex + 1) % numProfiles;
  applyAudioProfile(nextIndex);
}

// Get current profile name
const char* getCurrentProfileName() {
  return deviceProfiles[currentProfileIndex].name;
}

#endif // CONFIG_H
