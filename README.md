# M5StickCPlus2 OpenAI Voice Assistant

Transform your M5StickCPlus2 into an intelligent voice assistant with OpenWebUI integration, featuring conversation threading, full context awareness, and persistent chat history.

## Features

### 🎙️ Voice Interaction
- **Audio Recording:** Press Button A to record your question
- **Speech-to-Text:** Automatic transcription using OpenAI Whisper API
- **AI Response:** Get answers from your configured LLM model
- **Visual Feedback:** Real-time status updates on device display

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

- M5StickCPlus2 with PDM microphone
- WiFi connection
- USB-C cable for programming

## Setup

### 1. Configure Secrets

Copy `secrets.example.h` to `secrets.h` and configure:

```cpp
// WiFi Configuration
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// OpenWebUI / LLM Configuration
#define OWUI_BASE_URL "https://your-openwebui-instance.com"
#define LLM_API_KEY "your-api-key"
#define LLM_MODEL "your-model-name"

// Optional: Customize system prompt
#define LLM_SYSTEM_PROMPT " Answer in 20 words or less."

// Optional: Configure NTP servers
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"

// Enable OpenWebUI session tracking
#define USE_OWUI_SESSIONS true
```

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
- **Button B:** Start a new chat session (clears conversation history)

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

## Audio Configuration & Memory Constraints

The M5StickCPlus2 (ESP32) has limited RAM available for the audio buffer. The memory usage is calculated as:
`Buffer Size = SAMPLE_RATE * RECORD_SECONDS * 2 bytes`

We have approximately **100KB - 120KB** of safe, contiguous heap available for audio.

### Tested Configurations

| Configuration | Sample Rate | Duration | Buffer Size | Quality | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **High Quality** | 16000 Hz | 3 Seconds | ~96 KB | Excellent | Best for short questions. Clearer transcription. |
| **Long Duration** | 8000 Hz | 5 Seconds | ~80 KB | Good | **Current Default.** Better for longer questions. "Telephone quality". |

### How to Adjust

To change the configuration, edit the top of `m5stickcplus2-openai-answers.ino`:

```cpp
// For High Quality (3 seconds):
static const int SAMPLE_RATE = 16000;
static const int RECORD_SECONDS = 3;

// For Long Duration (5 seconds):
static const int SAMPLE_RATE = 8000;
static const int RECORD_SECONDS = 5;
```

> **Warning:** If you increase both (e.g., 16000Hz for 5 seconds), the device will run out of memory (`Mem Error`) and crash/reboot.

## Architecture

### Message Flow

```
1. User presses Button A
2. Record audio via PDM microphone
3. Transcribe audio → OpenAI Whisper API
4. Create/reuse OpenWebUI chat session
5. Update chat with user message (proper threading)
6. Send completion request with full conversation history
7. Poll chat history for assistant response
8. Save complete chat history with threading
9. Display response on screen
```

### OpenWebUI API Integration

**Endpoints used:**
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
- Voice activity detection
- Wake word support

## License

MIT License - see LICENSE file for details
