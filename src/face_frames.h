#pragma once
#include <Arduino.h>

namespace FaceAssets {
static constexpr uint16_t FRAME_W = 135;
static constexpr uint16_t FRAME_H = 165;
static constexpr uint8_t FRAME_COUNT = 32;

enum class Group : uint8_t {
  NEUTRAL = 0,
  WINK,
  HAPPY,
  SMUG,
  CONFUSED,
  CRYING,
  SLEEPY,
  OFFLINE,
};

struct FrameAsset {
  uint8_t paletteSize;
  const uint16_t* palette;
  const uint8_t* rle;
  uint16_t rleSize;
};

struct GroupInfo {
  uint8_t start;
  uint8_t count;
};

extern const FrameAsset FRAMES[FRAME_COUNT];
extern const GroupInfo GROUPS[8];
}
