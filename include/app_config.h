#pragma once

// HomeAIAgent Mini A1.36.4 A6 NVS - Dock20 clean: no automatic voice wake
// Target: M5Stack StickC-Plus SE + Hat SPK2

#define HOMEAI_FW_VERSION      "A1.36.4-a6-nvs-dock20"
#define HOMEAI_DEVICE_ID       "homeai-mini-bedroom-01"
#define HOMEAI_DEVICE_NAME     "HomeAIAgent Mini"

// Copy secrets.example.h to secrets.h and fill in Wi-Fi / Gateway values.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

// UI / behavior
static constexpr uint16_t UI_FPS = 15;
static constexpr uint16_t UI_ACTIVE_FPS = 18;
static constexpr uint8_t  LCD_BRIGHT_DOCK_ACTIVE = 105;
static constexpr uint8_t  LCD_BRIGHT_DOCK_STANDBY = 51; // 20% of the 0..255 backlight range
static constexpr uint8_t  LCD_BRIGHT_HAND = 115;
static constexpr uint8_t  LCD_BRIGHT_NIGHT = 105;
// SPK2 clean-voice rail.
//
// Official Hat SPK2 path:
//   MAX98357 I2S amp, GPIO25 DATA / GPIO26 BCK / GPIO0 WS,
//   external 5 V enabled, M5Unified external_speaker.hat_spk2 = true.
//
// Important: the Hat contains a 1 W speaker even though the MAX98357 amp itself
// can reach higher output power under some load/supply conditions. A1.34 avoids
// brute-force clipping and instead keeps a clean near-full-scale PCM signal.
static constexpr uint8_t  SPEAKER_VOLUME = 232;
static constexpr uint8_t  SPEAKER_CHANNEL_VOLUME = 255;
static constexpr uint8_t  SPEAKER_MAGNIFICATION = 16;   // M5Unified default
static constexpr uint32_t SPEAKER_OUTPUT_SAMPLE_RATE = 48000;

// Conservative TTS leveling.
// Previous builds allowed up to 8x gain and then hard-limited near half scale,
// which can sound thin/harsh while still not sounding subjectively loud.
static constexpr float    TTS_LEVEL_TARGET_RMS = 6500.0f; // ~ -14 dBFS
static constexpr float    TTS_LEVEL_MIN_GAIN = 0.85f;
static constexpr float    TTS_LEVEL_MAX_GAIN = 2.50f;
static constexpr int32_t  TTS_LIMIT_KNEE = 25000;
static constexpr int32_t  TTS_LIMIT_CEILING = 30000;

// Mild one-pole smoothing to tame the small speaker's sharp top end without
// turning speech muddy.
static constexpr float    TTS_LP_CURRENT = 0.72f;
static constexpr float    TTS_LP_PREV = 0.28f;

// Audio transport: signed 16-bit little-endian, mono PCM.
static constexpr uint32_t MIC_SAMPLE_RATE = 16000;
static constexpr uint16_t MIC_FRAME_SAMPLES = 320;   // 20 ms @ 16 kHz
static constexpr uint32_t TTS_DEFAULT_SAMPLE_RATE = 16000;
static constexpr uint32_t TTS_PLAYBACK_SAMPLE_RATE = 16000;
static constexpr uint16_t SPEAKER_RING_SAMPLES = 1024;


// Follow-up only: Gateway arms an authenticated followup.arm after audio playback.
// No local or Gateway-ASR wake listener is compiled into this release.
static constexpr uint32_t DOCK_COMMAND_MAX_MS = 15000;
static constexpr uint32_t DOCK_COMMAND_END_SILENCE_MS = 1200;
static constexpr uint8_t AUTO_VAD_PRE_ROLL_FRAMES = 12; // 240 ms at 20 ms/frame
static constexpr float AUTO_VAD_ABS_START = 0.055f;
static constexpr float AUTO_VAD_ABS_END = 0.032f;
static constexpr float AUTO_VAD_START_MULT = 2.40f;
static constexpr float AUTO_VAD_END_MULT = 1.65f;
static constexpr uint8_t AUTO_VAD_START_FRAMES = 2;

// Power / dock detection.
static constexpr int16_t VBUS_DOCK_THRESHOLD_MV = 4200;
static constexpr uint32_t DOCK_DEBOUNCE_MS = 800;

// Network timing.
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr uint32_t WS_RECONNECT_MS = 3000;
static constexpr uint32_t NTP_RETRY_MS = 30000;
static constexpr uint32_t NET_DIAG_INTERVAL_MS = 5000;
static constexpr uint32_t GATEWAY_PROBE_INTERVAL_MS = 10000;

// Demo fallback lets you validate UI even if Gateway is not online yet.
static constexpr bool ENABLE_DEMO_FALLBACK = true;

// Clock is synced from internet NTP in UTC, then converted explicitly to UTC+8.
// We do not rely on libc/POSIX timezone parsing, avoiding sign/locale ambiguity.
static constexpr int32_t UTC_OFFSET_SECONDS = 8 * 60 * 60;
static constexpr const char* NTP_SERVER_1 = "ntp.aliyun.com";
static constexpr const char* NTP_SERVER_2 = "ntp.tencent.com";
static constexpr const char* NTP_SERVER_3 = "time.cloudflare.com";
