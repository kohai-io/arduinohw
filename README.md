# M5 Voice Assistant

Multi-device voice assistant for M5Stack devices with OpenAI/OpenWebUI integration.

## Supported Devices

- **M5StickC Plus2** - Compact voice assistant
- **M5Stack Core2** - Full-featured with touchscreen and speaker
- **M5Stack CoreS3** - Latest generation with enhanced features
- **M5GO-Bottom2** - LED expansion module with 10 NeoPixel LEDs (Core2/CoreS3 only)

## Features

- 🎤 **Voice Recording** with Voice Activity Detection (VAD)
- 🗣️ **Speech-to-Text** via OpenAI Whisper or OpenWebUI
- 🤖 **LLM Integration** (OpenAI GPT or OpenWebUI models)
- 🔊 **Text-to-Speech** (Core2/CoreS3 only)
- � **Camera Support** (CoreS3 only - experimental)
  - Live camera preview with tap-to-capture
  - Vision model integration for image analysis
  - Software horizontal mirror correction for GC0308 sensor
- � **LED Visual Feedback** (with M5GO-Bottom2)
  - Real-time audio level visualization during recording
  - Status indicators for WiFi, transcription, AI thinking, and TTS
  - Automatic detection and initialization
- 📊 **Real-time Audio Level Display**
- 💾 **Chat History** (OpenWebUI sessions)
- 🎚️ **Multiple Audio Profiles** (quality/duration presets)
- ⚙️ **Device-Specific Configurations**
  - Hardware features configured per device
  - UI mode (touch vs physical buttons)
  - Optimized audio quality (16kHz default for Core2/CoreS3)

## Project Structure

```
m5-voice-assistant/
├── README.md                          # This file - setup guide
├── secrets.example.h                  # Template for credentials
├── common/                            # Shared code (all devices)
│   ├── api_functions.h                # API declarations
│   ├── audio.h                        # Recording & WAV generation
│   ├── display.h                      # Screen rendering & UI
│   └── image_upload.h                 # Image upload (camera)
├── m5-voice-assistant-stickc/         # M5StickC Plus/Plus2
│   ├── m5-voice-assistant-stickc.ino
│   ├── device_config.h
│   └── m5go_leds.h
├── m5-voice-assistant-core2/          # M5Stack Core2
│   ├── m5-voice-assistant-core2.ino
│   ├── device_config.h
│   ├── m5go_leds.h
│   └── touch_ui.h
└── m5-voice-assistant-cores3/         # M5Stack CoreS3 & CoreS3 Lite
    ├── m5-voice-assistant-cores3.ino
    ├── device_config.h
    ├── camera.h                       # (experimental)
    ├── touch_ui.h
    └── m5go_leds.h
```

## Getting Started

### 1. Choose Your Device Folder
Navigate to the folder for your M5Stack device:
- **M5StickC Plus/Plus2**: Use `m5-voice-assistant-stickc/`
- **M5Stack Core2**: Use `m5-voice-assistant-core2/`
- **M5Stack CoreS3 or CoreS3 Lite**: Use `m5-voice-assistant-cores3/`

### 2. Configure Credentials
Copy `secrets.example.h` to `secrets.h` in the root folder and fill in your credentials:
```cpp
// WiFi
const char* WIFI_SSID = "your-wifi-ssid";
const char* WIFI_PASS = "your-wifi-password";

// API Keys
const char* STT_API_KEY = "your-openai-key";
const char* LLM_API_KEY = "your-openai-key";
```

### 3. Open and Upload
Open the `.ino` file from your device folder in Arduino IDE and upload to your device.

## M5GO-Bottom2 LED Features

When the M5GO-Bottom2 module is attached to your Core2/CoreS3, the assistant automatically detects it and provides rich visual feedback through 10 NeoPixel LEDs.

### Automatic Detection
- Detects M5GO-Bottom2 on startup (Core2/CoreS3 only)
- Brief blue flash confirms successful initialization
- No configuration needed - works out of the box

### LED Status Indicators

| State | Color | Pattern |
|-------|-------|---------|
| WiFi Connecting | Yellow | Pulsing on/off |
| WiFi Connected | Green | Brief flash (500ms) |
| Recording Ready | Blue | Solid (ready to listen) |
| Speaking Detected | Green | Dynamic level bars (1-10 LEDs) |
| Listening (Silence) | Blue | 2 LEDs (waiting for speech) |
| Transcribing | Cyan | Breathing animation |
| AI Thinking | Purple | Solid (processing) |
| TTS Speaking | Orange | Solid (audio playback) |

### Real-Time Audio Visualization
During recording, the LEDs dynamically respond to your voice:
- **Volume-based**: More LEDs light up as you speak louder (1-10 LEDs)
- **Color-coded**: Green when speaking, blue when silent
- **Instant feedback**: See your audio levels in real-time
- **VAD integration**: Visual confirmation of voice activity detection

## Audio Profiles

### M5StickC Plus2
- **Standard** (default) - 8kHz, 5s (80KB RAM)
- **HQ Short** - 16kHz, 3s (96KB RAM)

### Core2/CoreS3
- **HQ** (default) - 16kHz, 5s (160KB RAM) - Better STT accuracy
- **Standard** - 8kHz, 8s (128KB RAM)
- **Long** - 8kHz, 15s (240KB RAM)

## Button Controls

### Button A
- **Click** - Start voice recording

### Button B
- **Click** - Start new chat session
- **Hold 2s** - Cycle audio profile

### Button C (Core2/CoreS3 only)
- **Click** - Replay last TTS audio
- **Hold 2s** - Toggle TTS voice

## Setup

1. **Install Libraries**
   - M5Unified
   - FastLED
   - arduino-libhelix (for TTS)

2. **Configure Credentials**
   ```bash
   cp secrets.example.h secrets.h
   # Edit secrets.h with your WiFi and API keys
   ```

3. **Upload to Device**
   - Select your M5 device in Arduino IDE
   - Upload the sketch

## Configuration Options

### Device-Specific Configuration (`device_config.h`)

Each device folder has its own `device_config.h` with hardware-specific settings:

**Core2:**
```cpp
#define USE_PHYSICAL_BUTTONS true   // Use A/B/C buttons
#define ENABLE_TOUCH_UI false        // Disable touch UI
#define ENABLE_CAMERA false          // Set true if camera module connected
#define ENABLE_M5GO_LEDS true        // M5GO-Bottom2 LED ring
```

**CoreS3:**
```cpp
#define USE_PHYSICAL_BUTTONS false  // No physical buttons
#define ENABLE_TOUCH_UI true         // Enable touch UI
#define ENABLE_CAMERA true           // Built-in GC0308 camera
#define ENABLE_M5GO_LEDS false       // Disabled (GPIO 25 conflict with camera)
```

**StickC Plus2:**
```cpp
#define USE_PHYSICAL_BUTTONS true   // Button A only
#define ENABLE_TOUCH_UI false        // No touch screen
#define ENABLE_CAMERA false          // No camera
#define ENABLE_M5GO_LEDS false       // Optional accessory
```

### API Configuration (`secrets.h`)

See `secrets.h` for API credentials and settings:
- WiFi credentials
- API endpoints (OpenAI/OpenWebUI)
- STT/LLM/TTS settings
- System prompts
- Voice selection

## Voice Activity Detection (VAD)

Automatically stops recording after 1.5 seconds of silence (configurable in main `.ino` file):
- `VAD_SILENCE_THRESHOLD` - RMS level for silence detection (default: 500)
- `VAD_SILENCE_DURATION` - Seconds of silence before stop (default: 1.5)
- `VAD_ENABLED` - Enable/disable VAD (default: true)

When using M5GO-Bottom2, VAD status is visually indicated through LED colors and patterns.

## Memory Usage

The code dynamically allocates audio buffers based on selected profile. Free heap is logged throughout operation for monitoring.

## License

MIT License - See repository for details
