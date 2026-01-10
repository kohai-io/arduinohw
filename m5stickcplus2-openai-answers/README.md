# M5StickCPlus2 OpenAI Voice Assistant

This project turns your M5StickCPlus2 into a voice assistant using OpenAI's Whisper (for transcription) and GPT models (for answers).

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

## Setup

1.  Open `secrets.h` and add your WiFi credentials and OpenAI API Key.
2.  Compile and upload using Arduino IDE or `arduino-cli`.
3.  Press **Button A** to record.
