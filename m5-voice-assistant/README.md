# M5 Voice Assistant

Multi-device voice assistant for M5Stack devices with OpenAI/OpenWebUI integration.

## Supported Devices

- **M5StickC Plus2** - Compact voice assistant
- **M5Stack Core2** - Full-featured with touchscreen and speaker
- **M5Stack CoreS3** - Latest generation with enhanced features
- **M5GO-Bottom2** - LED expansion module (Core2/CoreS3 only)

## Features

- 🎤 **Voice Recording** with Voice Activity Detection (VAD)
- 🗣️ **Speech-to-Text** via OpenAI Whisper or OpenWebUI
- 🤖 **LLM Integration** (OpenAI GPT or OpenWebUI models)
- 🔊 **Text-to-Speech** (Core2/CoreS3 only)
- 💡 **LED Visual Feedback** (with M5GO-Bottom2)
- 📊 **Real-time Audio Level Display**
- 💾 **Chat History** (OpenWebUI sessions)
- 🎚️ **Multiple Audio Profiles** (quality/duration presets)

## Project Structure

```
m5stickcplus2-openai-answers/
├── m5stickcplus2-openai-answers.ino  # Main application
├── secrets.h                          # WiFi & API credentials
├── secrets.example.h                  # Template for credentials
├── config.h                           # Device detection & profiles
├── audio.h                            # Recording & WAV generation
├── display.h                          # Screen rendering & UI
├── m5go_leds.h                        # LED control (M5GO-Bottom2)
├── network.h                          # API declarations
└── README.md                          # This file
```

## LED Status Indicators (M5GO-Bottom2)

| State | Color | Pattern |
|-------|-------|---------|
| WiFi Connecting | Yellow | Pulsing |
| WiFi Connected | Green | Flash |
| Recording Ready | Blue | Solid |
| Speaking Detected | Green | Level bars (1-10) |
| Listening (Silence) | Blue | 2 LEDs |
| Transcribing | Cyan | Breathing |
| AI Thinking | Purple | Solid |
| TTS Speaking | Orange | Solid |

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

Automatically stops recording after 1.5 seconds of silence (configurable in `config.h`):
- `VAD_SILENCE_THRESHOLD` - RMS level for silence detection (default: 500)
- `VAD_SILENCE_DURATION` - Seconds of silence before stop (default: 1.5)
- `VAD_ENABLED` - Enable/disable VAD (default: true)

## Memory Usage

The code dynamically allocates audio buffers based on selected profile. Free heap is logged throughout operation for monitoring.

## License

MIT License - See repository for details
