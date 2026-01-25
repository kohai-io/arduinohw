#include "secrets.h"
#include <HTTPClient.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

// Display dimensions - set dynamically in setup()
static int WIDTH = 240;
static int HEIGHT = 135;

// Credentials are now in secrets.h

// Audio settings
static const int SAMPLE_RATE = 8000;
static const int RECORD_SECONDS = 5;
static const int RECORD_SAMPLES = SAMPLE_RATE * RECORD_SECONDS;
static int16_t *audioBuffer = nullptr;

String response = "Press A\nto ask a question";

// OpenWebUI chat session tracking
String currentChatId = "";
String currentSessionId = "";

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

  int samplesPerSecond = SAMPLE_RATE;
  for (int sec = 0; sec < RECORD_SECONDS; sec++) {
    Serial.printf("Recording second %d/%d...\n", sec + 1, RECORD_SECONDS);
    drawProgress(RECORD_SECONDS - sec);
    M5.Mic.record(&audioBuffer[sec * samplesPerSecond], samplesPerSecond,
                  SAMPLE_RATE);
    while (M5.Mic.isRecording()) {
      delay(10);
    }
  }

  M5.Mic.end();
  Serial.println("Recording complete");

  // Audio stats
  int16_t minVal = 32767, maxVal = -32768;
  int64_t sum = 0;
  for (int i = 0; i < RECORD_SAMPLES; i++) {
    if (audioBuffer[i] < minVal)
      minVal = audioBuffer[i];
    if (audioBuffer[i] > maxVal)
      maxVal = audioBuffer[i];
    sum += abs(audioBuffer[i]);
  }
  Serial.printf("Audio stats: min=%d, max=%d, avg=%lld\n", minVal, maxVal,
                sum / RECORD_SAMPLES);
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

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);

  Serial.printf("Connecting to %s:%d...\n", STT_HOST, STT_PORT);
  if (!client.connect(STT_HOST, STT_PORT)) {
    Serial.println("ERROR: Connection failed!");
    return "Connection failed";
  }
  Serial.println("Connected");

  int audioDataSize = RECORD_SAMPLES * sizeof(int16_t);
  uint8_t wavHeader[44];
  createWavHeader(wavHeader, audioDataSize);

  String boundary = "----ESP32Boundary";

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

  String response = client.readString();
  client.stop();

  Serial.println("Whisper response body:");
  Serial.println(response);

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
  Serial.printf("Updating chat at: %s\n", url.c_str());
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(30000);
  
  unsigned long timestamp = getUnixTimestamp(); // Unix epoch in seconds
  
  String escapedContent = userContent;
  escapedContent.replace("\\", "\\\\");
  escapedContent.replace("\"", "\\\"");
  escapedContent.replace("\n", "\\n");
  
  String body = "{"
                "\"chat\":{"
                "\"title\":\"M5 Voice Assistant\","
                "\"history\":{"
                "\"messages\":{"
                "\"" + userMsgId + "\":{"
                "\"id\":\"" + userMsgId + "\","
                "\"parentId\":null,"
                "\"childrenIds\":[],"
                "\"role\":\"user\","
                "\"content\":\"" + escapedContent + "\","
                "\"timestamp\":" + String(timestamp) + ","
                "\"models\":[\"" + String(LLM_MODEL) + "\"]"
                "}"
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
  Serial.printf("Saving to: %s\n", url.c_str());
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + LLM_API_KEY);
  http.setTimeout(30000);
  
  // Get current timestamp in seconds
  unsigned long timestamp = getUnixTimestamp();
  
  // Escape content for JSON
  String escapedUser = userContent;
  escapedUser.replace("\\", "\\\\");
  escapedUser.replace("\"", "\\\"");
  escapedUser.replace("\n", "\\n");
  
  String escapedAssistant = assistantContent;
  escapedAssistant.replace("\\", "\\\\");
  escapedAssistant.replace("\"", "\\\"");
  escapedAssistant.replace("\n", "\\n");
  
  // Build chat history JSON with full structure (Step 6)
  String body = "{"
                "\"chat\":{"
                "\"title\":\"M5 Voice Assistant\","
                "\"history\":{"
                "\"messages\":{"
                "\"" + userMsgId + "\":{"
                "\"id\":\"" + userMsgId + "\","
                "\"parentId\":null,"
                "\"childrenIds\":[\"" + assistantMsgId + "\"],"
                "\"role\":\"user\","
                "\"content\":\"" + escapedUser + "\","
                "\"timestamp\":" + String(timestamp) + ","
                "\"models\":[\"" + String(LLM_MODEL) + "\"]"
                "},"
                "\"" + assistantMsgId + "\":{"
                "\"id\":\"" + assistantMsgId + "\","
                "\"parentId\":\"" + userMsgId + "\","
                "\"childrenIds\":[],"
                "\"role\":\"assistant\","
                "\"content\":\"" + escapedAssistant + "\","
                "\"model\":\"" + String(LLM_MODEL) + "\","
                "\"timestamp\":" + String(timestamp + 5) + ","
                "\"done\":true"
                "}"
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
      Serial.println("ERROR: Failed to update chat with user message!");
      return "Update error";
    }
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

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

  String escaped = question;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", " ");

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
           String(LLM_SYSTEM_PROMPT) + "\""
           "}"
           "]"
           "}"
           "]"
           "}";
  } else if (USE_OWUI_SESSIONS) {
    // Step 4: OpenWebUI with chat session tracking
    body = "{"
           "\"model\":\"" + String(LLM_MODEL) + "\","
           "\"messages\":["
           "{"
           "\"role\":\"user\","
           "\"content\":\"" +
           escaped +
           String(LLM_SYSTEM_PROMPT) + "\""
           "}"
           "],"
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
           String(LLM_SYSTEM_PROMPT) + "\""
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
      delay(2000); // Poll every 2 seconds
      
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
          
          // Find the content field after this message ID
          int contentIdx = chatData.indexOf("\"content\"", msgIdx);
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
          }
          
          if (result.length() > 0) {
            Serial.printf("Retrieved response length: %d\n", result.length());
            break;
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
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
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

  // Pre-allocate audio buffer
  audioBuffer = (int16_t *)malloc(RECORD_SAMPLES * sizeof(int16_t));
  if (!audioBuffer) {
    Serial.println("CRITICAL ERROR: Failed to allocate audio buffer!");
    drawScreen("Mem Error");
    while (1)
      delay(100);
  }
  Serial.println("Audio buffer allocated");

  // Show appropriate prompt based on device capabilities
  drawScreen("Press A\nto ask a question");
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

    Serial.printf("Free heap at end: %d bytes\n", ESP.getFreeHeap());
    Serial.println("\n*** INTERACTION COMPLETE ***\n");
  }

  // Button B returns to home screen
  if (M5.BtnB.wasClicked()) {
    Serial.println("Button B pressed - returning to home screen");
    drawScreen("Press A\nto ask a question");
  }

  delay(20);
}
