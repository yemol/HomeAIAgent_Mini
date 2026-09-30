#pragma once
#include <Arduino.h>

enum class FaceState : uint8_t {
  DOCK_IDLE,
  HAND_IDLE,
  WAKE,
  LISTENING,
  THINKING,
  SPEAKING,
  HAPPY,
  CONFUSED,
  CRYING,
  ERROR_STATE,
  SLEEPY,
};

struct DeviceStatus {
  bool docked = false;
  bool wifi = false;
  bool websocket = false;
  int battery = -1;
};
