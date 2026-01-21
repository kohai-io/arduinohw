#ifndef SECRETS_H
#define SECRETS_H

// WiFi credentials
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";

// API Key (works with OpenAI or OpenAI-compatible APIs like OpenWebUI)
const char *API_KEY = "sk-proj-YOUR_OPENAI_API_KEY";

// API Endpoints (change these for OpenWebUI or other compatible APIs)
// For OpenAI:
//   STT: "api.openai.com" path "/v1/audio/transcriptions"
//   LLM: "https://api.openai.com/v1/responses"
// For OpenWebUI (example):
//   STT: "your-openwebui-host" path "/api/v1/audio/transcriptions"
//   LLM: "http://your-openwebui-host/api/v1/chat/completions"

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

#endif
