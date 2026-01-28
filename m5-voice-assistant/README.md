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
- 💡 **LED Visual Feedback** (with M5GO-Bottom2)
  - Real-time audio level visualization during recording
  - Status indicators for WiFi, transcription, AI thinking, and TTS
  - Automatic detection and initialization
- 📊 **Real-time Audio Level Display**
- 💾 **Chat History** (OpenWebUI sessions)
- 🎚️ **Multiple Audio Profiles** (quality/duration presets)

## Project Structure

```
m5-voice-assistant/
├── m5-voice-assistant.ino  # Main application
├── secrets.h               # WiFi & API credentials
├── secrets.example.h       # Template for credentials
├── device_config.h         # Device detection & profiles
├── audio.h                 # Recording & WAV generation
├── display.h               # Screen rendering & UI
├── m5go_leds.h            # LED control (M5GO-Bottom2)
├── api_functions.h        # API declarations
└── README.md              # This file
```

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
- **Standard** - 8kHz, 5s (80KB RAM)
- **HQ Short** - 16kHz, 3s (96KB RAM)

### Core2/CoreS3
- **Standard** - 8kHz, 8s (128KB RAM)
- **Long** - 8kHz, 15s (240KB RAM)
- **HQ Short** - 16kHz, 5s (160KB RAM)

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

See `secrets.h` for detailed configuration:
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
