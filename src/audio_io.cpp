#include "audio_io.h"
#include <math.h>

namespace {
static float dbfsFromPeak(int32_t peak) {
  if (peak <= 0) return -120.0f;
  return 20.0f * log10f((float)peak / 32768.0f);
}

static float dbfsFromMeanSquare(uint64_t sumSq, uint32_t samples) {
  if (!samples || !sumSq) return -120.0f;
  const double meanSq = (double)sumSq / (double)samples;
  const double rms = sqrt(meanSq);
  if (rms <= 0.0) return -120.0f;
  return (float)(20.0 * log10(rms / 32768.0));
}
}

void AudioIO::configureSpeakerRail() {
  auto spkCfg = M5.Speaker.config();

  // Keep the exact M5Unified Hat SPK2 pin/port selection and use its normal
  // 48 kHz output clock. The 16 kHz Gateway TTS stays 16 kHz logically;
  // M5Unified performs the resampling to the I2S output clock.
  spkCfg.sample_rate = SPEAKER_OUTPUT_SAMPLE_RATE;
  spkCfg.stereo = false;
  spkCfg.buzzer = false;
  spkCfg.use_dac = false;
  spkCfg.magnification = SPEAKER_MAGNIFICATION;
  spkCfg.dma_buf_len = 256;
  spkCfg.dma_buf_count = 8;
  M5.Speaker.config(spkCfg);

  Serial.printf(
      "[audio][spkcfg] dout=%d bck=%d ws=%d port=%d out_rate=%lu stereo=%u buzzer=%u dac=%u mag=%u\n",
      spkCfg.pin_data_out,
      spkCfg.pin_bck,
      spkCfg.pin_ws,
      (int)spkCfg.i2s_port,
      (unsigned long)spkCfg.sample_rate,
      (unsigned)spkCfg.stereo,
      (unsigned)spkCfg.buzzer,
      (unsigned)spkCfg.use_dac,
      (unsigned)spkCfg.magnification);
}

void AudioIO::resetPlaybackStats() {
  _sumSqIn = 0;
  _sumSqOut = 0;
  _streamSamples = 0;
  _limitedSamples = 0;
  _nextLiveLogSample = 16000;
  _peakIn = 0;
  _peakOut = 0;
  _ttsAgcGain = 1.35f;
  _deemphPrev = 0.0f;
}

void AudioIO::conditionTtsBlock(int16_t* data, size_t samples) {
  if (!data || !samples) return;

  uint64_t blockSq = 0;
  for (size_t i = 0; i < samples; ++i) {
    const int32_t x = (int32_t)data[i];
    blockSq += (uint64_t)((int64_t)x * (int64_t)x);
  }

  const float blockRms = sqrtf((float)blockSq / (float)samples);

  // Ignore near-silence when adapting gain so pauses do not cause a huge gain jump.
  if (blockRms > 120.0f) {
    float desired = TTS_LEVEL_TARGET_RMS / blockRms;
    if (desired < TTS_LEVEL_MIN_GAIN) desired = TTS_LEVEL_MIN_GAIN;
    if (desired > TTS_LEVEL_MAX_GAIN) desired = TTS_LEVEL_MAX_GAIN;

    // Gain reduction reacts fairly quickly; gain increase is deliberately
    // slow so speech does not pump between syllables.
    if (desired < _ttsAgcGain) {
      _ttsAgcGain = _ttsAgcGain * 0.35f + desired * 0.65f;
    } else {
      _ttsAgcGain = _ttsAgcGain * 0.92f + desired * 0.08f;
    }
  }

  for (size_t i = 0; i < samples; ++i) {
    const int32_t x = (int32_t)data[i];
    const int32_t ax = x < 0 ? -x : x;

    if (ax > _peakIn) _peakIn = ax;
    _sumSqIn += (uint64_t)((int64_t)x * (int64_t)x);

    float vf = (float)x * _ttsAgcGain;

    // Mild low-pass smoothing takes the brittle edge off the tiny transducer.
    vf = vf * TTS_LP_CURRENT + _deemphPrev * TTS_LP_PREV;
    _deemphPrev = vf;

    int32_t v = (int32_t)vf;
    const bool neg = v < 0;
    int32_t a = neg ? -v : v;

    // Keep most of the 16-bit range available. Compression only starts near
    // the top of the range, rather than crushing normal speech around half scale.
    if (a > TTS_LIMIT_KNEE) {
      ++_limitedSamples;
      a = TTS_LIMIT_KNEE + ((a - TTS_LIMIT_KNEE) / 3);
    }
    if (a > TTS_LIMIT_CEILING) a = TTS_LIMIT_CEILING;

    const int32_t y = neg ? -a : a;
    data[i] = (int16_t)y;

    const int32_t ay = y < 0 ? -y : y;
    if (ay > _peakOut) _peakOut = ay;
    _sumSqOut += (uint64_t)((int64_t)y * (int64_t)y);
    ++_streamSamples;
  }

  // Live diagnostics every ~1 second, no need to wait for audio.end.
  if (_streamSamples >= _nextLiveLogSample) {
    _nextLiveLogSample += 16000;
    Serial.printf(
        "[audio][live] gain=%.2fx in_peak=%.1fdBFS in_rms=%.1fdBFS out_peak=%.1fdBFS out_rms=%.1fdBFS limited=%.2f%%\n",
        _ttsAgcGain,
        dbfsFromPeak(_peakIn),
        dbfsFromMeanSquare(_sumSqIn, _streamSamples),
        dbfsFromPeak(_peakOut),
        dbfsFromMeanSquare(_sumSqOut, _streamSamples),
        _streamSamples ? (100.0f * (float)_limitedSamples / (float)_streamSamples) : 0.0f);
  }
}

void AudioIO::logPlaybackStats() {
  Serial.printf(
      "[audio][level] samples=%lu in_peak=%.1fdBFS in_rms=%.1fdBFS out_peak=%.1fdBFS out_rms=%.1fdBFS limited=%lu (%.2f%%)\n",
      (unsigned long)_streamSamples,
      dbfsFromPeak(_peakIn),
      dbfsFromMeanSquare(_sumSqIn, _streamSamples),
      dbfsFromPeak(_peakOut),
      dbfsFromMeanSquare(_sumSqOut, _streamSamples),
      (unsigned long)_limitedSamples,
      _streamSamples ? (100.0f * (float)_limitedSamples / (float)_streamSamples) : 0.0f);
}

void AudioIO::begin() {
  // M5Unified initialized both devices from board config.
  // Keep speaker off while idle because StickC-Plus SE mic and SPK2 cannot run simultaneously.
  M5.Speaker.end();
  configureSpeakerRail();

  M5.Mic.end();
  auto micCfg = M5.Mic.config();
  micCfg.sample_rate = MIC_SAMPLE_RATE;
  micCfg.noise_filter_level = 16;
  micCfg.magnification = 16;
  M5.Mic.config(micCfg);
  M5.Mic.begin();
}

bool AudioIO::startCapture() {
  if (_capturing) return true;
  stopAutoCapture();
  stopPlayback();
  M5.Speaker.end();
  if (!M5.Mic.isRunning()) M5.Mic.begin();
  _capturing = true;
  _micWrite = 0;
  _nextSeq = 1;
  _lastSentSeq = 0;
  memset(_micSeq, 0, sizeof(_micSeq));
  _inputLevel = 0;
  return true;
}

void AudioIO::serviceCapture() {
  if (!_capturing) return;

  // M5 Mic records asynchronously with a two-slot queue. A ring of four buffers
  // keeps us safely behind the DMA writer before sending the older completed frame.
  uint8_t idx = _micWrite;
  if (M5.Mic.record(_mic[idx], MIC_FRAME_SAMPLES, MIC_SAMPLE_RATE, false)) {
    _micSeq[idx] = _nextSeq++;
    _micWrite = (_micWrite + 1) % MIC_RING;
    sendCompletedMicFrames(false);
  }
}

void AudioIO::sendCompletedMicFrames(bool flushAll) {
  uint32_t safeSeq = 0;
  if (_nextSeq > 3) safeSeq = _nextSeq - 3; // keep two frames behind active recorder queue
  if (flushAll) safeSeq = (_nextSeq > 0) ? _nextSeq - 1 : 0;

  while (_lastSentSeq < safeSeq) {
    uint32_t want = _lastSentSeq + 1;
    int found = -1;
    for (int i = 0; i < MIC_RING; ++i) {
      if (_micSeq[i] == want) { found = i; break; }
    }
    if (found < 0) break;

    _inputLevel = _inputLevel * 0.70f + calcLevel(_mic[found], MIC_FRAME_SAMPLES) * 0.30f;
    if (_sendFn) _sendFn(reinterpret_cast<const uint8_t*>(_mic[found]), MIC_FRAME_SAMPLES * sizeof(int16_t));
    _lastSentSeq = want;
  }
}

void AudioIO::stopCapture() {
  if (!_capturing) return;
  _capturing = false;
  uint32_t start = millis();
  while (M5.Mic.isRecording() && millis() - start < 250) {
    M5.delay(1);
  }
  sendCompletedMicFrames(true);
  _inputLevel = 0;
}


bool AudioIO::startAutoCapture(uint32_t waitTimeoutMs, uint32_t maxUtteranceMs, uint32_t endSilenceMs) {
  if (_capturing) stopCapture();
  if (_playbackActive) stopPlayback();
  if (_autoEnabled) stopAutoCapture();

  M5.Speaker.end();
  if (!M5.Mic.isRunning()) M5.Mic.begin();

  _autoEnabled = true;
  _autoUtterance = false;
  _autoWaitTimeoutMs = waitTimeoutMs;
  _autoMaxUtteranceMs = maxUtteranceMs;
  _autoEndSilenceMs = endSilenceMs;
  _autoArmedAt = millis();
  _autoSpeechStartedAt = 0;
  _autoLastSpeechAt = 0;
  _autoHotFrames = 0;
  _autoNoise = 0.012f;
  _autoPreWrite = 0;
  _autoPreCount = 0;
  _micWrite = 0;
  _nextSeq = 1;
  _lastSentSeq = 0;
  memset(_micSeq, 0, sizeof(_micSeq));
  _inputLevel = 0;
  return true;
}

void AudioIO::serviceAutoCapture() {
  if (!_autoEnabled) return;

  const uint32_t now = millis();
  if (!_autoUtterance && _autoWaitTimeoutMs && now - _autoArmedAt >= _autoWaitTimeoutMs) {
    _autoEnabled = false;
    _autoPreCount = 0;
    _autoHotFrames = 0;
    _inputLevel = 0;
    if (_autoTimeoutFn) _autoTimeoutFn();
    return;
  }

  const uint8_t idx = _micWrite;
  if (M5.Mic.record(_mic[idx], MIC_FRAME_SAMPLES, MIC_SAMPLE_RATE, false)) {
    _micSeq[idx] = _nextSeq++;
    _micWrite = (_micWrite + 1) % MIC_RING;
    processCompletedAutoFrames();
  }
}

void AudioIO::processCompletedAutoFrames() {
  if (!_autoEnabled) return;
  uint32_t safeSeq = 0;
  if (_nextSeq > 3) safeSeq = _nextSeq - 3;

  while (_autoEnabled && _lastSentSeq < safeSeq) {
    const uint32_t want = _lastSentSeq + 1;
    int found = -1;
    for (int i = 0; i < MIC_RING; ++i) {
      if (_micSeq[i] == want) { found = i; break; }
    }
    if (found < 0) break;
    _lastSentSeq = want;
    processAutoFrame(_mic[found]);
  }
}

void AudioIO::processAutoFrame(const int16_t* data) {
  if (!_autoEnabled || !data) return;

  const uint32_t now = millis();
  const float level = calcLevel(data, MIC_FRAME_SAMPLES);
  _inputLevel = _inputLevel * 0.72f + level * 0.28f;

  const float startThreshold = max(AUTO_VAD_ABS_START, _autoNoise * AUTO_VAD_START_MULT + 0.010f);
  const float endThreshold = max(AUTO_VAD_ABS_END, _autoNoise * AUTO_VAD_END_MULT + 0.008f);

  if (!_autoUtterance) {
    memcpy(_autoPre[_autoPreWrite], data, MIC_FRAME_SAMPLES * sizeof(int16_t));
    _autoPreWrite = (_autoPreWrite + 1) % AUTO_VAD_PRE_ROLL_FRAMES;
    if (_autoPreCount < AUTO_VAD_PRE_ROLL_FRAMES) ++_autoPreCount;

    if (level < startThreshold) {
      _autoNoise = _autoNoise * 0.985f + level * 0.015f;
      if (_autoNoise < 0.003f) _autoNoise = 0.003f;
      if (_autoNoise > 0.120f) _autoNoise = 0.120f;
    }

    if (level >= startThreshold) {
      if (_autoHotFrames < 255) ++_autoHotFrames;
    } else {
      _autoHotFrames = 0;
    }

    if (_autoHotFrames >= AUTO_VAD_START_FRAMES) {
      _autoUtterance = true;
      _autoSpeechStartedAt = now;
      _autoLastSpeechAt = now;
      if (_autoStartFn) _autoStartFn();
      pushAutoPreRoll();
      Serial.printf("[voice-vad] speech start level=%.3f noise=%.3f start=%.3f\n",
                    level, _autoNoise, startThreshold);
    }
    return;
  }

  if (_sendFn) {
    _sendFn(reinterpret_cast<const uint8_t*>(data), MIC_FRAME_SAMPLES * sizeof(int16_t));
  }

  if (level >= endThreshold) _autoLastSpeechAt = now;

  const bool maxed = _autoMaxUtteranceMs && now - _autoSpeechStartedAt >= _autoMaxUtteranceMs;
  const bool silent = _autoEndSilenceMs && now - _autoLastSpeechAt >= _autoEndSilenceMs;
  if (maxed || silent) {
    _autoEnabled = false;
    _autoUtterance = false;
    _autoPreCount = 0;
    _autoHotFrames = 0;
    _inputLevel = 0;
    Serial.printf("[voice-vad] speech stop reason=%s duration=%lums\n",
                  maxed ? "max" : "silence",
                  (unsigned long)(now - _autoSpeechStartedAt));
    if (_autoStopFn) _autoStopFn();
  }
}

void AudioIO::pushAutoPreRoll() {
  if (!_sendFn || !_autoPreCount) {
    _autoPreCount = 0;
    return;
  }

  const uint8_t start = (uint8_t)((_autoPreWrite + AUTO_VAD_PRE_ROLL_FRAMES - _autoPreCount) % AUTO_VAD_PRE_ROLL_FRAMES);
  for (uint8_t i = 0; i < _autoPreCount; ++i) {
    const uint8_t idx = (uint8_t)((start + i) % AUTO_VAD_PRE_ROLL_FRAMES);
    _sendFn(reinterpret_cast<const uint8_t*>(_autoPre[idx]), MIC_FRAME_SAMPLES * sizeof(int16_t));
  }
  _autoPreCount = 0;
}

void AudioIO::stopAutoCapture() {
  if (!_autoEnabled && !_autoUtterance) return;
  _autoEnabled = false;
  _autoUtterance = false;
  _autoPreCount = 0;
  _autoHotFrames = 0;
  _inputLevel = 0;
}

void AudioIO::startPlayback(uint32_t sampleRate) {
  if (_capturing) stopCapture();
  stopAutoCapture();

  uint32_t announcedRate = sampleRate ? sampleRate : TTS_DEFAULT_SAMPLE_RATE;

  // playRaw() expects the sampling rate of the RAW SOURCE data.
  // M5Unified then converts that source rate to the configured 48 kHz I2S rail.
  // Do NOT force every TTS stream to 16 kHz: if the Gateway announces 12 kHz,
  // 24 kHz, etc., forcing 16 kHz changes both speed and pitch.
  if (announcedRate < 8000 || announcedRate > 48000) {
    Serial.printf("[audio][WARN] invalid announced source rate=%lu, fallback=%lu\n",
                  (unsigned long)announcedRate,
                  (unsigned long)TTS_DEFAULT_SAMPLE_RATE);
    announcedRate = TTS_DEFAULT_SAMPLE_RATE;
  }
  _playRate = announcedRate;

  if (_playbackActive && M5.Speaker.isRunning()) return;

  M5.Mic.end();

  configureSpeakerRail();

  M5.Speaker.begin();
  M5.Speaker.setVolume(SPEAKER_VOLUME);
  M5.Speaker.setChannelVolume(0, SPEAKER_CHANNEL_VOLUME);

  _spkIndex = 0;
  _outputLevel = 0;
  _playbackActive = true;
  _finishRequested = false;
  _queuedChunks = 0;
  _droppedChunks = 0;
  resetPlaybackStats();

  Serial.printf(
      "[audio] playback START announced_rate=%lu source_rate=%lu out_rate=%lu master=%u channel=%u magnification=%u level_target=%.0f\n",
      (unsigned long)announcedRate,
      (unsigned long)_playRate,
      (unsigned long)SPEAKER_OUTPUT_SAMPLE_RATE,
      (unsigned)SPEAKER_VOLUME,
      (unsigned)SPEAKER_CHANNEL_VOLUME,
      (unsigned)SPEAKER_MAGNIFICATION,
      TTS_LEVEL_TARGET_RMS);
}

void AudioIO::pushPlayback(const uint8_t* data, size_t len) {
  if (!data || len < 2) return;
  if (!M5.Speaker.isRunning()) startPlayback(_playRate);

  _finishRequested = false;

  // Raw transport is PCM16 little-endian mono. Never silently skip a chunk when
  // the M5Unified speaker queue is temporarily full: dropped chunks make speech
  // sound unnaturally rushed even when the nominal sample rate is correct.
  while (len >= 2) {
    const size_t samples = min((size_t)SPEAKER_RING_SAMPLES, len / 2);
    memcpy(_spk[_spkIndex], data, samples * sizeof(int16_t));
    conditionTtsBlock(_spk[_spkIndex], samples);
    _outputLevel = _outputLevel * 0.68f
                 + calcLevel(_spk[_spkIndex], samples) * 0.32f;

    const uint32_t waitStart = millis();
    bool queued = false;
    while (!(queued = M5.Speaker.playRaw(
                 _spk[_spkIndex], samples, _playRate,
                 false, 1, 0, false))) {
      if (millis() - waitStart > 750) break;
      M5.delay(1);
    }

    if (!queued) {
      ++_droppedChunks;
      Serial.printf("[audio][WARN] speaker queue timeout, dropped chunk #%lu (%u samples)\n",
                    (unsigned long)_droppedChunks, (unsigned)samples);
    } else {
      ++_queuedChunks;
    }

    _spkIndex = (_spkIndex + 1) % 3;
    data += samples * sizeof(int16_t);
    len  -= samples * sizeof(int16_t);
  }
}

void AudioIO::requestPlaybackFinish() {
  if (!_playbackActive) return;
  if (_finishRequested) return;
  _finishRequested = true;
  Serial.printf("[audio] stream END received, draining speaker queue (queued=%lu dropped=%lu)\n",
                (unsigned long)_queuedChunks,
                (unsigned long)_droppedChunks);
}

void AudioIO::servicePlayback() {
  if (!_playbackActive || !_finishRequested) return;
  if (M5.Speaker.isPlaying()) return;

  M5.Speaker.end();
  _outputLevel = 0;
  _playbackActive = false;
  _finishRequested = false;

  logPlaybackStats();

  if (!M5.Mic.isRunning()) M5.Mic.begin();

  Serial.printf("[audio] playback DRAINED actual_rate=%lu queued=%lu dropped=%lu\n",
                (unsigned long)_playRate,
                (unsigned long)_queuedChunks,
                (unsigned long)_droppedChunks);
}

void AudioIO::playDiagnosticTone() {
  if (_capturing) return;

  stopAutoCapture();
  stopPlayback();
  M5.Mic.end();

  // Force the Hat 5 V rail on for this hardware test.
  M5.Power.setExtOutput(true);

  configureSpeakerRail();
  M5.Speaker.begin();
  M5.Speaker.setVolume(255);
  M5.Speaker.setChannelVolume(0, 255);

  Serial.printf(
      "[audio][diag] LOCAL TONE start ext5v=%u vbus=%dmV bat=%dmV master=255 mag=%u\n",
      (unsigned)M5.Power.getExtOutput(),
      (int)M5.Power.getVBUSVoltage(),
      (int)M5.Power.getBatteryVoltage(),
      (unsigned)SPEAKER_MAGNIFICATION);

  M5.Speaker.tone(600.0f, 650, 0, true);
  while (M5.Speaker.isPlaying()) { M5.delay(1); M5.update(); }
  M5.delay(120);
  M5.Speaker.tone(1000.0f, 650, 0, true);
  while (M5.Speaker.isPlaying()) { M5.delay(1); M5.update(); }

  M5.Speaker.end();
  if (!M5.Mic.isRunning()) M5.Mic.begin();

  Serial.println("[audio][diag] LOCAL TONE done");
}

void AudioIO::stopPlayback() {
  if (M5.Speaker.isRunning()) {
    M5.Speaker.stop();
    uint32_t start = millis();
    while (M5.Speaker.isPlaying() && millis() - start < 350) M5.delay(1);
    M5.Speaker.end();
  }
  _outputLevel = 0;
  _playbackActive = false;
  _finishRequested = false;
  if (!M5.Mic.isRunning()) M5.Mic.begin();
}

float AudioIO::calcLevel(const int16_t* data, size_t samples) const {
  if (!data || samples == 0) return 0;
  uint64_t acc = 0;
  for (size_t i = 0; i < samples; i += 4) {
    int32_t v = abs((int32_t)data[i]);
    acc += v;
  }
  float avg = float(acc) / float((samples + 3) / 4);
  float n = avg / 6500.0f;
  if (n < 0) n = 0;
  if (n > 1) n = 1;
  return n;
}
