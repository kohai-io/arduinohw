#ifndef SECRETS_H
#define SECRETS_H

// WiFi credentials
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";

// API Keys - can use different keys for STT and LLM
// If using same provider for both, set both to the same value
const char *STT_API_KEY = "sk-proj-YOUR_OPENAI_API_KEY";  // For Whisper/transcription
const char *LLM_API_KEY = "sk-proj-YOUR_OPENAI_API_KEY";  // For chat completions

// API Endpoints (change these for OpenWebUI or other compatible APIs)
// For OpenAI:
//   STT: "api.openai.com" path "/v1/audio/transcriptions"
//   LLM: "https://api.openai.com/v1/responses"
// For Full OpenWebUI (STT + LLM):
//   Set USE_OWUI_STT = true and USE_OWUI_SESSIONS = true
//   Both will use OWUI_BASE_URL with LLM_API_KEY
// For Mixed (OpenAI STT + OpenWebUI LLM):
//   Set USE_OWUI_STT = false, USE_OWUI_SESSIONS = true
//   STT uses OpenAI, LLM uses OpenWebUI

// Speech-to-Text endpoint (Whisper-compatible)
// Set USE_OWUI_STT to true to use OpenWebUI's transcription endpoint instead of OpenAI
// When true, uses OWUI_BASE_URL + "/api/v1/audio/transcriptions" with LLM_API_KEY
const bool USE_OWUI_STT = false;  // Set to true to use OpenWebUI for STT

// OpenAI Whisper settings (used when USE_OWUI_STT = false)
const char *STT_HOST = "api.openai.com";
const int STT_PORT = 443;
const char *STT_PATH = "/v1/audio/transcriptions";
const bool STT_USE_SSL = true;
const char *STT_MODEL = "whisper-1";

// LLM endpoint (Chat completions compatible)
const char *LLM_URL = "https://api.openai.com/v1/responses";
const char *LLM_MODEL = "gpt-4o-mini";
// Set to true if using OpenAI Responses API format, false for standard chat completions
const bool LLM_USE_RESPONSES_API = true;

// System prompt configuration
// The actual prompt is built dynamically: LLM_SYSTEM_PROMPT_BASE + " in X words or less."
// where X is determined by device type (small screen = fewer words)
const char *LLM_SYSTEM_PROMPT_BASE = " Answer";  // Base prompt (word limit added automatically)
const int LLM_MAX_WORDS_SMALL = 20;   // For StickC Plus2 (small screen)
const int LLM_MAX_WORDS_LARGE = 50;   // For Core2/CoreS3 (larger screen)

// OpenWebUI Session Tracking (set to true to save chats in OpenWebUI history)
const bool USE_OWUI_SESSIONS = true;  // Enable for OpenWebUI chat history
const char *OWUI_BASE_URL = "http://your-openwebui-host:8080";  // Base URL for OpenWebUI

// Text-to-Speech (TTS) - Core2/CoreS3 only (devices with speakers)
// Uses OWUI_BASE_URL + "/api/v1/audio/speech" with LLM_API_KEY
// Requires arduino-libhelix library for MP3 decoding
const bool USE_TTS = true;            // Enable spoken responses on large devices
const char *TTS_MODEL = "tts-1";      // tts-1 or tts-1-hd
const char *TTS_VOICE_1 = "alloy";    // Primary voice (Button C hold 2s to toggle)
const char *TTS_VOICE_2 = "nova";     // Secondary voice

// NTP Time Servers (for accurate Unix timestamps)
const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.nist.gov";

// Example OpenWebUI configuration:
// const bool USE_OWUI_SESSIONS = true;
// const bool LLM_USE_RESPONSES_API = false;
// const char *OWUI_BASE_URL = "http://192.168.1.100:8080";
// const char *LLM_MODEL = "llama3";

#endif
