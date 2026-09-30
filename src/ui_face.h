#pragma once
#include <M5Unified.h>
#include "app_types.h"
#include "app_config.h"
#include "face_frames.h"

class FaceUI {
public:
  void begin();
  void setState(FaceState state, bool force = false);
  FaceState state() const { return _state; }
  void setStatus(const DeviceStatus& status);
  void setMessage(const String& text, uint32_t holdMs = 7000);
  void setAudioLevel(float level01);
  void setBrightness(uint8_t level);
  uint8_t brightness() const { return _brightness; }
  void update(uint32_t nowMs);
  void forceRedraw();
  void cycleDemoExpression();
  void endDemoExpression();
  bool demoExpressionActive() const { return _demoOverride; }

private:
  enum class VisualFace : uint8_t {
    NEUTRAL = 0,
    WINK,
    HAPPY,
    SMUG,
    CONFUSED,
    CRYING,
    SLEEPY,
    OFFLINE,
  };

  FaceState _state = FaceState::DOCK_IDLE;
  DeviceStatus _status;
  VisualFace _visual = VisualFace::NEUTRAL;
  bool _demoOverride = false;
  uint8_t _demoIndex = 7;

  float _audioLevel = 0.0f;
  uint8_t _audioBand = 0;
  uint8_t _frameInGroup = 0;
  uint8_t _idleActionStep = 0;
  uint8_t _idleActionKind = 0;
  uint32_t _nextFrameAt = 0;
  uint32_t _lastAudioFrameAt = 0;
  uint32_t _lastClockPoll = 0;
  uint32_t _lastUiDiagAt = 0;
  uint32_t _framePushCount = 0;
  uint8_t _lastAbsoluteFrame = 255;
  String _clockCache = "--:--";
  uint8_t _brightness = LCD_BRIGHT_DOCK_ACTIVE;

  bool isOfflineFace() const;
  bool isGatewayWaiting() const;
  VisualFace visualForState() const;
  FaceAssets::Group groupForVisual(VisualFace v) const;
  void resetAnimation(uint32_t nowMs);
  void showFrame(uint8_t frameInGroup);
  void showAbsoluteFrame(uint8_t frameIndex);
  void showAbsoluteFrame(uint8_t frameIndex, int8_t offsetX, int8_t offsetY);
  void advanceAnimation(uint32_t nowMs);
  void serviceAudioAnimation(uint32_t nowMs);

  String currentTimeString() const;
  void renderClock();
  void drawPixelDigit(M5Canvas& canvas, char ch, int x, int y, int scale, uint16_t color);
};
