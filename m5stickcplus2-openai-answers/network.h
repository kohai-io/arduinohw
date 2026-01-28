#ifndef NETWORK_H
#define NETWORK_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <MP3DecoderHelix.h>
#include "secrets.h"
#include "audio.h"

using namespace libhelix;

// External references
extern int SAMPLE_RATE;
extern int16_t *audioBuffer;
extern int actualRecordedSamples;
extern bool isLargeDevice;
extern String systemPrompt;

// TTS audio output buffer and state
extern int16_t* ttsOutputBuffer;
extern size_t ttsOutputSize;
extern size_t ttsOutputIndex;
extern int ttsSampleRate;
extern int ttsChannels;

// Last played TTS for replay
extern int16_t* lastTtsBuffer;
extern size_t lastTtsLength;
extern int lastTtsSampleRate;
extern int lastTtsChannels;

// Current TTS voice
extern bool useTtsVoice1;

// OpenWebUI chat session tracking
extern String currentChatId;
extern String currentSessionId;

// UUID v4 generator for message and session IDs
String generateUUID() {
  String uuid = "";
  const char* hex = "0123456789abcdef";
  
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      uuid += '-';
    } else if (i == 14) {
      uuid += '4';
    } else if (i == 19) {
      uuid += hex[(esp_random() & 0x3) | 0x8];
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

// Callback for libhelix MP3 decoder
void ttsAudioCallback(MP3FrameInfo &info, short *pwm_buffer, size_t len, void *ref) {
  ttsSampleRate = info.samprate;
  ttsChannels = info.nChans;
  
  size_t newSize = ttsOutputIndex + len;
  if (newSize > ttsOutputSize) {
    size_t allocSize = newSize + 8192;
    int16_t* newBuffer = (int16_t*)realloc(ttsOutputBuffer, allocSize * sizeof(int16_t));
    if (newBuffer) {
      ttsOutputBuffer = newBuffer;
      ttsOutputSize = allocSize;
    } else {
      Serial.println("ERROR: Failed to realloc TTS buffer");
      return;
    }
  }
  
  memcpy(ttsOutputBuffer + ttsOutputIndex, pwm_buffer, len * sizeof(int16_t));
  ttsOutputIndex += len;
}

// Text-to-Speech
void speakText(const String &text);

// Replay last TTS audio
void replayTts();

// Transcribe audio to text
String transcribeAudio();

// Ask GPT/LLM a question
String askGPT(const String &question);

// Create chat session
String createChatSession(const String &title);

// Update chat with user message
bool updateChatWithUserMessage(const String &chatId, const String &userMsgId, const String &userContent);

// Chat completed handler
void chatCompleted(const String &chatId, const String &sessionId, const String &userMsgId, 
                   const String &userContent, const String &assistantMsgId, const String &assistantContent);

// Save chat history
void saveChatHistory(const String &chatId, const String &userMsgId, const String &userContent,
                     const String &assistantMsgId, const String &assistantContent);

#endif // NETWORK_H
