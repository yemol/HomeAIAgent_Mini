#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "app_config.h"

class AudioIO {
public:
  using BinarySendFn = bool (*)(const uint8_t* data, size_t len);
  using AutoEventFn = void (*)();

  void begin();
  void setSendCallback(BinarySendFn fn) { _sendFn = fn; }
  void setAutoCaptureCallbacks(AutoEventFn onStart, AutoEventFn onStop, AutoEventFn onTimeout) {
    _autoStartFn = onStart;
    _autoStopFn = onStop;
    _autoTimeoutFn = onTimeout;
  }
  bool startCapture();
  void serviceCapture();
  void stopCapture();
  bool capturing() const { return _capturing; }
  float inputLevel() const { return _inputLevel; }

  // One-shot VAD capture used only for authenticated Follow-up after PTT playback.
  // It waits locally for speech, prepends a short local pre-roll, streams only the
  // utterance PCM through the existing binary callback, then stops on silence.
  bool startAutoCapture(uint32_t waitTimeoutMs, uint32_t maxUtteranceMs, uint32_t endSilenceMs);
  void serviceAutoCapture();
  void stopAutoCapture();
  bool autoCapturing() const { return _autoEnabled; }
  bool autoUtteranceActive() const { return _autoUtterance; }

  void startPlayback(uint32_t sampleRate = TTS_DEFAULT_SAMPLE_RATE);
  void pushPlayback(const uint8_t* data, size_t len);
  void requestPlaybackFinish();
  void servicePlayback();
  void stopPlayback();
  void playDiagnosticTone();
  bool playing() const { return _playbackActive; }
  bool finishing() const { return _finishRequested; }
  uint32_t playbackRate() const { return _playRate; }
  uint32_t droppedChunks() const { return _droppedChunks; }
  float outputLevel() const { return _outputLevel; }

private:
  static constexpr uint8_t MIC_RING = 4;
  int16_t _mic[MIC_RING][MIC_FRAME_SAMPLES]{};
  uint32_t _micSeq[MIC_RING]{};
  uint8_t _micWrite = 0;
  uint32_t _nextSeq = 1;
  uint32_t _lastSentSeq = 0;
  bool _capturing = false;
  BinarySendFn _sendFn = nullptr;
  AutoEventFn _autoStartFn = nullptr;
  AutoEventFn _autoStopFn = nullptr;
  AutoEventFn _autoTimeoutFn = nullptr;
  float _inputLevel = 0;

  // Automatic utterance detector. Reuses the microphone DMA ring above, so it
  // is strictly exclusive with normal button PTT capture and speaker playback.
  int16_t _autoPre[AUTO_VAD_PRE_ROLL_FRAMES][MIC_FRAME_SAMPLES]{};
  uint8_t _autoPreWrite = 0;
  uint8_t _autoPreCount = 0;
  bool _autoEnabled = false;
  bool _autoUtterance = false;
  uint32_t _autoWaitTimeoutMs = 0;
  uint32_t _autoMaxUtteranceMs = 0;
  uint32_t _autoEndSilenceMs = 0;
  uint32_t _autoArmedAt = 0;
  uint32_t _autoSpeechStartedAt = 0;
  uint32_t _autoLastSpeechAt = 0;
  uint8_t _autoHotFrames = 0;
  float _autoNoise = 0.012f;

  int16_t _spk[3][SPEAKER_RING_SAMPLES]{};
  uint8_t _spkIndex = 0;
  uint32_t _playRate = TTS_DEFAULT_SAMPLE_RATE;
  float _outputLevel = 0;
  bool _playbackActive = false;
  bool _finishRequested = false;
  uint32_t _queuedChunks = 0;
  uint32_t _droppedChunks = 0;

  // TTS stream diagnostics / conditioner state.
  uint64_t _sumSqIn = 0;
  uint64_t _sumSqOut = 0;
  uint32_t _streamSamples = 0;
  uint32_t _limitedSamples = 0;
  uint32_t _nextLiveLogSample = 16000;
  int32_t _peakIn = 0;
  int32_t _peakOut = 0;
  float _ttsAgcGain = 3.0f;
  float _deemphPrev = 0.0f;

  void sendCompletedMicFrames(bool flushAll);
  void processCompletedAutoFrames();
  void processAutoFrame(const int16_t* data);
  void pushAutoPreRoll();
  void configureSpeakerRail();
  void resetPlaybackStats();
  void conditionTtsBlock(int16_t* data, size_t samples);
  void logPlaybackStats();
  float calcLevel(const int16_t* data, size_t samples) const;
};
