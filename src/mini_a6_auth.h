#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// A6 protocol adapter. Does not send traffic or own the WebSocket. The caller
// must accept application data only after server auth.ok and gateway.ready.
class MiniA6Auth {
public:
  static bool isConfigured();
  static bool createChallengeResponse(JsonDocument& challenge,
                                      const char* expectedDeviceId,
                                      String& outJson, const char*& error);
};
