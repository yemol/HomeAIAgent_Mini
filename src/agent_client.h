#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "app_config.h"
#include "app_types.h"

class AgentClient {
public:
  using StateFn = void (*)(FaceState state, const String& text, uint32_t audioRate);
  using BinaryFn = void (*)(const uint8_t* data, size_t len);
  using FollowupArmFn = void (*)(const String& armId, uint32_t timeoutMs);

  void begin();
  void loop();
  void setStateCallback(StateFn fn) { _stateFn = fn; }
  void setBinaryCallback(BinaryFn fn) { _binaryFn = fn; }
  void setFollowupArmCallback(FollowupArmFn fn) { _followupArmFn = fn; }

  bool wifiConnected() const;
  // Authenticated AND Gateway-ready, not merely a TCP/WebSocket connection.
  bool connected() const { return _wsConnected && _a6AuthOk && _gatewayReady; }
  bool authenticated() const { return _wsConnected && _a6AuthOk; }
  bool sendBinary(const uint8_t* data, size_t len);
  void printDiagnostics(bool force = false);
  void sendPttStart(const char* trigger = "button_a", const char* followupArmId = nullptr);
  void sendPttStop(const char* trigger = "button_a");
  void sendPttAbort(const char* trigger, const char* reason);
  void sendHello();
  void sendDockState(bool docked, int battery);

private:
  WebSocketsClient _ws;
  bool _wsConnected = false;
  bool _wsInitialized = false;
  bool _a6AuthOk = false;
  bool _gatewayReady = false; // May precede auth.ok in Gateway observe mode.
  bool _a6ProofPending = false;
  bool _a6Locked = false; // Explicit auth failure: require operator intervention/reboot.
  uint32_t _helloSentAt = 0;
  uint32_t _lastWifiAttempt = 0;
  uint32_t _lastNtpAttempt = 0;
  uint32_t _ntpStartAt = 0;
  uint32_t _lastDiagAt = 0;
  uint32_t _lastGatewayProbeAt = 0;
  uint32_t _wifiAttemptCount = 0;
  wl_status_t _lastWifiStatus = WL_DISCONNECTED;
  bool _ntpConfigured = false;
  bool _systemClockCleared = false;
  bool _clockSynced = false;
  StateFn _stateFn = nullptr;
  BinaryFn _binaryFn = nullptr;
  FollowupArmFn _followupArmFn = nullptr;

  static AgentClient* _instance;
  static void onWsEventStatic(WStype_t type, uint8_t* payload, size_t length);
  void onWsEvent(WStype_t type, uint8_t* payload, size_t length);
  void ensureWifi();
  void probeGateway();
  const char* wifiStatusName(wl_status_t st) const;
  void syncClock();
  // sendJson is application-only; bootstrap handshake uses sendBootstrapJson.
  void sendJson(JsonDocument& doc);
  bool sendBootstrapJson(JsonDocument& doc);
  void resetConnectionAuth();
  void stopOnAuthFailure(const char* reason);
  FaceState mapState(const String& s) const;
};
