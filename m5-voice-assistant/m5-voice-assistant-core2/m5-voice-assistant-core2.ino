#include "../secrets.h"
#include <HTTPClient.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>

// MP3 decoding for TTS (Core2/CoreS3 only) - uses arduino-libhelix
#include <MP3DecoderHelix.h>
using namespace libhelix;

// FastLED for M5GO-Bottom2 LED control
#include <FastLED.h>

// Modular includes (must come after system includes)
#include "device_config.h"
#include "m5go_leds.h"

// Forward declarations for display.h
extern bool audioLevelInitialized;

#include "../common/display.h"
#include "touch_ui.h"
#include "../common/audio.h"
#include "camera.h"
#include "../common/image_upload.h"
#include "../common/api_functions.h"

// Display dimensions - set dynamically in setup()
int WIDTH = 240;
int HEIGHT = 135;

// M5GO-Bottom2 LED state (size from config)
CRGB leds[10];  // Max size, actual count from M5GO_NUM_LEDS in config
bool hasM5GOBottom2 = false;

// Camera state (defined in camera.h)
extern bool cameraInitialized;

// Current audio settings (dynamic) - defined in config.h
int SAMPLE_RATE = 8000;
int RECORD_SECONDS = 5;
int RECORD_SAMPLES = SAMPLE_RATE * RECORD_SECONDS;
int16_t *audioBuffer = nullptr;

// Profile management - defined in config.h
int currentProfileIndex = 0;
bool isLargeDevice = false;
int numProfiles = 2;
const AudioProfile* deviceProfiles = STICK_PROFILES;

// LED functions now in m5go_leds.h
// Config functions now in config.h

// Voice Activity Detection (VAD) settings
const int VAD_SILENCE_THRESHOLD = 500;
const float VAD_SILENCE_DURATION = 1.5;
const bool VAD_ENABLED = true;

// Dynamic system prompt
int currentMaxWords = 20;
String systemPrompt = "";

String response = "Press A\nto ask a question";

// OpenWebUI chat session tracking
String currentChatId = "";
String currentSessionId = "";

// Track actual recorded samples (for VAD early stop)
int actualRecordedSamples = RECORD_SAMPLES;

// Audio profile functions now in config.h

// TTS audio output buffer and state (kept for replay)
int16_t* ttsOutputBuffer = nullptr;
size_t ttsOutputSize = 0;
size_t ttsOutputIndex = 0;
int ttsSampleRate = 44100;
int ttsChannels = 2;

// Last played TTS for replay on button C
int16_t* lastTtsBuffer = nullptr;
size_t lastTtsLength = 0;
int lastTtsSampleRate = 44100;
int lastTtsChannels = 1;

// Current TTS voice (toggles between TTS_VOICE_1 and TTS_VOICE_2)
bool useTtsVoice1 = true;

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
  const char* currentVoice = useTtsVoice1 ? TTS_VOICE_1 : TTS_VOICE_2;
  String body = "{\"model\":\"" + String(TTS_MODEL) + "\","
                "\"input\":\"" + text + "\","
                "\"voice\":\"" + String(currentVoice) + "\"}";
  
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

// Real-time audio display state
volatile bool isRecording = false;
volatile int currentRmsLevel = 0;
volatile int recordingSecondsLeft = 0;
TaskHandle_t displayTaskHandle = NULL;
bool audioLevelInitialized = false;

// Utility functions
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

unsigned long getUnixTimestamp() {
  time_t now;
  time(&now);
  return (unsigned long)now;
}

unsigned long long getUnixTimestampMs() {
  return (unsigned long long)getUnixTimestamp() * 1000;
}

// Display and audio functions now in display.h and audio.h

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

String askGPTWithImage(const String &question, const String &fileId) {
  Serial.println("\n========== ASKING LLM WITH IMAGE ==========");
  Serial.println("Question: " + question);
  Serial.println("File ID: " + fileId);

  if (fileId.length() == 0) {
    Serial.println("ERROR: No file ID provided, falling back to text-only");
    return askGPT(question);
  }

  // Step 1: Create or reuse chat session for OpenWebUI
  if (USE_OWUI_SESSIONS && currentChatId.length() == 0) {
    currentChatId = createChatSession("M5 Voice Assistant");
    currentSessionId = generateUUID();
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
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  // Build message content with image file reference
  // OpenWebUI format for images in messages uses files array
  String escapedQuestion = question;
  escapedQuestion.replace("\\", "\\\\");
  escapedQuestion.replace("\"", "\\\"");
  escapedQuestion.replace("\n", " ");

  String messageContent = escapedQuestion + systemPrompt;

  // Build messages array with image file
  String messagesArray = "{\"role\":\"user\",\"content\":\"" + messageContent + "\","
                        "\"files\":[{\"type\":\"file\",\"id\":\"" + fileId + "\"}]}";

  String url = String(OWUI_BASE_URL) + "/api/v1/chat/completions";
  
  Serial.printf("Connecting to LLM at %s...\n", url.c_str());
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(90000);

  String body = "{"
         "\"model\":\"" + String(LLM_MODEL) + "\","
         "\"messages\":[" + messagesArray + "],"
         "\"chat_id\":\"" + currentChatId + "\","
         "\"id\":\"" + assistantMsgId + "\","
         "\"session_id\":\"" + currentSessionId + "\","
         "\"stream\":true"
         "}";

  Serial.println("Sending request with image...");
  Serial.println("Body: " + body);

  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);

  if (httpCode != 200) {
    String error = http.getString();
    Serial.println("Error response: " + error);
    http.end();
    return "HTTP " + String(httpCode);
  }

  String result = "";
  String resp = http.getString();
  http.end();
  
  Serial.println("Task initiated:");
  Serial.println(resp);
  
  // Poll chat history until assistant response appears
  Serial.println("Polling chat history for completion...");
  
  HTTPClient fetchHttp;
  WiFiClientSecure fetchClient;
  fetchClient.setInsecure();
  
  String fetchUrl = String(OWUI_BASE_URL) + "/api/v1/chats/" + currentChatId;
  unsigned long pollStart = millis();
  int pollAttempt = 0;
  
  while (millis() - pollStart < 90000) { // 90 second timeout for image processing
    pollAttempt++;
    delay(1500); // Poll every 1.5 seconds (image processing takes longer)
    
    Serial.printf("[POLL #%d] Fetching chat history...\n", pollAttempt);
    
    fetchHttp.begin(fetchClient, fetchUrl);
    fetchHttp.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
    
    int fetchCode = fetchHttp.GET();
    String chatData = fetchHttp.getString();
    fetchHttp.end();
    
    if (fetchCode == 200) {
      int msgIdx = chatData.indexOf("\"" + assistantMsgId + "\"");
      
      if (msgIdx >= 0) {
        Serial.printf("Found assistant message after %d polls\n", pollAttempt);
        
        int contentIdx = chatData.indexOf("\"content\"", msgIdx);
        if (contentIdx < 0) {
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
            break;
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

  Serial.println("Extracted answer: " + result);
  Serial.println("===========================================\n");

  // Call completed handler and save chat history
  if (USE_OWUI_SESSIONS && currentChatId.length() > 0 && userMsgId.length() > 0) {
    chatCompleted(currentChatId, currentSessionId, userMsgId, question, assistantMsgId, result);
    saveChatHistory(currentChatId, userMsgId, question, assistantMsgId, result);
  }
  
  return result;
}

// wordWrap() now in display.h

// Handler for voice-only questions (used by touch UI)
void handleVoiceQuestion() {
  Serial.println("\n*** VOICE QUESTION TRIGGERED ***\n");
  Serial.printf("Free heap before recording: %d bytes\n", ESP.getFreeHeap());

  Serial.println("DEBUG: About to call recordAudio()");
  bool recordResult = recordAudio();
  Serial.printf("DEBUG: recordAudio() returned: %s\n", recordResult ? "true" : "false");
  
  if (!recordResult) {
    Serial.println("DEBUG: Recording failed, showing error");
    #if ENABLE_TOUCH_UI
    drawScreenWithButtons("Mic error");
    delay(2000);
    drawScreenWithButtons("Ready!\nTap button below");
    #else
    drawScreen("Mic error");
    delay(2000);
    drawScreen("Press A to ask");
    #endif
    return;
  }

  Serial.printf("Free heap after recording: %d bytes\n", ESP.getFreeHeap());
  Serial.println("DEBUG: About to show transcribing screen");

  drawScreenWithButtons("Transcribing...");
  
  Serial.println("DEBUG: About to call transcribeAudio()");
  String question = transcribeAudio();
  Serial.printf("DEBUG: transcribeAudio() returned: %s\n", question.c_str());

  if (question.length() < 2 || question.startsWith("No ") ||
      question.startsWith("Parse") || question.startsWith("Connection") ||
      question.startsWith("Timeout")) {
    Serial.println("Transcription failed or empty");
    #if ENABLE_TOUCH_UI
    drawScreenWithButtons("Couldn't hear.\nTry again.");
    delay(2000);
    drawScreenWithButtons("Ready!\nTap button below");
    #else
    drawScreen("Couldn't hear.\nTry again.");
    delay(2000);
    drawScreen("Press A to ask");
    #endif
    return;
  }

  drawScreenWithButtons("Thinking...");
  
  String answer = askGPT(question);

  int wrapChars = (WIDTH >= 320) ? 35 : 25;
  response = wordWrap(answer, wrapChars);
  Serial.println("Final display text:");
  Serial.println(response);
  drawScreenWithButtons(response);

  if (USE_TTS && isLargeDevice) {
    speakText(answer);
  }

  Serial.printf("Free heap at end: %d bytes\n", ESP.getFreeHeap());
  Serial.println("\n*** INTERACTION COMPLETE ***\n");
}

// Handler for camera + voice questions (used by touch UI)
void handleCameraQuestion() {
  Serial.println("\n*** CAMERA MODE TRIGGERED ***\n");
  drawScreenWithButtons("Capturing image...");
  
  if (!captureImage()) {
    #if ENABLE_TOUCH_UI
    drawScreenWithButtons("Camera error");
    delay(2000);
    drawScreenWithButtons("Ready!\nTap button below");
    #else
    drawScreen("Camera error");
    delay(2000);
    drawScreen("Press A to ask");
    #endif
    return;
  }
  
  // Show preview
  displayCapturedImage();
  delay(1000);
  
  drawScreenWithButtons("Uploading image...");
  
  String fileId = uploadLastCapturedImage();
  if (fileId.length() == 0) {
    #if ENABLE_TOUCH_UI
    drawScreenWithButtons("Upload failed");
    delay(2000);
    drawScreenWithButtons("Ready!\nTap button below");
    #else
    drawScreen("Upload failed");
    delay(2000);
    drawScreen("Press A to ask");
    #endif
    return;
  }
  
  Serial.printf("Image uploaded, file ID: %s\n", fileId.c_str());
  
  drawScreenWithButtons("Ask about image\nRecording...");
  
  if (!recordAudio()) {
    #if ENABLE_TOUCH_UI
    drawScreenWithButtons("Mic error");
    delay(2000);
    drawScreenWithButtons("Ready!\nTap button below");
    #else
    drawScreen("Mic error");
    delay(2000);
    drawScreen("Press A to ask");
    #endif
    return;
  }
  
  drawScreenWithButtons("Transcribing...");
  
  String question = transcribeAudio();
  
  if (question.length() < 2 || question.startsWith("No ") ||
      question.startsWith("Parse") || question.startsWith("Connection") ||
      question.startsWith("Timeout")) {
    Serial.println("Transcription failed, using default question");
    question = "What do you see in this image?";
  }
  
  drawScreenWithButtons("Analyzing image...");
  
  String answer = askGPTWithImage(question, fileId);
  
  int wrapChars = (WIDTH >= 320) ? 35 : 25;
  response = wordWrap(answer, wrapChars);
  Serial.println("Final display text:");
  Serial.println(response);
  drawScreenWithButtons(response);
  
  if (USE_TTS && isLargeDevice) {
    speakText(answer);
  }
  
  Serial.printf("Free heap at end: %d bytes\n", ESP.getFreeHeap());
  Serial.println("\n*** CAMERA INTERACTION COMPLETE ***\n");
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

  // Detect device type and configure (from config.h)
  detectDeviceType();
  Serial.printf("Max words: %d\n", isLargeDevice ? LLM_MAX_WORDS_LARGE : LLM_MAX_WORDS_SMALL);
  
  // Apply default profile and build system prompt
  applyAudioProfile(0);
  buildSystemPrompt(LLM_SYSTEM_PROMPT_BASE, LLM_MAX_WORDS_SMALL, LLM_MAX_WORDS_LARGE);
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  // Initialize camera for CoreS3 (has camera module) - do this BEFORE M5GO detection
  Serial.printf("Camera init check: ENABLE_CAMERA=%d, isLargeDevice=%d, WIDTH=%d, HEIGHT=%d\n", 
                ENABLE_CAMERA, isLargeDevice, WIDTH, HEIGHT);
  
  if (ENABLE_CAMERA && isLargeDevice && WIDTH >= 320 && HEIGHT >= 240) {
    Serial.println("Initializing camera for CoreS3...");
    if (initCamera()) {
      Serial.println("Camera ready for image capture");
    } else {
      Serial.println("Camera initialization failed (may not be CoreS3)");
    }
  } else if (!ENABLE_CAMERA) {
    Serial.println("Camera disabled in config");
  } else {
    Serial.println("Camera not initialized - device requirements not met");
  }

  // Detect M5GO-Bottom2 after device type is determined (from m5go_leds.h)
  // Pass camera status to avoid pin conflict (camera uses pin 25)
  detectM5GOBottom2(isLargeDevice, cameraInitialized);

  drawScreen("Connecting...");

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    // Pulse LEDs while connecting
    if (hasM5GOBottom2 && attempt % 2 == 0) {
      setM5GOLEDs(CRGB::Yellow);
    } else if (hasM5GOBottom2) {
      clearM5GOLEDs();
    }
    
    attempt++;
    if (attempt % 10 == 0) {
      Serial.printf("\nWiFi Status: %d\n", WiFi.status());
      // 255: WL_NO_SHIELD, 0: WL_IDLE_STATUS, 1: WL_NO_SSID_AVAIL
      // 2: WL_SCAN_COMPLETED, 3: WL_CONNECTED, 4: WL_CONNECT_FAILED
      // 5: WL_CONNECTION_LOST, 6: WL_DISCONNECTED
    }
  }

  Serial.println("\nWiFi connected!");
  
  // WiFi connected - brief green flash
  if (hasM5GOBottom2) {
    setM5GOLEDs(CRGB::Green);
    delay(500);
    clearM5GOLEDs();
  }
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
  
  #if ENABLE_TOUCH_UI
  if (isLargeDevice && WIDTH >= 320) {
    // CoreS3 with touchscreen - show touch UI
    String startMsg = "Ready!\nTap button below";
    drawScreenWithButtons(startMsg);
    Serial.println("\nReady! Tap Voice or Camera button.\n");
  } else
  #endif
  {
    // Core2/StickC or other devices with physical buttons
    String startMsg = String("Press A to ask\n") + 
                      String(profile.sampleRate/1000) + "kHz " + 
                      String(profile.recordSeconds) + "s";
    drawScreen(startMsg);
    Serial.println("\nReady! Press button A to ask a question.\n");
  }
}

void loop() {
  M5.update();

  // Handle touch input for CoreS3 (320x240 touchscreen)
  // Only enable touch UI if configured (Core2 uses physical buttons instead)
  #if ENABLE_TOUCH_UI
  if (isLargeDevice && WIDTH >= 320) {
    static bool wasTouched = false;
    static unsigned long lastTouchTime = 0;
    
    M5.update();
    
    // Check for touch events with debouncing
    if (M5.Touch.getCount()) {
      // Debounce - ignore touches within 500ms of last touch
      if (millis() - lastTouchTime < 500) {
        delay(20);
        return;
      }
      
      if (!wasTouched) {
        wasTouched = true;
        auto touch = M5.Touch.getDetail();
        
        // Get raw touch coordinates
        int rawX = touch.x;
        int rawY = touch.y;
        
        Serial.printf("RAW touch: X=%d, Y=%d\n", rawX, rawY);
        
        // CoreS3 touch calibration - map raw values to screen coordinates
        // Raw touch range is approximately 0-4095 for both X and Y
        // Screen is 320x240 in landscape mode (rotation 1)
        // X coordinate is inverted - high raw X = left side of screen
        int touchX = map(rawX, 4095, 0, 0, WIDTH);  // Inverted mapping
        int touchY = map(rawY, 0, 4095, 0, HEIGHT);
        
        // Constrain to screen bounds
        touchX = constrain(touchX, 0, WIDTH - 1);
        touchY = constrain(touchY, 0, HEIGHT - 1);
        
        Serial.printf("MAPPED touch: X=%d, Y=%d (WIDTH=%d, HEIGHT=%d)\n", touchX, touchY, WIDTH, HEIGHT);
        Serial.printf("Voice button: X=%d-%d, Y=%d-%d\n", 5, 155, 155, 235);
        Serial.printf("Camera button: X=%d-%d, Y=%d-%d\n", 165, 315, 155, 235);
        
        int buttonId = getTouchedButton(touchX, touchY);
        
        Serial.printf("Detected button: %d\n", buttonId);
        
        if (buttonId >= 0) {
          Serial.printf("=== Button %d activated ===\n", buttonId);
          lastTouchTime = millis();
          
          if (buttonId == BTN_VOICE) {
            Serial.println("Voice button pressed - starting voice question");
            handleVoiceQuestion();
          } else if (buttonId == BTN_CAMERA) {
            Serial.printf("Camera initialized: %s\n", cameraInitialized ? "YES" : "NO");
            if (cameraInitialized) {
              Serial.println("Camera button pressed - starting camera question");
              handleCameraQuestion();
            } else {
              Serial.println("Camera button pressed but camera not initialized!");
              drawScreenWithButtons("Camera not ready");
              delay(1500);
              drawScreenWithButtons("Ready!\nTap button below");
            }
          } else if (buttonId == BTN_NEW_CHAT) {
            Serial.println("New chat button pressed");
            currentChatId = "";
            drawScreenWithButtons("New chat started");
            delay(1000);
            drawScreenWithButtons("Ready!\nTap button below");
          }
        }
      }
    } else {
      // Touch released
      wasTouched = false;
    }
    
    delay(20);
    return;
  }
  #endif // ENABLE_TOUCH_UI

  // Physical button handling for StickC and other devices
  // Button A: Click = voice question, Hold 2s = camera + voice question (CoreS3 only)
  static unsigned long btnAPressTime = 0;
  static bool btnAHeld = false;
  
  if (M5.BtnA.wasPressed()) {
    btnAPressTime = millis();
    btnAHeld = false;
  }
  
  // Check for long press (camera mode on CoreS3)
  if (M5.BtnA.isPressed() && !btnAHeld && cameraInitialized) {
    if (millis() - btnAPressTime >= 2000) {
      btnAHeld = true;
      
      Serial.println("\n*** CAMERA MODE TRIGGERED ***\n");
      drawScreen("Capturing image...");
      
      // LED: Blue during camera capture
      if (hasM5GOBottom2) {
        setM5GOLEDs(CRGB::Blue);
      }
      
      if (!captureImage()) {
        drawScreen("Camera error");
        clearM5GOLEDs();
        delay(2000);
        drawScreen("Press A\nto ask a question");
        return;
      }
      
      // Show preview
      displayCapturedImage();
      delay(1000);
      
      drawScreen("Uploading image...");
      
      // Upload image to OWUI
      String fileId = uploadLastCapturedImage();
      if (fileId.length() == 0) {
        drawScreen("Upload failed");
        clearM5GOLEDs();
        delay(2000);
        drawScreen("Press A\nto ask a question");
        return;
      }
      
      Serial.printf("Image uploaded, file ID: %s\n", fileId.c_str());
      
      // Now record audio question about the image
      drawScreen("Ask about image\nRecording...");
      
      if (!recordAudio()) {
        drawScreen("Mic error");
        clearM5GOLEDs();
        delay(2000);
        drawScreen("Press A\nto ask a question");
        return;
      }
      
      drawScreen("Transcribing...");
      
      // LED: Cyan pulse during transcription
      if (hasM5GOBottom2) {
        breatheM5GOLEDs(CRGB::Cyan, 1);
      }
      
      String question = transcribeAudio();
      
      if (question.length() < 2 || question.startsWith("No ") ||
          question.startsWith("Parse") || question.startsWith("Connection") ||
          question.startsWith("Timeout")) {
        Serial.println("Transcription failed, using default question");
        question = "What do you see in this image?";
      }
      
      drawScreen("Analyzing image...");
      
      // LED: Purple/Magenta during AI thinking
      if (hasM5GOBottom2) {
        setM5GOLEDs(CRGB::Purple);
      }
      
      String answer = askGPTWithImage(question, fileId);
      
      // Clear LEDs after response
      clearM5GOLEDs();
      
      // Adjust wrap width based on screen size
      int wrapChars = (WIDTH >= 320) ? 35 : 25;
      response = wordWrap(answer, wrapChars);
      Serial.println("Final display text:");
      Serial.println(response);
      drawScreen(response);
      
      // Speak the response
      if (USE_TTS && isLargeDevice) {
        if (hasM5GOBottom2) {
          setM5GOLEDs(CRGB::Orange);
        }
        speakText(answer);
        clearM5GOLEDs();
      }
      
      Serial.printf("Free heap at end: %d bytes\n", ESP.getFreeHeap());
      Serial.println("\n*** CAMERA INTERACTION COMPLETE ***\n");
    }
  }
  
  // Short click on button A - normal voice question
  if (M5.BtnA.wasReleased()) {
    if (!btnAHeld && (millis() - btnAPressTime < 2000)) {
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
      
      // LED: Cyan pulse during transcription
      if (hasM5GOBottom2) {
        breatheM5GOLEDs(CRGB::Cyan, 1);
      }
      
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
      
      // LED: Purple/Magenta during AI thinking
      if (hasM5GOBottom2) {
        setM5GOLEDs(CRGB::Purple);
      }
      
      String answer = askGPT(question);
      
      // Clear LEDs after response
      clearM5GOLEDs();

      // Adjust wrap width based on screen size (Core 2 is wider)
      int wrapChars = (WIDTH >= 320) ? 35 : 25;
      response = wordWrap(answer, wrapChars);
      Serial.println("Final display text:");
      Serial.println(response);
      drawScreen(response);

      // Speak the response on Core2/CoreS3 (devices with speakers)
      if (USE_TTS && isLargeDevice) {
        // LED: Orange during TTS playback
        if (hasM5GOBottom2) {
          setM5GOLEDs(CRGB::Orange);
        }
        
        speakText(answer);
        
        // Clear LEDs after speaking
        clearM5GOLEDs();
      }

      Serial.printf("Free heap at end: %d bytes\n", ESP.getFreeHeap());
      Serial.println("\n*** INTERACTION COMPLETE ***\n");
    }
    btnAHeld = false;
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

  // Button C: Click = replay TTS, Hold 2s = toggle voice (Core2/CoreS3 only)
  static unsigned long btnCPressTime = 0;
  static bool btnCHeld = false;
  
  if (isLargeDevice && USE_TTS) {
    if (M5.BtnC.wasPressed()) {
      btnCPressTime = millis();
      btnCHeld = false;
    }
    
    if (M5.BtnC.isPressed() && !btnCHeld) {
      if (millis() - btnCPressTime >= 2000) {
        // Long press (2s) - toggle TTS voice
        btnCHeld = true;
        useTtsVoice1 = !useTtsVoice1;
        const char* newVoice = useTtsVoice1 ? TTS_VOICE_1 : TTS_VOICE_2;
        String msg = String("Voice:\n") + newVoice;
        drawScreen(msg);
        Serial.printf("Switched to TTS voice: %s\n", newVoice);
        delay(1500);
        drawScreen("Press A\nto ask a question");
      }
    }
    
    if (M5.BtnC.wasReleased()) {
      if (!btnCHeld && (millis() - btnCPressTime < 2000)) {
        // Short click - replay TTS
        Serial.println("Button C clicked - replaying TTS");
        replayTts();
      }
      btnCHeld = false;
    }
  }

  delay(20);
}
