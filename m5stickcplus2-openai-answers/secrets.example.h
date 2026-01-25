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
// For Mixed (OpenAI STT + OpenWebUI LLM) - RECOMMENDED:
//   STT: "api.openai.com" with OpenAI key
//   LLM: "http://your-openwebui-host:8080/api/v1/chat/completions" with JWT token
//   Note: OpenWebUI doesn't have a Whisper endpoint, so use OpenAI for STT

// Speech-to-Text endpoint (Whisper-compatible)
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
// System prompt added to user questions (leave empty "" for none)
const char *LLM_SYSTEM_PROMPT = " Answer in 20 words or less.";

// OpenWebUI Session Tracking (set to true to save chats in OpenWebUI history)
const bool USE_OWUI_SESSIONS = true;  // Enable for OpenWebUI chat history
const char *OWUI_BASE_URL = "http://your-openwebui-host:8080";  // Base URL for OpenWebUI

// NTP Time Servers (for accurate Unix timestamps)
const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.nist.gov";

// Example OpenWebUI configuration:
// const bool USE_OWUI_SESSIONS = true;
// const bool LLM_USE_RESPONSES_API = false;
// const char *OWUI_BASE_URL = "http://192.168.1.100:8080";
// const char *LLM_MODEL = "llama3";

#endif
