#include "ui_face.h"
#include <time.h>

namespace {
M5Canvas sFaceCanvas(&M5.Display);
M5Canvas sClockCanvas(&M5.Display);

constexpr uint8_t kDigit5x7[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x14,0x04,0x04,0x04,0x1F},
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
  {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}
};

constexpr uint8_t kNeutralAnim[] = {0, 1, 2, 3};
}

void FaceUI::begin() {
  M5.Display.setRotation(0);
  M5.Display.wakeup();
  M5.Display.setBrightness(LCD_BRIGHT_DOCK_ACTIVE);
  _brightness = LCD_BRIGHT_DOCK_ACTIVE;
  M5.Display.fillScreen(TFT_BLACK);

  sFaceCanvas.setColorDepth(16);
  sFaceCanvas.createSprite(FaceAssets::FRAME_W, FaceAssets::FRAME_H);
  sFaceCanvas.fillSprite(TFT_BLACK);

  sClockCanvas.setColorDepth(16);
  sClockCanvas.createSprite(135, 75);
  sClockCanvas.fillSprite(TFT_BLACK);

  randomSeed((uint32_t)micros());
  _clockCache = currentTimeString();
  resetAnimation(millis());
  showFrame(0);
  renderClock();
}

void FaceUI::setState(FaceState state, bool force) {
  if (!force && state == _state && !_demoOverride) return;
  _demoOverride = false;
  _state = state;
  const VisualFace next = visualForState();
  const bool changed = next != _visual;
  _visual = next;
  resetAnimation(millis());
  if (changed || force) showFrame(0);
}

void FaceUI::setStatus(const DeviceStatus& status) {
  const VisualFace before = visualForState();
  _status = status;
  const VisualFace after = visualForState();
  if (!_demoOverride && before != after) {
    _visual = after;
    resetAnimation(millis());
    showFrame(0);
  }
}

void FaceUI::setMessage(const String& text, uint32_t holdMs) {
  (void)text;
  (void)holdMs;
}

void FaceUI::setAudioLevel(float level01) {
  if (level01 < 0.0f) level01 = 0.0f;
  if (level01 > 1.0f) level01 = 1.0f;
  _audioLevel = level01;
}

void FaceUI::setBrightness(uint8_t level) {
  if (_brightness == level) return;
  _brightness = level;
  M5.Display.wakeup();
  M5.Display.setBrightness(level);
  Serial.printf("[ui] brightness=%u\n", (unsigned)level);
}

void FaceUI::forceRedraw() {
  _lastAbsoluteFrame = 255;
  showFrame(_frameInGroup);
  renderClock();
}

void FaceUI::cycleDemoExpression() {
  static const char* kFaceNames[8] = {
    "NEUTRAL", "WINK", "HAPPY", "SMUG",
    "CONFUSED", "CRYING", "SLEEPY", "OFFLINE"
  };

  _demoOverride = true;
  _demoIndex = (_demoIndex + 1) & 0x07;
  _visual = static_cast<VisualFace>(_demoIndex);
  resetAnimation(millis());
  showFrame(0);
  Serial.printf("[ui][preview] %u/8 %s hold=10s\n",
                (unsigned)(_demoIndex + 1), kFaceNames[_demoIndex]);
}

void FaceUI::endDemoExpression() {
  if (!_demoOverride) return;
  _demoOverride = false;
  _visual = visualForState();
  resetAnimation(millis());
  showFrame(0);
  Serial.printf("[ui][preview] timeout -> live=%u\n", (unsigned)_visual);
}

bool FaceUI::isOfflineFace() const {
  if (_demoOverride) return _visual == VisualFace::OFFLINE;
  if (_state == FaceState::ERROR_STATE) return true;
  const bool idle = (_state == FaceState::DOCK_IDLE || _state == FaceState::HAND_IDLE);
  // Red OFFLINE now means actual Wi-Fi loss only. Gateway/WebSocket loss is a separate state.
  return idle && !_status.wifi;
}

bool FaceUI::isGatewayWaiting() const {
  if (_demoOverride) return false;
  const bool idle = (_state == FaceState::DOCK_IDLE || _state == FaceState::HAND_IDLE);
  return idle && _status.wifi && !_status.websocket;
}

FaceUI::VisualFace FaceUI::visualForState() const {
  if (isOfflineFace()) return VisualFace::OFFLINE;
  if (isGatewayWaiting()) return VisualFace::CONFUSED;
  switch (_state) {
    case FaceState::WAKE:       return VisualFace::WINK;
    case FaceState::THINKING:   return VisualFace::SMUG;
    case FaceState::SPEAKING:   return VisualFace::HAPPY;
    case FaceState::HAPPY:      return VisualFace::HAPPY;
    case FaceState::CONFUSED:   return VisualFace::CONFUSED;
    case FaceState::CRYING:     return VisualFace::CRYING;
    case FaceState::SLEEPY:     return VisualFace::SLEEPY;
    case FaceState::LISTENING:  return VisualFace::NEUTRAL;
    case FaceState::DOCK_IDLE:
    case FaceState::HAND_IDLE:
    default:                    return VisualFace::SLEEPY;
  }
}

FaceAssets::Group FaceUI::groupForVisual(VisualFace v) const {
  return static_cast<FaceAssets::Group>(static_cast<uint8_t>(v));
}

void FaceUI::resetAnimation(uint32_t nowMs) {
  _frameInGroup = 0;
  _idleActionStep = 0;
  _idleActionKind = 0;
  _audioBand = 0;
  _lastAudioFrameAt = 0;
  if (_visual == VisualFace::NEUTRAL) _nextFrameAt = nowMs + (uint32_t)random(2600, 5200);
  else _nextFrameAt = nowMs + 180;
}

void FaceUI::showFrame(uint8_t frameInGroup) {
  const auto group = groupForVisual(_visual);
  const auto& gi = FaceAssets::GROUPS[(uint8_t)group];
  if (gi.count == 0) return;
  if (frameInGroup >= gi.count) frameInGroup = gi.count - 1;
  _frameInGroup = frameInGroup;
  showAbsoluteFrame(gi.start + frameInGroup);
}

void FaceUI::showAbsoluteFrame(uint8_t frameIndex) {
  if (frameIndex >= FaceAssets::FRAME_COUNT) return;
  if (frameIndex == _lastAbsoluteFrame) return;
  _lastAbsoluteFrame = frameIndex;
  const auto& asset = FaceAssets::FRAMES[frameIndex];

  sFaceCanvas.fillSprite(TFT_BLACK);
  uint32_t outPos = 0;
  for (uint16_t p = 0; p + 1 < asset.rleSize; p += 2) {
    const uint8_t count = pgm_read_byte(asset.rle + p);
    const uint8_t palIndex = pgm_read_byte(asset.rle + p + 1);
    if (palIndex >= asset.paletteSize) continue;
    const uint16_t color = pgm_read_word(asset.palette + palIndex);

    uint8_t remaining = count;
    while (remaining && outPos < (uint32_t)FaceAssets::FRAME_W * FaceAssets::FRAME_H) {
      const uint16_t x = outPos % FaceAssets::FRAME_W;
      const uint16_t y = outPos / FaceAssets::FRAME_W;
      const uint16_t rowRoom = FaceAssets::FRAME_W - x;
      const uint16_t span = remaining < rowRoom ? remaining : rowRoom;
      sFaceCanvas.drawFastHLine(x, y, span, color);
      outPos += span;
      remaining -= span;
    }
  }

  // One buffered transfer, never clear the LCD between animation frames.
  M5.Display.startWrite();
  sFaceCanvas.pushSprite(0, 0);
  M5.Display.endWrite();
  ++_framePushCount;
}

void FaceUI::serviceAudioAnimation(uint32_t nowMs) {
  if (_state != FaceState::SPEAKING && _state != FaceState::LISTENING) return;
  if (nowMs - _lastAudioFrameAt < 90) return;

  uint8_t band = 0;
  if (_audioLevel >= 0.08f) band = 1;
  if (_audioLevel >= 0.28f) band = 2;
  if (_audioLevel >= 0.58f) band = 3;
  if (band == _audioBand && nowMs - _lastAudioFrameAt < 240) return;

  _audioBand = band;
  _lastAudioFrameAt = nowMs;

  if (_state == FaceState::SPEAKING) {
    // Happy frames were authored with progressively changing mouth openness.
    static constexpr uint8_t kSpeechFrame[4] = {0, 1, 2, 3};
    showFrame(kSpeechFrame[band]);
  } else {
    // Listening stays on the approved Neutral face, with tiny eye-direction shifts.
    static constexpr uint8_t kListenFrame[4] = {0, 1, 2, 3};
    showFrame(kListenFrame[band]);
  }
}

void FaceUI::advanceAnimation(uint32_t nowMs) {
  if (_state == FaceState::SPEAKING || _state == FaceState::LISTENING) return;
  if ((int32_t)(nowMs - _nextFrameAt) < 0) return;

  if (_visual == VisualFace::NEUTRAL) {
    // Event-driven blink: rest for a few seconds, then play the approved
    // 4-frame Neutral animation once. This keeps the companion alive without
    // turning the LCD into a permanently looping GIF.
    if (_idleActionStep == 0) {
      _idleActionStep = 1;
      showFrame(0);
      _nextFrameAt = nowMs + (uint32_t)random(2400, 5200);
      return;
    }

    const uint8_t step = _idleActionStep - 1;
    showFrame(kNeutralAnim[step]);
    ++_idleActionStep;

    if (_idleActionStep > 4) {
      _idleActionStep = 0;
      showFrame(0);
      _nextFrameAt = nowMs + (uint32_t)random(2600, 5400);
    } else {
      _nextFrameAt = nowMs + 120;
    }
    return;
  }

  const auto& gi = FaceAssets::GROUPS[(uint8_t)groupForVisual(_visual)];
  uint16_t frameMs = 280;
  switch (_visual) {
    case VisualFace::WINK:      frameMs = 180; break;
    case VisualFace::HAPPY:     frameMs = 240; break;
    case VisualFace::SMUG:      frameMs = 420; break;
    case VisualFace::CONFUSED:  frameMs = 320; break;
    case VisualFace::CRYING:    frameMs = 360; break;
    case VisualFace::SLEEPY:    frameMs = 560; break;
    case VisualFace::OFFLINE:   frameMs = 850; break;
    default:                    frameMs = 280; break;
  }

  showFrame((_frameInGroup + 1) % gi.count);
  _nextFrameAt = nowMs + frameMs;
}

String FaceUI::currentTimeString() const {
  if (M5.Rtc.isEnabled()) {
    m5::rtc_datetime_t dt;
    if (M5.Rtc.getDateTime(&dt) && dt.date.year >= 2024 && dt.time.hours < 24 && dt.time.minutes < 60) {
      char b[8];
      snprintf(b, sizeof(b), "%02d:%02d", dt.time.hours, dt.time.minutes);
      return String(b);
    }
  }
  time_t epoch = time(nullptr);
  if (epoch > 1700000000) {
    epoch += UTC_OFFSET_SECONDS;
    struct tm localTm;
    gmtime_r(&epoch, &localTm);
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", localTm.tm_hour, localTm.tm_min);
    return String(b);
  }
  return "--:--";
}

void FaceUI::drawPixelDigit(M5Canvas& canvas, char ch, int x, int y, int scale, uint16_t color) {
  if (ch == ':') {
    canvas.fillRect(x, y + 2 * scale, scale, scale, color);
    canvas.fillRect(x, y + 5 * scale, scale, scale, color);
    return;
  }
  if (ch < '0' || ch > '9') return;
  const uint8_t* rows = kDigit5x7[ch - '0'];
  for (int row = 0; row < 7; ++row) {
    for (int col = 0; col < 5; ++col) {
      if (rows[row] & (1 << (4 - col))) {
        canvas.fillRect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

void FaceUI::renderClock() {
  sClockCanvas.fillSprite(TFT_BLACK);
  const uint16_t white = M5.Display.color565(220, 220, 216);
  const int scale = 4;
  const int digitW = 5 * scale;
  const int colonW = scale;
  const int gap = scale;
  const int totalW = digitW * 4 + colonW + gap * 4;
  int x = (135 - totalW) / 2;
  const int y = 19;
  for (size_t i = 0; i < _clockCache.length(); ++i) {
    const char ch = _clockCache[i];
    drawPixelDigit(sClockCanvas, ch, x, y, scale, white);
    x += ((ch == ':') ? colonW : digitW) + gap;
  }
  M5.Display.startWrite();
  sClockCanvas.pushSprite(0, 165);
  M5.Display.endWrite();
}

void FaceUI::update(uint32_t nowMs) {
  if (!_lastUiDiagAt || nowMs - _lastUiDiagAt >= 10000) {
    _lastUiDiagAt = nowMs;
    Serial.printf("[ui][diag] wifi=%d ws=%d visual=%u face_pushes_10s=%lu brightness=%u\n",
                  _status.wifi ? 1 : 0, _status.websocket ? 1 : 0, (unsigned)_visual,
                  (unsigned long)_framePushCount, (unsigned)_brightness);
    _framePushCount = 0;
  }
  if (nowMs - _lastClockPoll >= 1000) {
    _lastClockPoll = nowMs;
    const String nowClock = currentTimeString();
    if (nowClock != _clockCache) {
      _clockCache = nowClock;
      renderClock();
    }
  }
  serviceAudioAnimation(nowMs);
  advanceAnimation(nowMs);
}
