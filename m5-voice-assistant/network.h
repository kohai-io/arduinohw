#ifndef NETWORK_H
#define NETWORK_H

// Pure declarations only - no includes to avoid conflicts
// MP3DecoderHelix types used in main .ino file

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

// Function declarations
String generateUUID();
unsigned long getUnixTimestamp();
unsigned long long getUnixTimestampMs();

void speakText(const String &text);
void replayTts();
String transcribeAudio();
String askGPT(const String &question);
String createChatSession(const String &title);
bool updateChatWithUserMessage(const String &chatId, const String &userMsgId, const String &userContent);
bool chatCompleted(const String &chatId, const String &sessionId, const String &userMsgId, 
                   const String &userContent, const String &assistantMsgId, const String &assistantContent);
bool saveChatHistory(const String &chatId, const String &userMsgId, const String &userContent,
                     const String &assistantMsgId, const String &assistantContent);

#endif // NETWORK_H
