#ifndef AUDIO_H
#define AUDIO_H

#include <M5Unified.h>
#include "m5go_leds.h"

// External references
extern int SAMPLE_RATE;
extern int RECORD_SECONDS;
extern int RECORD_SAMPLES;
extern int16_t *audioBuffer;
extern int actualRecordedSamples;
extern const int VAD_SILENCE_THRESHOLD;
extern const float VAD_SILENCE_DURATION;
extern const bool VAD_ENABLED;

// Real-time audio display state
extern volatile bool isRecording;
extern volatile int currentRmsLevel;
extern volatile int recordingSecondsLeft;
extern TaskHandle_t displayTaskHandle;
extern bool audioLevelInitialized;

// LED control functions
extern void setM5GOLEDs(CRGB color);
extern void setM5GOLEDsPattern(int activeLeds, CRGB color);
extern void clearM5GOLEDs();
extern bool hasM5GOBottom2;

// Display task
extern void displayTask(void *parameter);

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
  audioLevelInitialized = false;
  
  // Initialize M5GO LEDs for recording
  if (hasM5GOBottom2) {
    setM5GOLEDs(CRGB::Blue);
  }
  
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
      
      // Update M5GO LEDs based on audio level
      if (hasM5GOBottom2) {
        int activeLeds = map(constrain(currentRmsLevel, 0, 3000), 0, 3000, 0, M5GO_NUM_LEDS);
        if (currentRmsLevel >= VAD_SILENCE_THRESHOLD) {
          setM5GOLEDsPattern(activeLeds, CRGB::Green);
        } else {
          setM5GOLEDsPattern(2, CRGB::Blue);
        }
      }
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
  
  // Clear M5GO LEDs
  clearM5GOLEDs();
  
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

#endif // AUDIO_H
