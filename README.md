# M5StickCPlus2 OpenAI Voice Assistant

Transform your M5StickCPlus2 into an intelligent voice assistant with OpenWebUI integration, featuring conversation threading, full context awareness, and persistent chat history.

## Features

### 🎙️ Voice Interaction
- **Audio Recording:** Press Button A to record your question
- **Voice Activity Detection (VAD):** Auto-stops recording after 1.5s of silence
- **Real-time Audio Level:** Visual feedback with color-coded level meter
- **Speech-to-Text:** Transcription via OpenAI Whisper or OpenWebUI STT
- **AI Response:** Get answers from your configured LLM model

### 💬 OpenWebUI Integration

This project integrates with [OpenWebUI](https://openwebui.com), an extensible, self-hosted WebUI for LLMs.

- **Session Tracking:** Maintains persistent chat sessions in OpenWebUI
- **Conversation Threading:** Linear message chains with proper parent-child relationships
- **Full Context:** LLM sees entire conversation history for contextual responses
- **Graceful Recovery:** Automatically handles deleted sessions and creates new ones
- **New Chat Sessions:** Press Button B to start a fresh conversation

**Custom Models/Agents:**
The `LLM_MODEL` setting can reference any model configured in your OpenWebUI instance, including:
- Standard LLM models (GPT-4, Claude, Llama, etc.)
- **Custom Agents** with specialized system prompts
- **RAG-enabled Models** with knowledge base documents
- **Tool-equipped Agents** with function calling capabilities

This allows you to create domain-specific assistants in OpenWebUI (e.g., a technical support agent with your product documentation, or a cooking assistant with recipe knowledge) and access them directly from your M5StickCPlus2 by simply setting the agent/model name in `secrets.h`.

### ⚙️ Configurable Settings
- Custom system prompts
- Adjustable NTP servers for accurate timestamps
- Flexible audio recording parameters
- Multiple API format support (OpenWebUI, standard Chat Completions, Responses API)

## Hardware Requirements

**Supported Devices:**
- M5StickC Plus2 (240x135 display)
- M5Stack Core2 (320x240 display)
- M5Stack CoreS3 (320x240 display)

**Also needed:**
- WiFi connection
- USB-C cable for programming

## Setup

### 1. Configure Secrets

Copy `secrets.example.h` to `secrets.h` and configure:

```cpp
// WiFi Configuration
const char *WIFI_SSID = "your-wifi-name";
const char *WIFI_PASS = "your-wifi-password";

// API Keys
const char *STT_API_KEY = "sk-...";  // For OpenAI Whisper (if USE_OWUI_STT = false)
const char *LLM_API_KEY = "sk-...";  // For LLM and OpenWebUI

// OpenWebUI Configuration
const char *OWUI_BASE_URL = "http://your-openwebui-host:8080";
const char *LLM_MODEL = "your-model-name";

// API Mode Configuration
const bool USE_OWUI_STT = true;      // Use OpenWebUI for speech-to-text
const bool USE_OWUI_SESSIONS = true; // Save chats in OpenWebUI history
```

### API Configuration Modes

| Mode | USE_OWUI_STT | USE_OWUI_SESSIONS | Description |
| :--- | :--- | :--- | :--- |
| **Full OpenWebUI** | true | true | Both STT and LLM via OpenWebUI |
| **Mixed** | false | true | OpenAI Whisper + OpenWebUI LLM |
| **Full OpenAI** | false | false | Both STT and LLM via OpenAI |

### 2. Install Dependencies

Required Arduino libraries:
- M5Unified
- ArduinoJson
- HTTPClient
- WiFiClientSecure

### 3. Upload Firmware

Using Arduino IDE:
1. Open `m5stickcplus2-openai-answers.ino`
2. Select board: M5StickCPlus2
3. Upload to device

Using arduino-cli:
```bash
arduino-cli compile --fqbn m5stack:esp32:m5stick_c_plus2
arduino-cli upload -p [PORT] --fqbn m5stack:esp32:m5stick_c_plus2
```

## Usage

### Basic Operation

1. **Power on** the device
2. Wait for "Press A to ask a question" on screen
3. **Press Button A** to start recording
4. **Speak your question** (recording will auto-stop after configured duration)
5. Wait for transcription and AI response
6. View response on display

### Button Controls

- **Button A:** Record and ask a question
- **Button B (click):** Start a new chat session (clears conversation history)
- **Button B (hold 2s):** Toggle audio quality profile

### Conversation Features

**Multi-turn Conversations:**
The assistant remembers previous messages in the same session:
```
You: "What's 2 plus 2?"
AI: "4"
You: "What about times 3?"
AI: "12" (understands you mean 4 × 3)
```

**Threading in OpenWebUI:**
All messages are properly linked in a conversation tree:
```
User msg (parentId: null)
└─ Assistant msg (parentId: user_id)
   └─ User msg (parentId: assistant_id)
      └─ Assistant msg (parentId: user_id)
```

## Audio Configuration & Device Profiles

The firmware automatically detects your device type and provides appropriate audio profiles.

### Device-Specific Profiles

**M5StickC Plus2** (240x135 display, ~120KB safe RAM):

| Profile | Sample Rate | Duration | Buffer | Use Case |
| :--- | :--- | :--- | :--- | :--- |
| **Standard** | 8 kHz | 5s | 80 KB | Default, balanced |
| **HQ Short** | 16 kHz | 3s | 96 KB | High quality, quick questions |

**Core2 / CoreS3** (320x240 display, ~300KB+ safe RAM):

| Profile | Sample Rate | Duration | Buffer | Use Case |
| :--- | :--- | :--- | :--- | :--- |
| **Standard** | 8 kHz | 8s | 128 KB | Default, balanced |
| **Long** | 8 kHz | 15s | 240 KB | Extended recording |
| **HQ Short** | 16 kHz | 5s | 160 KB | High quality |

### Switching Profiles

**Hold Button B for 2 seconds** to cycle through available profiles. The display will show the new profile settings.

### Device-Specific Response Length

The system prompt automatically adjusts based on screen size:
- **M5StickC Plus2:** 20 words max (fits small screen)
- **Core2/CoreS3:** 50 words max (larger screen)

Configure the base prompt and word limits in `secrets.h`:
```cpp
const char *LLM_SYSTEM_PROMPT_BASE = " Answer";  // Base prompt
const int LLM_MAX_WORDS_SMALL = 20;   // StickC Plus2
const int LLM_MAX_WORDS_LARGE = 50;   // Core2/CoreS3
```

## Architecture

### Message Flow

```
1. User presses Button A
2. Record audio via PDM microphone
3. Transcribe audio → OpenAI Whisper or OpenWebUI STT
4. Create/reuse OpenWebUI chat session
5. Update chat with user message (proper threading)
6. Send completion request with full conversation history
7. Poll chat history for assistant response
8. Save complete chat history with threading
9. Display response on screen
```

### OpenWebUI API Integration

**Endpoints used:**
- `POST /api/v1/audio/transcriptions` - Speech-to-text (when USE_OWUI_STT = true)
- `POST /api/v1/chats/new` - Create new chat session
- `GET /api/v1/chats/{id}` - Fetch chat history
- `POST /api/v1/chats/{id}` - Update chat with messages
- `POST /api/v1/chat/completions` - Request LLM completion
- `POST /api/chat/completed` - Signal completion

**Features:**
- UUID generation for messages and sessions
- Unix timestamps for message ordering
- Parent-child message linking via `parentId` and `childrenIds`
- Model tracking in chat metadata
- Automatic session recovery on errors (401/404)

## Troubleshooting

### Memory Issues
- Reduce `RECORD_SECONDS` or `SAMPLE_RATE`
- Monitor serial output for heap warnings

### Connection Errors
- Verify WiFi credentials
- Check API endpoint URLs
- Confirm API key is valid

### Transcription Issues
- Speak clearly and close to microphone
- Reduce background noise
- Consider increasing `SAMPLE_RATE` for better quality

### Session Errors
- Device automatically recovers from deleted sessions
- Press Button B to manually start fresh session
- Check OpenWebUI server availability

## Development

### Serial Monitor

Enable serial output (115200 baud) to see:
- WiFi connection status
- API request/response details
- Chat session management
- Memory usage statistics
- Conversation threading debug info

### Contributing

Contributions welcome! Areas for improvement:
- Additional audio codecs
- Alternative LLM backends
- Enhanced display UI
- Wake word support
- Text-to-speech responses

## License

MIT License - see LICENSE file for details
