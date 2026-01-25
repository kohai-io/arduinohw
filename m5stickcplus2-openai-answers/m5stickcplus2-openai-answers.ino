#include "secrets.h"
#include <HTTPClient.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

// MP3 decoding for TTS (Core2/CoreS3 only) - uses arduino-libhelix
#include <MP3DecoderHelix.h>
using namespace libhelix;

// Display dimensions - set dynamically in setup()
static int WIDTH = 240;
static int HEIGHT = 135;

// Credentials are now in secrets.h

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

// Current audio settings (dynamic)
static int SAMPLE_RATE = 8000;
static int RECORD_SECONDS = 5;
static int RECORD_SAMPLES = SAMPLE_RATE * RECORD_SECONDS;
static int16_t *audioBuffer = nullptr;

// Profile management
static int currentProfileIndex = 0;
static bool isLargeDevice = false; // Core2/CoreS3 vs StickC
static int numProfiles = 2;
static const AudioProfile* deviceProfiles = STICK_PROFILES;

// Dynamic system prompt (built based on device type)
static int currentMaxWords = 20;
static String systemPrompt = "";

// Build system prompt with device-appropriate word limit
void buildSystemPrompt() {
  currentMaxWords = isLargeDevice ? LLM_MAX_WORDS_LARGE : LLM_MAX_WORDS_SMALL;
  systemPrompt = String(LLM_SYSTEM_PROMPT_BASE) + " in " + String(currentMaxWords) + " words or less.";
  Serial.printf("System prompt: %s\n", systemPrompt.c_str());
}

// Voice Activity Detection (VAD) settings
static const int VAD_SILENCE_THRESHOLD = 500;  // RMS threshold for silence (adjust based on testing)
static const float VAD_SILENCE_DURATION = 1.5; // Seconds of silence before auto-stop
static const bool VAD_ENABLED = true;          // Enable/disable VAD

String response = "Press A\nto ask a question";

// OpenWebUI chat session tracking
String currentChatId = "";
String currentSessionId = "";

// Track actual recorded samples (for VAD early stop)
int actualRecordedSamples = RECORD_SAMPLES;

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

// TTS audio output buffer and state (kept for replay)
static int16_t* ttsOutputBuffer = nullptr;
static size_t ttsOutputSize = 0;
static size_t ttsOutputIndex = 0;
static int ttsSampleRate = 44100;
static int ttsChannels = 2;

// Last played TTS for replay on button C
static int16_t* lastTtsBuffer = nullptr;
static size_t lastTtsLength = 0;
static int lastTtsSampleRate = 44100;
static int lastTtsChannels = 1;

// Callback for libhelix MP3 decoder - receives decoded PCM samples
void ttsAudioCallback(MP3FrameInfo &info, short *pwm_buffer, size_t len, void *ref) {
  ttsSampleRate = info.samprate;
  ttsChannels = info.nChans;
  
  // Ensure we have enough buffer space
  size_t newSize = ttsOutputIndex + len;
  if (newSize > ttsOutputSize) {
    size_t allocSize = newSize + 8192; // Extra space for more frames
    int16_t* newBuffer = (int16_t*)realloc(ttsOutputBuffer, allocSize * sizeof(int16_t));
    if (newBuffer) {
      ttsOutputBuffer = newBuffer;
      ttsOutputSize = allocSize;
    } else {
      Serial.println("ERROR: Failed to realloc TTS buffer");
      return;
    }
  }
  
  // Copy decoded samples to output buffer
  memcpy(ttsOutputBuffer + ttsOutputIndex, pwm_buffer, len * sizeof(int16_t));
  ttsOutputIndex += len;
}

// Text-to-Speech - speak the response on Core2/CoreS3
void speakText(const String &text) {
  if (!USE_TTS || !isLargeDevice) {
    Serial.println("TTS disabled or not a large device");
    return;
  }
  
  Serial.println("\n========== TEXT-TO-SPEECH ==========");
  Serial.printf("Speaking: %s\n", text.c_str());
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  String ttsUrl = String(OWUI_BASE_URL) + "/api/v1/audio/speech";
  Serial.printf("TTS URL: %s\n", ttsUrl.c_str());
  
  http.begin(client, ttsUrl);
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(30000);
  
  // Build JSON request - OpenWebUI always returns MP3 regardless of format
  String body = "{\"model\":\"" + String(TTS_MODEL) + "\","
                "\"input\":\"" + text + "\","
                "\"voice\":\"" + String(TTS_VOICE) + "\"}";
  
  Serial.println("Requesting TTS...");
  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  if (httpCode == 200) {
    int contentLength = http.getSize();
    Serial.printf("MP3 size: %d bytes\n", contentLength);
    
    if (contentLength > 0 && contentLength < 500000) {
      // Allocate buffer for MP3 data
      uint8_t* mp3Data = (uint8_t*)malloc(contentLength);
      if (mp3Data) {
        WiFiClient* stream = http.getStreamPtr();
        int bytesRead = 0;
        
        while (bytesRead < contentLength && stream->connected()) {
          if (stream->available()) {
            int toRead = min((int)stream->available(), contentLength - bytesRead);
            int read = stream->readBytes(mp3Data + bytesRead, toRead);
            bytesRead += read;
          }
          delay(1);
        }
        
        Serial.printf("Downloaded %d bytes of MP3\n", bytesRead);
        http.end();
        
        // Reset output buffer
        ttsOutputIndex = 0;
        ttsSampleRate = 44100;
        ttsChannels = 2;
        
        // Create MP3 decoder
        MP3DecoderHelix mp3Decoder;
        mp3Decoder.setDataCallback(ttsAudioCallback);
        mp3Decoder.begin();
        
        Serial.println("Decoding MP3...");
        mp3Decoder.write(mp3Data, bytesRead);
        mp3Decoder.end();
        
        free(mp3Data);
        
        Serial.printf("Decoded %d samples at %dHz\n", ttsOutputIndex, ttsSampleRate);
        
        if (ttsOutputIndex > 0 && ttsOutputBuffer) {
          // Reinitialize speaker for each playback
          M5.Speaker.end();
          delay(50);
          M5.Speaker.begin();
          M5.Speaker.setVolume(200);
          
          Serial.printf("Playing audio... (%d channels, %d samples)\n", ttsChannels, ttsOutputIndex);
          // stereo parameter tells M5 if data is interleaved stereo
          bool isStereo = (ttsChannels == 2);
          
          // Play audio - use channel 0
          bool playResult = M5.Speaker.playRaw(ttsOutputBuffer, ttsOutputIndex, ttsSampleRate, isStereo, 1, 0);
          Serial.printf("playRaw returned: %d, isPlaying: %d\n", playResult, M5.Speaker.isPlaying());
          
          // Wait for playback to complete
          delay(100); // Give speaker time to start
          int waitCount = 0;
          while (M5.Speaker.isPlaying() && waitCount < 3000) { // Max 30 sec wait
            delay(10);
            waitCount++;
          }
          delay(100); // Extra delay to ensure buffer is fully consumed
          
          Serial.printf("TTS playback complete (waited %dms)\n", waitCount * 10);
          
          // Release speaker
          M5.Speaker.end();
          
          // Save buffer for replay (free previous if exists)
          if (lastTtsBuffer) {
            free(lastTtsBuffer);
          }
          lastTtsBuffer = ttsOutputBuffer;
          lastTtsLength = ttsOutputIndex;
          lastTtsSampleRate = ttsSampleRate;
          lastTtsChannels = ttsChannels;
          
          // Reset working buffer pointers (don't free - now owned by lastTts)
          ttsOutputBuffer = nullptr;
          ttsOutputSize = 0;
          ttsOutputIndex = 0;
        } else {
          Serial.println("ERROR: No decoded audio to play");
        }
      } else {
        Serial.println("ERROR: Failed to allocate MP3 buffer");
        http.end();
      }
    } else {
      Serial.printf("ERROR: Invalid content length: %d\n", contentLength);
      http.end();
    }
  } else {
    Serial.println("ERROR: TTS request failed");
    Serial.println(http.getString());
    http.end();
  }
  
  Serial.println("=====================================\n");
}

// Replay last TTS audio (called on button C press)
void replayTts() {
  if (!lastTtsBuffer || lastTtsLength == 0) {
    Serial.println("No TTS audio to replay");
    return;
  }
  
  Serial.println("\n========== REPLAY TTS ==========");
  Serial.printf("Replaying %d samples at %dHz (%d channels)\n", lastTtsLength, lastTtsSampleRate, lastTtsChannels);
  
  // Initialize speaker
  M5.Speaker.end();
  delay(50);
  M5.Speaker.begin();
  M5.Speaker.setVolume(200);
  
  bool isStereo = (lastTtsChannels == 2);
  M5.Speaker.playRaw(lastTtsBuffer, lastTtsLength, lastTtsSampleRate, isStereo, 1, 0);
  
  // Wait for playback
  delay(100);
  while (M5.Speaker.isPlaying()) {
    delay(10);
  }
  delay(100);
  
  M5.Speaker.end();
  Serial.println("Replay complete");
  Serial.println("=================================\n");
}

// Real-time audio display
static volatile bool isRecording = false;
static volatile int currentRmsLevel = 0;
static volatile int recordingSecondsLeft = 0;
static TaskHandle_t displayTaskHandle = NULL;

// UUID v4 generator for message and session IDs
String generateUUID() {
  String uuid = "";
  const char* hex = "0123456789abcdef";
  
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      uuid += '-';
    } else if (i == 14) {
      uuid += '4'; // Version 4
    } else if (i == 19) {
      uuid += hex[(esp_random() & 0x3) | 0x8]; // Variant bits
    } else {
      uuid += hex[esp_random() & 0xF];
    }
  }
  
  return uuid;
}

// Get Unix timestamp in seconds
unsigned long getUnixTimestamp() {
  time_t now;
  time(&now);
  return (unsigned long)now;
}

// Get Unix timestamp in milliseconds
unsigned long long getUnixTimestampMs() {
  return (unsigned long long)getUnixTimestamp() * 1000;
}

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

// Display task runs on core 0, updates display in real-time
void displayTask(void *parameter) {
  while (true) {
    if (isRecording) {
      drawAudioLevel(recordingSecondsLeft, currentRmsLevel);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS); // ~20fps update rate
  }
}

// Track previous values to avoid unnecessary redraws
static int lastDisplayedSeconds = -1;
static int lastDisplayedBars = -1;
static bool lastSpeakingState = false;
static bool audioLevelInitialized = false;

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

bool recordAudio() {
  Serial.println("\n========== RECORDING ==========");

  if (!audioBuffer) {
    Serial.printf("Allocating buffer: %d samples, %d bytes\n", RECORD_SAMPLES,
                  RECORD_SAMPLES * sizeof(int16_t));
    audioBuffer = (int16_t *)malloc(RECORD_SAMPLES * sizeof(int16_t));
    if (!audioBuffer) {
      Serial.println("ERROR: Failed to allocate audio buffer!");
      return false;
    }
    Serial.println("Buffer allocated OK");
  } else {
    Serial.println("Using existing buffer");
  }

  Serial.println("Starting mic...");
  M5.Mic.begin();

  // Use smaller chunks for more responsive display (250ms chunks = 4 updates/sec)
  const int CHUNK_MS = 250;
  const int SAMPLES_PER_CHUNK = SAMPLE_RATE * CHUNK_MS / 1000;
  const int CHUNKS_PER_SECOND = 1000 / CHUNK_MS;
  
  int totalSamplesRecorded = 0;
  int silentChunks = 0;
  int silenceChunkThreshold = (int)(VAD_SILENCE_DURATION * CHUNKS_PER_SECOND);
  bool stoppedEarly = false;
  
  // Start display task for real-time updates
  isRecording = true;
  recordingSecondsLeft = RECORD_SECONDS;
  currentRmsLevel = 0;
  audioLevelInitialized = false; // Reset so display initializes fresh
  
  // Create display task on core 0 (main loop runs on core 1)
  if (displayTaskHandle == NULL) {
    xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, NULL, 1, &displayTaskHandle, 0);
  }
  
  int totalChunks = RECORD_SECONDS * CHUNKS_PER_SECOND;
  Serial.printf("Recording %d chunks of %dms each...\n", totalChunks, CHUNK_MS);
  
  for (int chunk = 0; chunk < totalChunks; chunk++) {
    int offset = totalSamplesRecorded;
    
    // Record one chunk
    M5.Mic.record(&audioBuffer[offset], SAMPLES_PER_CHUNK, SAMPLE_RATE);
    while (M5.Mic.isRecording()) {
      delay(1);
    }
    
    totalSamplesRecorded += SAMPLES_PER_CHUNK;
    
    // Calculate RMS for this chunk (skip first 2 chunks to ignore button click)
    if (chunk >= 2) {
      int64_t sum = 0;
      for (int i = 0; i < SAMPLES_PER_CHUNK; i++) {
        int16_t sample = audioBuffer[offset + i];
        sum += (int64_t)sample * sample;
      }
      currentRmsLevel = (int)sqrt(sum / SAMPLES_PER_CHUNK);
    }
    
    // Update countdown
    recordingSecondsLeft = RECORD_SECONDS - (chunk / CHUNKS_PER_SECOND);
    
    // Log every second
    if (chunk % CHUNKS_PER_SECOND == 0) {
      Serial.printf("Recording: %ds, RMS: %d\n", chunk / CHUNKS_PER_SECOND + 1, currentRmsLevel);
    }
    
    // VAD check after first second
    if (VAD_ENABLED && chunk >= CHUNKS_PER_SECOND) {
      if (currentRmsLevel < VAD_SILENCE_THRESHOLD) {
        silentChunks++;
        
        if (silentChunks >= silenceChunkThreshold) {
          Serial.println("Silence threshold - stopping");
          stoppedEarly = true;
          break;
        }
      } else {
        silentChunks = 0;
      }
    }
  }

  // Stop recording
  isRecording = false;
  M5.Mic.end();
  
  // Update actual recorded samples for transcription
  actualRecordedSamples = totalSamplesRecorded;
  
  if (stoppedEarly) {
    Serial.printf("Recording stopped early after %d samples (%.1fs)\n", 
                  totalSamplesRecorded, (float)totalSamplesRecorded / SAMPLE_RATE);
  } else {
    Serial.println("Recording complete");
  }

  // Audio stats
  int16_t minVal = 32767, maxVal = -32768;
  int64_t sum = 0;
  for (int i = 0; i < actualRecordedSamples; i++) {
    if (audioBuffer[i] < minVal)
      minVal = audioBuffer[i];
    if (audioBuffer[i] > maxVal)
      maxVal = audioBuffer[i];
    sum += abs(audioBuffer[i]);
  }
  Serial.printf("Audio stats: min=%d, max=%d, avg=%lld\n", minVal, maxVal,
                sum / actualRecordedSamples);
  Serial.println("================================\n");

  return true;
}

void createWavHeader(uint8_t *header, int dataSize) {
  int fileSize = dataSize + 36;
  int byteRate = SAMPLE_RATE * 1 * 16 / 8;
  int blockAlign = 1 * 16 / 8;

  memcpy(header, "RIFF", 4);
  header[4] = fileSize & 0xFF;
  header[5] = (fileSize >> 8) & 0xFF;
  header[6] = (fileSize >> 16) & 0xFF;
  header[7] = (fileSize >> 24) & 0xFF;
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  header[16] = 16;
  header[17] = 0;
  header[18] = 0;
  header[19] = 0;
  header[20] = 1;
  header[21] = 0;
  header[22] = 1;
  header[23] = 0;
  header[24] = SAMPLE_RATE & 0xFF;
  header[25] = (SAMPLE_RATE >> 8) & 0xFF;
  header[26] = (SAMPLE_RATE >> 16) & 0xFF;
  header[27] = (SAMPLE_RATE >> 24) & 0xFF;
  header[28] = byteRate & 0xFF;
  header[29] = (byteRate >> 8) & 0xFF;
  header[30] = (byteRate >> 16) & 0xFF;
  header[31] = (byteRate >> 24) & 0xFF;
  header[32] = blockAlign;
  header[33] = 0;
  header[34] = 16;
  header[35] = 0;
  memcpy(header + 36, "data", 4);
  header[40] = dataSize & 0xFF;
  header[41] = (dataSize >> 8) & 0xFF;
  header[42] = (dataSize >> 16) & 0xFF;
  header[43] = (dataSize >> 24) & 0xFF;
}

String transcribeAudio() {
  Serial.println("\n========== TRANSCRIBING ==========");

  int audioDataSize = actualRecordedSamples * sizeof(int16_t);
  uint8_t wavHeader[44];
  createWavHeader(wavHeader, audioDataSize);

  String boundary = "----ESP32Boundary";
  String response;

  if (USE_OWUI_STT) {
    // Use OpenWebUI's transcription endpoint via HTTPClient
    Serial.println("Using OpenWebUI STT endpoint");
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    
    String sttUrl = String(OWUI_BASE_URL) + "/api/v1/audio/transcriptions";
    Serial.printf("STT URL: %s\n", sttUrl.c_str());
    
    http.begin(client, sttUrl);
    http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
    http.setTimeout(60000);
    
    // Build multipart body - OpenWebUI uses "file" and optional "language"
    String bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
    bodyStart += "Content-Type: audio/wav\r\n\r\n";
    
    String bodyEnd = "\r\n--" + boundary + "--\r\n";
    
    int contentLength = bodyStart.length() + 44 + audioDataSize + bodyEnd.length();
    Serial.printf("Content length: %d bytes (audio: %d bytes)\n", contentLength, audioDataSize);
    
    // Create full body buffer
    uint8_t* fullBody = (uint8_t*)malloc(contentLength);
    if (!fullBody) {
      Serial.println("ERROR: Failed to allocate body buffer");
      return "Memory error";
    }
    
    int offset = 0;
    memcpy(fullBody + offset, bodyStart.c_str(), bodyStart.length());
    offset += bodyStart.length();
    memcpy(fullBody + offset, wavHeader, 44);
    offset += 44;
    memcpy(fullBody + offset, audioBuffer, audioDataSize);
    offset += audioDataSize;
    memcpy(fullBody + offset, bodyEnd.c_str(), bodyEnd.length());
    
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    
    Serial.println("Sending audio to OpenWebUI...");
    int httpCode = http.POST(fullBody, contentLength);
    free(fullBody);
    
    Serial.printf("HTTP response code: %d\n", httpCode);
    
    if (httpCode == 200) {
      response = http.getString();
    } else {
      Serial.println("ERROR: STT request failed");
      Serial.println(http.getString());
      http.end();
      return "STT failed";
    }
    http.end();
    
  } else {
    // Use OpenAI Whisper endpoint via raw socket (original implementation)
    Serial.println("Using OpenAI Whisper endpoint");
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60);

    Serial.printf("Connecting to %s:%d...\n", STT_HOST, STT_PORT);
    if (!client.connect(STT_HOST, STT_PORT)) {
      Serial.println("ERROR: Connection failed!");
      return "Connection failed";
    }
    Serial.println("Connected");

    String bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"file\"; "
                 "filename=\"audio.wav\"\r\n";
    bodyStart += "Content-Type: audio/wav\r\n\r\n";

    String bodyEnd = "\r\n--" + boundary + "\r\n";
    bodyEnd += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
    bodyEnd += String(STT_MODEL) + "\r\n";
    bodyEnd += "--" + boundary + "--\r\n";

    int contentLength =
        bodyStart.length() + 44 + audioDataSize + bodyEnd.length();
    Serial.printf("Content length: %d bytes (audio: %d bytes)\n", contentLength,
                  audioDataSize);

    Serial.println("Sending request headers...");
    client.print(String("POST ") + STT_PATH + " HTTP/1.1\r\n");
    client.print(String("Host: ") + STT_HOST + "\r\n");
    client.print("Authorization: Bearer " + String(STT_API_KEY) + "\r\n");
    client.print("Content-Type: multipart/form-data; boundary=" + boundary +
                 "\r\n");
    client.print("Content-Length: " + String(contentLength) + "\r\n");
    client.print("Connection: close\r\n\r\n");

    Serial.println("Sending audio data...");
    client.print(bodyStart);
    client.write(wavHeader, 44);

    // Send in chunks to avoid watchdog and network buffer issues
    int chunkSize = 1024;
    uint8_t *ptr = (uint8_t *)audioBuffer;
    int remaining = audioDataSize;
    int sent = 0;

    while (remaining > 0) {
      if (!client.connected()) {
        Serial.println("ERROR: Connection lost during upload");
        return "Connection lost";
      }

      int toSend = min(chunkSize, remaining);
      int written = client.write(ptr, toSend);

      if (written == 0) {
        Serial.println("WARNING: 0 bytes written, retrying...");
        delay(100);
        if (!client.connected())
          break;
        continue;
      }

      ptr += written;
      remaining -= written;
      sent += written;

      if (sent % 8192 == 0) {
        Serial.printf("  Sent %d / %d bytes\n", sent, audioDataSize);
      }
      delay(2); // Small delay to let network stack process
    }

    client.print(bodyEnd);
    Serial.println("Request sent, waiting for response...");

    unsigned long timeout = millis();
    while (!client.available()) {
      if (millis() - timeout > 60000) {
        Serial.println("ERROR: Timeout waiting for response!");
        client.stop();
        return "Timeout";
      }
      delay(100);
    }

    Serial.println("Response received, reading headers...");
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      Serial.println("  " + line);
      if (line == "\r")
        break;
    }

    response = client.readString();
    client.stop();
  }

  Serial.println("STT response body:");
  Serial.println(response);

  // Parse response - both OpenAI and OpenWebUI return {"text": "..."}
  int textIdx = response.indexOf("\"text\"");
  if (textIdx < 0) {
    Serial.println("ERROR: No 'text' field in response!");
    return "No transcription";
  }

  int start = response.indexOf('"', textIdx + 6);
  if (start < 0) {
    Serial.println("ERROR: Parse error!");
    return "Parse error";
  }
  start++;

  String result;
  bool escaped = false;
  for (unsigned int i = start; i < response.length(); i++) {
    char c = response[i];
    if (escaped) {
      result += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      break;
    } else {
      result += c;
    }
  }

  Serial.println("Transcription: " + result);
  Serial.println("==================================\n");

  return result;
}

String createChatSession(const String &title) {
  Serial.println("\n========== CREATE CHAT SESSION ==========");
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = String(OWUI_BASE_URL) + "/api/v1/chats/new";
  Serial.printf("Creating chat at: %s\n", url.c_str());
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(30000);
  
  // OpenWebUI /api/v1/chats/new body format with history and timestamp
  unsigned long long timestamp = getUnixTimestampMs(); // Milliseconds for chat creation
  String body = "{"
                "\"chat\":{"
                "\"title\":\"" + title + "\","
                "\"models\":[\"" + String(LLM_MODEL) + "\"],"
                "\"timestamp\":" + String((unsigned long)timestamp) + ","
                "\"history\":{"
                "\"messages\":{},"
                "\"currentId\":null"
                "}"
                "}"
                "}";
  
  Serial.println("Request body: " + body);
  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  // Always read the response body
  String resp = http.getString();
  http.end();
  
  Serial.println("Full response:");
  Serial.println(resp);
  Serial.printf("Response length: %d bytes\n", resp.length());
  
  if (httpCode < 200 || httpCode >= 300) {
    Serial.println("ERROR: Non-2xx response code");
    return "";
  }
  
  // Parse chat ID from response
  int idIdx = resp.indexOf("\"id\"");
  if (idIdx < 0) {
    Serial.println("ERROR: No 'id' field found in response!");
    Serial.println("Searching for alternative fields...");
    
    // Try looking for chat_id or chatId
    idIdx = resp.indexOf("\"chat_id\"");
    if (idIdx < 0) idIdx = resp.indexOf("\"chatId\"");
    if (idIdx < 0) {
      Serial.println("ERROR: Could not find any ID field in response!");
      return "";
    }
    Serial.println("Found alternative ID field");
  }
  
  int start = resp.indexOf('"', idIdx + 4);
  if (start < 0) {
    Serial.println("ERROR: Could not find opening quote for ID value");
    return "";
  }
  start++;
  
  String chatId;
  for (unsigned int i = start; i < resp.length(); i++) {
    if (resp[i] == '"') break;
    chatId += resp[i];
  }
  
  if (chatId.length() == 0) {
    Serial.println("ERROR: Parsed chat ID is empty!");
    return "";
  }
  
  Serial.println("Chat ID: " + chatId);
  Serial.printf("Chat ID length: %d\n", chatId.length());
  Serial.println("=========================================\n");
  
  return chatId;
}

// Step 3: Update chat with user message
bool updateChatWithUserMessage(const String &chatId, const String &userMsgId, const String &userContent) {
  Serial.println("\n========== UPDATE CHAT WITH USER MESSAGE ==========");
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = String(OWUI_BASE_URL) + "/api/v1/chats/" + chatId;
  
  // First, fetch existing chat to get current history
  Serial.println("Fetching existing chat history...");
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  
  int getCode = http.GET();
  String existingChat = http.getString();
  http.end();
  
  if (getCode == 401 || getCode == 404) {
    Serial.println("Chat session no longer exists (deleted or invalid)");
    Serial.println("===================================================\n");
    currentChatId = "";
    return false;
  }
  
  if (getCode != 200) {
    Serial.printf("ERROR: Failed to fetch chat (HTTP %d)\n", getCode);
    Serial.println("===================================================\n");
    return false;
  }
  
  // Extract currentId (last message ID) from history
  String previousMsgId = "";
  int historyIdx = existingChat.indexOf("\"history\"");
  if (historyIdx >= 0) {
    int currentIdIdx = existingChat.indexOf("\"currentId\"", historyIdx);
    if (currentIdIdx >= 0) {
      // Check if currentId is null (JSON null, not quoted string)
      int colonIdx = existingChat.indexOf(':', currentIdIdx);
      if (colonIdx >= 0) {
        // Skip whitespace after colon
        int checkIdx = colonIdx + 1;
        while (checkIdx < (int)existingChat.length() && 
               (existingChat[checkIdx] == ' ' || existingChat[checkIdx] == '\t')) {
          checkIdx++;
        }
        // Check if it's null (not quoted)
        if (existingChat.substring(checkIdx, checkIdx + 4) != "null") {
          // It's a quoted string, extract it
          int start = existingChat.indexOf('"', currentIdIdx + 12);
          if (start >= 0) {
            start++;
            int end = existingChat.indexOf('"', start);
            if (end >= 0) {
              previousMsgId = existingChat.substring(start, end);
            }
          }
        }
      }
    }
  }
  
  Serial.printf("Previous message ID: %s\n", previousMsgId.length() > 0 ? previousMsgId.c_str() : "none (new chat)");
  
  // Extract existing messages from history.messages
  String existingMessages = "";
  if (historyIdx >= 0) {
    int messagesIdx = existingChat.indexOf("\"messages\"", historyIdx);
    if (messagesIdx >= 0) {
      int openBrace = existingChat.indexOf('{', messagesIdx + 10);
      if (openBrace >= 0) {
        int braceCount = 1;
        int start = openBrace + 1;
        for (unsigned int i = start; i < existingChat.length(); i++) {
          if (existingChat[i] == '{') braceCount++;
          else if (existingChat[i] == '}') {
            braceCount--;
            if (braceCount == 0) {
              existingMessages = existingChat.substring(start, i);
              break;
            }
          }
        }
      }
    }
  }
  
  Serial.printf("Existing messages length: %d\n", existingMessages.length());
  
  // Update previous message to add new message as child
  if (previousMsgId.length() > 0 && existingMessages.length() > 0) {
    // Find and update the previous message's childrenIds
    int prevMsgIdx = existingMessages.indexOf("\"" + previousMsgId + "\"");
    if (prevMsgIdx >= 0) {
      int childrenIdx = existingMessages.indexOf("\"childrenIds\"", prevMsgIdx);
      if (childrenIdx >= 0) {
        int arrayStart = existingMessages.indexOf('[', childrenIdx);
        int arrayEnd = existingMessages.indexOf(']', arrayStart);
        if (arrayStart >= 0 && arrayEnd >= 0) {
          String existingChildren = existingMessages.substring(arrayStart + 1, arrayEnd);
          String newChildren = existingChildren;
          if (newChildren.length() > 0) {
            newChildren += ",";
          }
          newChildren += "\"" + userMsgId + "\"";
          
          // Replace the childrenIds array
          existingMessages = existingMessages.substring(0, arrayStart + 1) + 
                           newChildren + 
                           existingMessages.substring(arrayEnd);
        }
      }
    }
  }
  
  // Now update with new user message appended to existing
  Serial.printf("Updating chat at: %s\n", url.c_str());
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(30000);
  
  unsigned long timestamp = getUnixTimestamp();
  
  String escapedContent = userContent;
  escapedContent.replace("\\", "\\\\");
  escapedContent.replace("\"", "\\\"");
  escapedContent.replace("\n", "\\n");
  
  // Build new message entry with proper parent linking
  String parentIdField = previousMsgId.length() > 0 ? 
                        ("\"" + previousMsgId + "\"") : "null";
  
  String newMessage = "\"" + userMsgId + "\":{"
                      "\"id\":\"" + userMsgId + "\","
                      "\"parentId\":" + parentIdField + ","
                      "\"childrenIds\":[],"
                      "\"role\":\"user\","
                      "\"content\":\"" + escapedContent + "\","
                      "\"timestamp\":" + String(timestamp) + ","
                      "\"models\":[\"" + String(LLM_MODEL) + "\"]"
                      "}";
  
  // Merge existing and new messages
  String allMessages = existingMessages;
  if (allMessages.length() > 0) {
    allMessages += ",";
  }
  allMessages += newMessage;
  
  String body = "{"
                "\"chat\":{"
                "\"title\":\"M5 Voice Assistant\","
                "\"history\":{"
                "\"messages\":{"
                + allMessages +
                "},"
                "\"currentId\":\"" + userMsgId + "\""
                "},"
                "\"messages\":["
                "{"
                "\"id\":\"" + userMsgId + "\","
                "\"role\":\"user\","
                "\"content\":\"" + escapedContent + "\""
                "}"
                "]"
                "}"
                "}";
  
  Serial.println("Updating with user message...");
  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  String resp = http.getString();
  http.end();
  
  if (httpCode >= 200 && httpCode < 300) {
    Serial.println("User message saved successfully");
    Serial.println("===================================================\n");
    return true;
  } else if (httpCode == 401 || httpCode == 404) {
    Serial.println("Chat session no longer exists (deleted or invalid)");
    Serial.println("===================================================\n");
    currentChatId = "";
    return false;
  } else {
    Serial.println("Error saving user message:");
    Serial.println(resp);
    Serial.println("===================================================\n");
    return false;
  }
}

// Step 5: Call completed handler
bool chatCompleted(const String &chatId, const String &sessionId, const String &userMsgId, 
                   const String &userContent, const String &assistantMsgId, const String &assistantContent) {
  Serial.println("\n========== CHAT COMPLETED ==========");
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = String(OWUI_BASE_URL) + "/api/chat/completed";
  Serial.printf("Calling completed at: %s\n", url.c_str());
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(30000);
  
  String escapedUser = userContent;
  escapedUser.replace("\\", "\\\\");
  escapedUser.replace("\"", "\\\"");
  escapedUser.replace("\n", "\\n");
  
  String escapedAssistant = assistantContent;
  escapedAssistant.replace("\\", "\\\\");
  escapedAssistant.replace("\"", "\\\"");
  escapedAssistant.replace("\n", "\\n");
  
  String body = "{"
                "\"model\":\"" + String(LLM_MODEL) + "\","
                "\"messages\":["
                "{"
                "\"id\":\"" + userMsgId + "\","
                "\"role\":\"user\","
                "\"content\":\"" + escapedUser + "\""
                "},"
                "{"
                "\"id\":\"" + assistantMsgId + "\","
                "\"role\":\"assistant\","
                "\"content\":\"" + escapedAssistant + "\""
                "}"
                "],"
                "\"chat_id\":\"" + chatId + "\","
                "\"session_id\":\"" + sessionId + "\","
                "\"id\":\"" + assistantMsgId + "\""
                "}";
  
  Serial.println("Calling completed handler...");
  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  String resp = http.getString();
  http.end();
  
  if (httpCode >= 200 && httpCode < 300) {
    Serial.println("Completed handler called successfully");
    Serial.println("====================================\n");
    return true;
  } else {
    Serial.println("Error calling completed:");
    Serial.println(resp);
    Serial.println("====================================\n");
    return false;
  }
}

// Step 6: Update chat with full conversation history
bool saveChatHistory(const String &chatId, const String &userMsgId, const String &userContent, 
                     const String &assistantMsgId, const String &assistantContent) {
  Serial.println("\n========== SAVING CHAT HISTORY ==========");
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = String(OWUI_BASE_URL) + "/api/v1/chats/" + chatId;
  
  // Fetch existing chat history first
  Serial.println("Fetching existing chat history...");
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  
  int getCode = http.GET();
  String existingChat = http.getString();
  http.end();
  
  if (getCode != 200) {
    Serial.printf("ERROR: Failed to fetch chat (HTTP %d)\n", getCode);
    Serial.println("=========================================\n");
    return false;
  }
  
  // Extract existing messages from history.messages
  String existingMessages = "";
  int historyIdx = existingChat.indexOf("\"history\"");
  if (historyIdx >= 0) {
    int messagesIdx = existingChat.indexOf("\"messages\"", historyIdx);
    if (messagesIdx >= 0) {
      int openBrace = existingChat.indexOf('{', messagesIdx + 10);
      if (openBrace >= 0) {
        int braceCount = 1;
        int start = openBrace + 1;
        for (unsigned int i = start; i < existingChat.length(); i++) {
          if (existingChat[i] == '{') braceCount++;
          else if (existingChat[i] == '}') {
            braceCount--;
            if (braceCount == 0) {
              existingMessages = existingChat.substring(start, i);
              break;
            }
          }
        }
      }
    }
  }
  
  Serial.printf("Existing messages length: %d\n", existingMessages.length());
  
  // Update the user message to add assistant as child
  if (existingMessages.length() > 0) {
    int userMsgIdx = existingMessages.indexOf("\"" + userMsgId + "\"");
    if (userMsgIdx >= 0) {
      int childrenIdx = existingMessages.indexOf("\"childrenIds\"", userMsgIdx);
      if (childrenIdx >= 0) {
        int arrayStart = existingMessages.indexOf('[', childrenIdx);
        int arrayEnd = existingMessages.indexOf(']', arrayStart);
        if (arrayStart >= 0 && arrayEnd >= 0) {
          String existingChildren = existingMessages.substring(arrayStart + 1, arrayEnd);
          String newChildren = existingChildren;
          if (newChildren.length() > 0 && !newChildren.equals("")) {
            newChildren += ",";
          }
          newChildren += "\"" + assistantMsgId + "\"";
          
          // Replace the childrenIds array
          existingMessages = existingMessages.substring(0, arrayStart + 1) + 
                           newChildren + 
                           existingMessages.substring(arrayEnd);
        }
      }
    }
  }
  
  // Get current timestamp
  unsigned long timestamp = getUnixTimestamp();
  
  // Escape assistant content for JSON
  String escapedAssistant = assistantContent;
  escapedAssistant.replace("\\", "\\\\");
  escapedAssistant.replace("\"", "\\\"");
  escapedAssistant.replace("\n", "\\n");
  
  // Build only the assistant message (user message already exists)
  String newMessage = "\"" + assistantMsgId + "\":{"
                      "\"id\":\"" + assistantMsgId + "\","
                      "\"parentId\":\"" + userMsgId + "\","
                      "\"childrenIds\":[],"
                      "\"role\":\"assistant\","
                      "\"content\":\"" + escapedAssistant + "\","
                      "\"model\":\"" + String(LLM_MODEL) + "\","
                      "\"timestamp\":" + String(timestamp) + ","
                      "\"done\":true"
                      "}";
  
  // Merge existing and new assistant message
  String allMessages = existingMessages;
  if (allMessages.length() > 0) {
    allMessages += ",";
  }
  allMessages += newMessage;
  
  // Escape user content for messages array
  String escapedUser = userContent;
  escapedUser.replace("\\", "\\\\");
  escapedUser.replace("\"", "\\\"");
  escapedUser.replace("\n", "\\n");
  
  String body = "{"
                "\"chat\":{"
                "\"title\":\"M5 Voice Assistant\","
                "\"history\":{"
                "\"messages\":{"
                + allMessages +
                "},"
                "\"currentId\":\"" + assistantMsgId + "\""
                "},"
                "\"messages\":["
                "{"
                "\"id\":\"" + userMsgId + "\","
                "\"role\":\"user\","
                "\"content\":\"" + escapedUser + "\""
                "},"
                "{"
                "\"id\":\"" + assistantMsgId + "\","
                "\"role\":\"assistant\","
                "\"content\":\"" + escapedAssistant + "\""
                "}"
                "]"
                "}"
                "}";
  
  Serial.println("Saving history...");
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(30000);
  
  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  String resp = http.getString();
  http.end();
  
  if (httpCode >= 200 && httpCode < 300) {
    Serial.println("Chat history saved successfully");
    Serial.println("=========================================\n");
    return true;
  } else {
    Serial.println("Error saving chat history:");
    Serial.println(resp);
    Serial.println("=========================================\n");
    return false;
  }
}

String askGPT(const String &question) {
  Serial.println("\n========== ASKING LLM ==========");
  Serial.println("Question: " + question);

  // Step 1: Create or reuse chat session for OpenWebUI
  if (USE_OWUI_SESSIONS && currentChatId.length() == 0) {
    currentChatId = createChatSession("M5 Voice Assistant");
    currentSessionId = generateUUID(); // Generate session ID once per chat
    if (currentChatId.length() == 0) {
      Serial.println("ERROR: Failed to create chat session!");
      return "Session error";
    }
  }

  // Step 2: Generate message IDs for OpenWebUI
  String userMsgId = "";
  String assistantMsgId = "";
  if (USE_OWUI_SESSIONS) {
    userMsgId = generateUUID();
    assistantMsgId = generateUUID();
    Serial.println("User message ID: " + userMsgId);
    Serial.println("Assistant message ID: " + assistantMsgId);
    
    // Step 3: Update chat with user message before completion
    if (!updateChatWithUserMessage(currentChatId, userMsgId, question)) {
      // Check if chat was deleted (currentChatId cleared by updateChatWithUserMessage)
      if (currentChatId.length() == 0) {
        Serial.println("Chat was deleted, creating new session and retrying...");
        currentChatId = createChatSession("M5 Voice Assistant");
        currentSessionId = generateUUID();
        if (currentChatId.length() > 0) {
          // Retry with new session
          if (!updateChatWithUserMessage(currentChatId, userMsgId, question)) {
            Serial.println("ERROR: Failed to update chat even after recreating!");
            return "Update error";
          }
        } else {
          Serial.println("ERROR: Failed to recreate chat session!");
          return "Session error";
        }
      } else {
        Serial.println("ERROR: Failed to update chat with user message!");
        return "Update error";
      }
    }
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  // Build messages array with full conversation history for context
  String messagesArray = "";
  if (USE_OWUI_SESSIONS) {
    // Fetch chat history to get all messages including current user message
    // (already saved by updateChatWithUserMessage)
    String chatUrl = String(OWUI_BASE_URL) + "/api/v1/chats/" + currentChatId;
    Serial.println("Fetching chat history for context...");
    
    http.begin(client, chatUrl);
    http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
    
    int getCode = http.GET();
    String chatData = http.getString();
    http.end();
    
    if (getCode == 200) {
      // Parse messages from history and build messages array
      // Find history.messages object
      int historyIdx = chatData.indexOf("\"history\"");
      if (historyIdx >= 0) {
        int messagesIdx = chatData.indexOf("\"messages\"", historyIdx);
        if (messagesIdx >= 0) {
          // Extract each message ID and build in chronological order
          // For simplicity, we'll traverse the tree from root to leaves
          int msgStart = messagesIdx;
          while (true) {
            // Find next message with role "user" or "assistant"
            int roleIdx = chatData.indexOf("\"role\":", msgStart);
            if (roleIdx < 0 || roleIdx > chatData.indexOf("\"currentId\"", historyIdx)) break;
            
            int roleStart = chatData.indexOf('"', roleIdx + 7);
            if (roleStart < 0) break;
            roleStart++;
            int roleEnd = chatData.indexOf('"', roleStart);
            String role = chatData.substring(roleStart, roleEnd);
            
            // Get content
            int contentIdx = chatData.indexOf("\"content\":", roleIdx);
            if (contentIdx > 0) {
              int contentStart = chatData.indexOf('"', contentIdx + 10);
              if (contentStart >= 0) {
                contentStart++;
                String content = "";
                bool esc = false;
                for (unsigned int i = contentStart; i < chatData.length(); i++) {
                  char c = chatData[i];
                  if (esc) {
                    if (c == 'n') content += '\n';
                    else if (c == 't') content += '\t';
                    else content += c;
                    esc = false;
                  } else if (c == '\\') {
                    esc = true;
                  } else if (c == '"') {
                    break;
                  } else {
                    content += c;
                  }
                }
                
                // Escape for JSON
                content.replace("\\", "\\\\");
                content.replace("\"", "\\\"");
                content.replace("\n", "\\n");
                
                // Add system prompt only to the last user message
                if (role == "user") {
                  // Check if this is the last message by looking ahead
                  int nextRoleIdx = chatData.indexOf("\"role\":", roleIdx + 1);
                  bool isLastUserMsg = (nextRoleIdx < 0 || nextRoleIdx > chatData.indexOf("\"currentId\"", historyIdx));
                  if (isLastUserMsg) {
                    content += systemPrompt;
                  }
                }
                
                // Add to messages array
                if (messagesArray.length() > 0) messagesArray += ",";
                messagesArray += "{\"role\":\"" + role + "\",\"content\":\"" + content + "\"}";
              }
            }
            
            msgStart = roleIdx + 1;
          }
        }
      }
      Serial.printf("Built context with history messages\n");
    }
  } else {
    // Non-OpenWebUI: build single message manually
    String escaped = question;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    escaped.replace("\n", " ");
    
    messagesArray = "{\"role\":\"user\",\"content\":\"" + escaped + systemPrompt + "\"}";
  }

  // Escape question for non-OpenWebUI API formats
  String escaped = question;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", " ");

  String url;
  if (USE_OWUI_SESSIONS) {
    url = String(OWUI_BASE_URL) + "/api/v1/chat/completions";
  } else {
    url = LLM_URL;
  }
  
  Serial.printf("Connecting to LLM at %s...\n", url.c_str());
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(90000);

  String body;
  if (LLM_USE_RESPONSES_API) {
    // OpenAI Responses API format
    body = "{"
           "\"model\":\"" + String(LLM_MODEL) + "\","
           "\"input\":["
           "{"
           "\"role\":\"user\","
           "\"content\":["
           "{"
           "\"type\":\"input_text\","
           "\"text\":\"" +
           escaped +
           systemPrompt + "\""
           "}"
           "]"
           "}"
           "]"
           "}";
  } else if (USE_OWUI_SESSIONS) {
    // Step 4: OpenWebUI with chat session tracking and full context
    body = "{"
           "\"model\":\"" + String(LLM_MODEL) + "\","
           "\"messages\":[" + messagesArray + "],"
           "\"chat_id\":\"" + currentChatId + "\","
           "\"id\":\"" + assistantMsgId + "\","
           "\"session_id\":\"" + currentSessionId + "\","
           "\"stream\":true"
           "}";
  } else {
    // Standard Chat Completions API format
    body = "{"
           "\"model\":\"" + String(LLM_MODEL) + "\","
           "\"messages\":["
           "{"
           "\"role\":\"user\","
           "\"content\":\"" +
           escaped +
           systemPrompt + "\""
           "}"
           "]"
           "}";
  }

  Serial.println("Sending request...");
  Serial.println("Body: " + body);

  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);

  if (httpCode != 200) {
    String error = http.getString();
    Serial.println("Error response: " + error);
    http.end();
    return "HTTP " + String(httpCode);
  }

  // Handle async completion for OpenWebUI (uses WebSocket, not HTTP streaming)
  String result = "";
  if (USE_OWUI_SESSIONS) {
    String resp = http.getString();
    http.end();
    
    Serial.println("Task initiated:");
    Serial.println(resp);
    
    // OpenWebUI returns task_id and streams via WebSocket
    // Poll chat history until assistant response appears
    Serial.println("Polling chat history for completion (OpenWebUI uses WebSocket streaming)...");
    
    HTTPClient fetchHttp;
    WiFiClientSecure fetchClient;
    fetchClient.setInsecure();
    
    String fetchUrl = String(OWUI_BASE_URL) + "/api/v1/chats/" + currentChatId;
    unsigned long pollStart = millis();
    int pollAttempt = 0;
    
    while (millis() - pollStart < 60000) { // 60 second timeout
      pollAttempt++;
      delay(1000); // Poll every 1 second
      
      Serial.printf("[POLL #%d] Fetching chat history...\n", pollAttempt);
      
      fetchHttp.begin(fetchClient, fetchUrl);
      fetchHttp.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
      
      int fetchCode = fetchHttp.GET();
      String chatData = fetchHttp.getString();
      fetchHttp.end();
      
      if (fetchCode == 200) {
        // Check if assistant message exists in history
        int msgIdx = chatData.indexOf("\"" + assistantMsgId + "\"");
        
        if (msgIdx >= 0) {
          Serial.printf("Found assistant message after %d polls\n", pollAttempt);
          
          // In OpenWebUI JSON, structure is: "role":"assistant",...,"content":"...",...,"id":"xxx"
          // So we need to search BACKWARDS from msgIdx for role, or find content after msgIdx
          // Simpler: just find content after the ID
          int contentIdx = chatData.indexOf("\"content\"", msgIdx);
          if (contentIdx < 0) {
            // Content might be before ID in the JSON object, search backwards
            // Look for content in the 500 chars before msgIdx
            int searchStart = (msgIdx > 500) ? msgIdx - 500 : 0;
            String searchBlock = chatData.substring(searchStart, msgIdx);
            int relContentIdx = searchBlock.lastIndexOf("\"content\"");
            if (relContentIdx >= 0) {
              contentIdx = searchStart + relContentIdx;
            }
          }
          
          if (contentIdx >= 0) {
            int start = chatData.indexOf('"', contentIdx + 9);
            if (start >= 0) {
              start++;
              bool esc = false;
              for (unsigned int i = start; i < chatData.length(); i++) {
                char c = chatData[i];
                if (esc) {
                  if (c == 'n') result += '\n';
                  else if (c == 't') result += '\t';
                  else result += c;
                  esc = false;
                } else if (c == '\\') {
                  esc = true;
                } else if (c == '"') {
                  break;
                } else {
                  result += c;
                }
              }
            }
            
            if (result.length() > 0) {
              Serial.printf("Retrieved response length: %d\n", result.length());
              Serial.printf("First 50 chars: %.50s\n", result.c_str());
              
              // Check if this is an echo of the user's question (without system prompt suffix)
              // Extract just the question part (before " Answer in")
              String questionOnly = question;
              int answerIdx = question.indexOf(" Answer in");
              if (answerIdx > 0) {
                questionOnly = question.substring(0, answerIdx);
              }
              
              if (result == questionOnly || result == question) {
                Serial.println("WARNING: Got echo of question, waiting for real response...");
                result = "";
                // Keep polling
              } else {
                break;
              }
            } else {
              Serial.println("Content empty, waiting...");
            }
          } else {
            Serial.println("Content field not found near message ID");
          }
        } else {
          Serial.println("Assistant message not ready yet...");
        }
      } else {
        Serial.printf("Fetch failed: HTTP %d\n", fetchCode);
      }
    }
    
    if (result.length() == 0) {
      Serial.printf("ERROR: No response after %d polls\n", pollAttempt);
      return "Timeout";
    }
  } else {
    // Non-streaming response for OpenAI APIs
    String resp = http.getString();
    http.end();
    
    Serial.println("LLM response:");
    Serial.println(resp);
  
    if (LLM_USE_RESPONSES_API) {
    // Parse OpenAI Responses API format
    int outIdx = resp.indexOf("output_text");
    if (outIdx < 0) {
      Serial.println("ERROR: No 'output_text' in response!");
      return "No output";
    }

    int textKey = resp.indexOf("\"text\"", outIdx);
    if (textKey < 0) {
      Serial.println("ERROR: No 'text' field!");
      return "No text";
    }

    int start = resp.indexOf('"', textKey + 6);
    if (start < 0) {
      Serial.println("ERROR: Parse error!");
      return "Parse error";
    }
    start++;

    bool esc = false;
    for (unsigned int i = start; i < resp.length(); i++) {
      char c = resp[i];
      if (esc) {
        if (c == 'n')
          result += '\n';
        else if (c == 'u') {
          i += 4;
          result += '-';
        } else
          result += c;
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        break;
      } else {
        result += c;
      }
    }
  } else {
    // Parse standard Chat Completions API format
    int contentIdx = resp.indexOf("\"content\"");
    if (contentIdx < 0) {
      Serial.println("ERROR: No 'content' in response!");
      return "No content";
    }

    int start = resp.indexOf('"', contentIdx + 9);
    if (start < 0) {
      Serial.println("ERROR: Parse error!");
      return "Parse error";
    }
    start++;

    bool esc = false;
    for (unsigned int i = start; i < resp.length(); i++) {
      char c = resp[i];
      if (esc) {
        if (c == 'n')
          result += '\n';
        else if (c == 'u') {
          i += 4;
          result += '-';
        } else
          result += c;
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        break;
      } else {
        result += c;
      }
    }
    }
  }

  Serial.println("Extracted answer: " + result);
  Serial.println("=================================\n");

  // Steps 5 & 6: Call completed handler and save full chat history for OpenWebUI
  if (USE_OWUI_SESSIONS && currentChatId.length() > 0 && userMsgId.length() > 0) {
    // Step 5: Call completed handler
    chatCompleted(currentChatId, currentSessionId, userMsgId, question, assistantMsgId, result);
    
    // Step 6: Save full conversation history
    saveChatHistory(currentChatId, userMsgId, question, assistantMsgId, result);
  }
  
  return result;
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n========================================");
  Serial.println("         M5 Voice Assistant");
  Serial.println("========================================\n");

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  // Get actual display dimensions (works for StickC Plus 2, Core 2, etc.)
  WIDTH = M5.Display.width();
  HEIGHT = M5.Display.height();
  Serial.printf("Display: %dx%d\n", WIDTH, HEIGHT);

  // Detect device type based on display size
  // Core2/CoreS3: 320x240, StickC Plus2: 240x135 (rotated)
  size_t freeHeap = ESP.getFreeHeap();
  Serial.printf("Free heap at startup: %d bytes\n", freeHeap);
  
  // Use display size as primary indicator (more reliable than heap)
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
  Serial.printf("Max words: %d\n", isLargeDevice ? LLM_MAX_WORDS_LARGE : LLM_MAX_WORDS_SMALL);
  
  // Apply default profile and build system prompt
  applyAudioProfile(0);
  buildSystemPrompt();
  Serial.printf("Free heap: %d bytes\n", freeHeap);

  drawScreen("Connecting...");

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempt++;
    if (attempt % 10 == 0) {
      Serial.printf("\nWiFi Status: %d\n", WiFi.status());
      // 255: WL_NO_SHIELD, 0: WL_IDLE_STATUS, 1: WL_NO_SSID_AVAIL
      // 2: WL_SCAN_COMPLETED, 3: WL_CONNECTED, 4: WL_CONNECT_FAILED
      // 5: WL_CONNECTION_LOST, 6: WL_DISCONNECTED
    }
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  // Sync time with NTP for accurate Unix timestamps
  Serial.println("Syncing time with NTP...");
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
  int ntpRetry = 0;
  while (time(nullptr) < 100000 && ntpRetry < 20) {
    delay(500);
    Serial.print(".");
    ntpRetry++;
  }
  Serial.println();
  if (time(nullptr) > 100000) {
    Serial.printf("Time synced: %lu\n", getUnixTimestamp());
  } else {
    Serial.println("WARNING: NTP sync failed, timestamps will be inaccurate");
  }

  // Audio buffer will be allocated on first recording based on profile
  Serial.println("Audio buffer will be allocated on first recording");

  // Show appropriate prompt based on device capabilities
  const AudioProfile& profile = deviceProfiles[currentProfileIndex];
  String startMsg = String("Press A to ask\n") + 
                    String(profile.sampleRate/1000) + "kHz " + 
                    String(profile.recordSeconds) + "s";
  drawScreen(startMsg);
  Serial.println("\nReady! Press button A to ask a question.\n");
}

void loop() {
  M5.update();

  // Trigger on button A (works on both StickC and Core 2 touch buttons)
  if (M5.BtnA.wasClicked()) {
    Serial.println("\n*** TRIGGERED ***\n");
    Serial.printf("Free heap before recording: %d bytes\n", ESP.getFreeHeap());

    if (!recordAudio()) {
      drawScreen("Mic error");
      delay(2000);
      drawScreen("Press A\nto ask a question");
      return;
    }

    Serial.printf("Free heap after recording: %d bytes\n", ESP.getFreeHeap());

    drawScreen("Transcribing...");
    String question = transcribeAudio();

    if (question.length() < 2 || question.startsWith("No ") ||
        question.startsWith("Parse") || question.startsWith("Connection") ||
        question.startsWith("Timeout")) {
      Serial.println("Transcription failed or empty");
      drawScreen("Couldn't hear.\nTry again.");
      delay(2000);
      drawScreen("Press A\nto ask a question");
      return;
    }

    drawScreen("Thinking...");
    String answer = askGPT(question);

    // Adjust wrap width based on screen size (Core 2 is wider)
    int wrapChars = (WIDTH >= 320) ? 35 : 25;
    response = wordWrap(answer, wrapChars);
    Serial.println("Final display text:");
    Serial.println(response);
    drawScreen(response);

    // Speak the response on Core2/CoreS3 (devices with speakers)
    if (USE_TTS && isLargeDevice) {
      speakText(answer);
    }

    Serial.printf("Free heap at end: %d bytes\n", ESP.getFreeHeap());
    Serial.println("\n*** INTERACTION COMPLETE ***\n");
  }

  // Button B: Click = new chat, Hold 2s = toggle audio profile
  static unsigned long btnBPressTime = 0;
  static bool btnBHeld = false;
  
  if (M5.BtnB.wasPressed()) {
    btnBPressTime = millis();
    btnBHeld = false;
  }
  
  if (M5.BtnB.isPressed() && !btnBHeld) {
    if (millis() - btnBPressTime >= 2000) {
      // Long press (2s) - toggle audio profile
      btnBHeld = true;
      nextAudioProfile();
      
      const AudioProfile& profile = deviceProfiles[currentProfileIndex];
      String msg = String("Profile:\n") + profile.name + "\n" + 
                   String(profile.sampleRate/1000) + "kHz " + 
                   String(profile.recordSeconds) + "s";
      drawScreen(msg);
      Serial.printf("Switched to profile: %s\n", profile.name);
      delay(2000);
      drawScreen("Press A\nto ask a question");
    }
  }
  
  if (M5.BtnB.wasReleased()) {
    if (!btnBHeld && (millis() - btnBPressTime < 2000)) {
      // Short click - new chat session
      Serial.println("Button B clicked - starting new chat session");
      currentChatId = "";
      drawScreen("New chat\nPress A to ask");
    }
    btnBHeld = false;
  }

  // Button C: Replay last TTS audio (Core2/CoreS3 only)
  if (isLargeDevice && USE_TTS && M5.BtnC.wasPressed()) {
    Serial.println("Button C pressed - replaying TTS");
    replayTts();
  }

  delay(20);
}
