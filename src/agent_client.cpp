#include "agent_client.h"
#include "mini_a6_auth.h"
#include <sys/time.h>
#include <WiFi.h>
#include <time.h>
#include <M5Unified.h>
#include <WiFiClient.h>

AgentClient* AgentClient::_instance = nullptr;

void AgentClient::begin() {
  _instance = this;
  WiFi.mode(WIFI_STA);
  // Real-time PTT/WebSocket traffic is more stable without modem sleep.
  WiFi.setSleep(false);

  Serial.println("[net] ===== HomeAIAgent Mini network diagnostics =====");
  Serial.printf("[net] ssid=\"%s\" password_len=%u\n", WIFI_SSID, (unsigned)strlen(WIFI_PASSWORD));
  Serial.printf("[net] gateway=ws://%s:%u%s token=%s\n",
                GATEWAY_HOST, (unsigned)GATEWAY_PORT, GATEWAY_PATH,
                strlen(GATEWAY_TOKEN) ? "SET" : "EMPTY");
  if (!strcmp(WIFI_SSID, "YOUR_WIFI_SSID") || !strlen(WIFI_SSID)) {
    Serial.println("[net][ERROR] WIFI_SSID is still the example/empty value. Check include/secrets.h");
  }
  if (!strcmp(GATEWAY_HOST, "192.168.1.20")) {
    Serial.println("[net][WARN] GATEWAY_HOST is still the example value 192.168.1.20 unless that is really your Gateway.");
  }

  if (!MiniA6Auth::isConfigured()) {
    // No fallback to observe-mode legacy traffic: with missing credentials,
    // Mini remains locally usable but never sends unauthenticated app data.
    Serial.println("[a6][LOCKED] Mini key missing/invalid. Provision over USB serial: AUTH SET <secret>.");
    _a6Locked = true;
  }
  _lastWifiStatus = WiFi.status();
  ensureWifi();

  Serial.println("[ws] configuring websocket client...");
  if (!_a6Locked) {
    _ws.begin(GATEWAY_HOST, GATEWAY_PORT, GATEWAY_PATH);
    _ws.onEvent(onWsEventStatic);
    _ws.setReconnectInterval(WS_RECONNECT_MS);
    _ws.enableHeartbeat(15000, 3000, 2);
    _wsInitialized = true;
  }
}

void AgentClient::loop() {
  ensureWifi();

  const wl_status_t st = WiFi.status();
  if (st != _lastWifiStatus) {
    Serial.printf("[wifi] status %s(%d) -> %s(%d)\n",
                  wifiStatusName(_lastWifiStatus), (int)_lastWifiStatus,
                  wifiStatusName(st), (int)st);
    _lastWifiStatus = st;
    if (st == WL_CONNECTED) {
      Serial.printf("[wifi] CONNECTED ssid=\"%s\" ip=%s gw=%s dns=%s rssi=%d dBm channel=%d\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                    WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP(0).toString().c_str(),
                    WiFi.RSSI(), WiFi.channel());
    }
  }

  if (!wifiConnected() && _wsConnected) {
    // Drop A6 lease at the first observed Wi-Fi loss, not on a later WS callback.
    _ws.disconnect();
    _wsConnected = false;
    resetConnectionAuth();
  }
  if (wifiConnected()) {
    if (_wsInitialized && !_a6Locked) {
      _ws.loop();
      if (_wsConnected && !connected() &&
          (uint32_t)(millis() - _helloSentAt) > 20000U) {
        Serial.println("[a6][TIMEOUT] Handshake did not complete; resetting this WebSocket.");
        _ws.disconnect(); // New socket will require a new server challenge.
        _wsConnected = false;
        resetConnectionAuth();
      }
    }
    syncClock();
    probeGateway();
  }
  printDiagnostics(false);
}

bool AgentClient::wifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void AgentClient::ensureWifi() {
  if (wifiConnected()) return;
  const uint32_t now = millis();
  if (_wifiAttemptCount && now - _lastWifiAttempt < WIFI_RETRY_MS) return;
  _lastWifiAttempt = now;
  ++_wifiAttemptCount;
  Serial.printf("[wifi] connect attempt #%lu ssid=\"%s\" current=%s(%d)\n",
                (unsigned long)_wifiAttemptCount, WIFI_SSID,
                wifiStatusName(WiFi.status()), (int)WiFi.status());
  WiFi.disconnect(false, false);
  delay(20);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

const char* AgentClient::wifiStatusName(wl_status_t st) const {
  switch (st) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID";
    case WL_SCAN_COMPLETED:  return "SCAN_DONE";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

void AgentClient::probeGateway() {
  if (!wifiConnected() || _wsConnected) return;
  const uint32_t now = millis();
  if (_lastGatewayProbeAt && now - _lastGatewayProbeAt < GATEWAY_PROBE_INTERVAL_MS) return;
  _lastGatewayProbeAt = now;

  WiFiClient probe;
  const uint32_t started = millis();
  const bool ok = probe.connect(GATEWAY_HOST, GATEWAY_PORT, 1800);
  const uint32_t took = millis() - started;
  Serial.printf("[net][tcp] gateway %s:%u -> %s (%lu ms) path=%s\n",
                GATEWAY_HOST, (unsigned)GATEWAY_PORT, ok ? "REACHABLE" : "FAILED",
                (unsigned long)took, GATEWAY_PATH);
  if (!ok) {
    Serial.println("[net][tcp] If Wi-Fi/NTP work but this fails, check Gateway IP, port, LAN/firewall, and that Gateway is listening on this interface.");
  }
  probe.stop();
}

void AgentClient::printDiagnostics(bool force) {
  const uint32_t now = millis();
  if (!force && _lastDiagAt && now - _lastDiagAt < NET_DIAG_INTERVAL_MS) return;
  _lastDiagAt = now;
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    Serial.printf("[net][diag] wifi=OK ip=%s rssi=%d ws=%s clock=%s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  _wsConnected ? "CONNECTED" : "DISCONNECTED",
                  _clockSynced ? "SYNCED" : "WAITING");
    Serial.printf("[a6][diag] state=%s raw_ws=%s ready=%u proof_pending=%u\n",
                  connected() ? "VERIFIED" : (_a6Locked ? "LOCKED" : "WAITING"),
                  _wsConnected ? "UP" : "DOWN",
                  (unsigned)_gatewayReady, (unsigned)_a6ProofPending);
  } else {
    Serial.printf("[net][diag] wifi=%s(%d) ws=DOWN attempts=%lu elapsed=%lus\n",
                  wifiStatusName(st), (int)st, (unsigned long)_wifiAttemptCount,
                  (unsigned long)(now / 1000));
    if (_wifiAttemptCount >= 3) {
      Serial.println("[net][hint] ESP32 uses 2.4GHz Wi-Fi. Verify SSID/password and that the AP exposes 2.4GHz WPA2/WPA-compatible service.");
    }
  }
}

void AgentClient::syncClock() {
  if (_clockSynced || !wifiConnected()) return;

  const uint32_t ms = millis();

  if (!_ntpConfigured || (ms - _lastNtpAttempt >= NTP_RETRY_MS)) {
    _lastNtpAttempt = ms;
    _ntpStartAt = ms;
    _ntpConfigured = true;

    // Important: ESP32/M5 can boot with a previously valid-looking system epoch
    // copied from RTC. configTime() starts SNTP asynchronously. If we read
    // time(nullptr) immediately after configTime(), that stale epoch can be
    // mistaken for a fresh NTP result and then UTC+8 gets applied to the wrong
    // value. Clear the libc/system clock ONCE before starting SNTP so a
    // "valid" epoch can only appear after a real network synchronization.
    if (!_systemClockCleared) {
      timeval tv = {};
      tv.tv_sec = 0;
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
      _systemClockCleared = true;
      Serial.println("[time] cleared stale system clock before SNTP");
    }

    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    Serial.printf("[time] requesting SNTP UTC: %s / %s / %s\n",
                  NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    return;  // Never accept the pre-configTime epoch in the same loop turn.
  }

  // Give SNTP time to receive at least one reply. The real acceptance gate is
  // still the epoch check below, because the clock was reset to zero.
  if (ms - _ntpStartAt < 1200) return;

  const time_t utcEpoch = time(nullptr);
  if (utcEpoch <= 1700000000) return;

  struct tm utcTm;
  gmtime_r(&utcEpoch, &utcTm);

  const time_t localEpoch = utcEpoch + UTC_OFFSET_SECONDS;
  struct tm localTm;
  gmtime_r(&localEpoch, &localTm);

  if (M5.Rtc.isEnabled()) {
    M5.Rtc.setDateTime(m5::rtc_datetime_t(localTm));
  }

  _clockSynced = true;
  Serial.printf(
      "[time] SNTP sync OK | UTC=%04d-%02d-%02d %02d:%02d:%02d"
      " | UTC+8=%04d-%02d-%02d %02d:%02d:%02d\n",
      utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
      utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec,
      localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday,
      localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
}

void AgentClient::onWsEventStatic(WStype_t type, uint8_t* payload, size_t length) {
  if (_instance) _instance->onWsEvent(type, payload, length);
}

void AgentClient::onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      resetConnectionAuth(); // A6 must never survive reconnect or old socket loss.
      _wsConnected = true;
      Serial.printf("[ws] CONNECTED gateway=%s:%u%s\n", GATEWAY_HOST, (unsigned)GATEWAY_PORT, GATEWAY_PATH);
      sendHello();
      break;
    case WStype_DISCONNECTED:
      if (_wsConnected) Serial.println("[ws] DISCONNECTED");
      _wsConnected = false;
      resetConnectionAuth();
      break;
    case WStype_ERROR:
      Serial.printf("[ws][ERROR] len=%u payload=%.*s\n", (unsigned)length, (int)length, payload ? (char*)payload : "");
      _wsConnected = false;
      resetConnectionAuth();
      break;
    case WStype_BIN:
      if (connected() && _binaryFn) _binaryFn(payload, length);
      break;
    case WStype_TEXT: {
      if (!_wsConnected || _a6Locked) break;
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err) break;
      const char* typeRaw = doc["type"] | "";
      if (!typeRaw[0]) typeRaw = doc["event"] | "";
      const char* stateRaw = doc["state"] | "";
      const char* textRaw = doc["text"] | "";
      if (!textRaw[0]) textRaw = doc["message"] | "";
      uint32_t rate = doc["sample_rate"] | 0U;
      if (!rate) rate = doc["audio_rate"] | 0U;
      String typeName(typeRaw);
      String stateText(stateRaw);
      String text(textRaw);

      // Authentication control messages are handled before any application
      // callback. In Gateway observe mode gateway.ready can arrive before the
      // challenge response is verified: neither event alone opens the gate.
      if (typeName == "security.challenge") {
        if (_a6AuthOk || _a6ProofPending) {
          stopOnAuthFailure("unexpected_or_duplicate_challenge");
          break;
        }
        String response;
        const char* why = "";
        if (!MiniA6Auth::createChallengeResponse(doc, HOMEAI_DEVICE_ID, response, why)) {
          stopOnAuthFailure(why);
          break;
        }
        if (!_ws.sendTXT(response)) {
          Serial.println("[a6] Could not send challenge response; restarting socket.");
          _ws.disconnect();
          _wsConnected = false;
          resetConnectionAuth();
          break;
        }
        _a6ProofPending = true;
        Serial.println("[a6] challenge response sent (proof not logged)");
        break;
      }
      if (typeName == "security.auth.ok") {
        const char* did = doc["device_id"] | "";
        const char* protocol = doc["protocol"] | "";
        if (!_a6ProofPending || strcmp(did, HOMEAI_DEVICE_ID) != 0 ||
            strcmp(protocol, "homeai-auth/1") != 0) {
          stopOnAuthFailure("unexpected_auth_ok");
          break;
        }
        _a6ProofPending = false;
        _a6AuthOk = true;
        Serial.printf("[a6] VERIFIED device=%s gateway_ready=%s\n",
                      HOMEAI_DEVICE_ID, _gatewayReady ? "yes" : "pending");
        break;
      }
      if (typeName == "security.auth.failed" || typeName == "security.denied") {
        // No reconnection storm with bad credentials or a revoked device.
        stopOnAuthFailure("gateway_rejected_credentials");
        break;
      }
      if (typeName == "gateway.ready") {
        const char* did = doc["device_id"] | "";
        const char* role = doc["device_role"] | "";
        if (strcmp(did, HOMEAI_DEVICE_ID) != 0 || strcmp(role, "companion") != 0) {
          stopOnAuthFailure("gateway_identity_mismatch");
          break;
        }
        _gatewayReady = true;
        Serial.printf("[a6] gateway.ready received; verified=%s\n", _a6AuthOk ? "yes" : "pending");
        break;
      }
      if (!connected()) {
        // Fail-closed even while the Gateway is operating in 'observe' mode.
        // Inbound states, PCM and followup are ignored until auth+ready.
        break;
      }

      if (typeName == "assistant.state") {
        Serial.printf("[agent][state] %s text_len=%u\n",
                      stateText.c_str(), (unsigned)text.length());
        // State changes may arrive before TTS metadata. Keep rate=0 here so the
        // UI can change without prematurely starting the speaker at a guessed rate.
        if (_stateFn) _stateFn(mapState(stateText), text, 0);
      } else if (typeName == "assistant.text") {
        // Text is metadata/subtitle only. Do not use it as an implicit audio.start.
        Serial.printf("[agent][text] len=%u\n", (unsigned)text.length());
      } else if (typeName == "audio.start") {
        if (!rate) rate = TTS_DEFAULT_SAMPLE_RATE;
        Serial.printf("[audio][ws] audio.start announced_rate=%lu text_len=%u\n",
                      (unsigned long)rate, (unsigned)text.length());
        if (_stateFn) _stateFn(FaceState::SPEAKING, text, rate);
      } else if (typeName == "audio.end") {
        Serial.println("[audio][ws] audio.end");
        if (_stateFn) _stateFn(FaceState::HAND_IDLE, text, rate);
      } else if (typeName == "followup.arm") {
        const char* armRaw = doc["arm_id"] | "";
        uint32_t timeoutMs = doc["timeout_ms"] | 0U;
        Serial.printf("[followup] arm id=%s timeout=%lums\n", armRaw, (unsigned long)timeoutMs);
        if (_followupArmFn) _followupArmFn(String(armRaw), timeoutMs);
      } else if (typeName == "assistant.error") {
        if (_stateFn) _stateFn(FaceState::ERROR_STATE, text, rate);
      }
      break;
    }
    default:
      break;
  }
}

FaceState AgentClient::mapState(const String& s) const {
  if (s == "listening") return FaceState::LISTENING;
  if (s == "thinking" || s == "processing") return FaceState::THINKING;
  if (s == "speaking" || s == "talking") return FaceState::SPEAKING;
  if (s == "happy" || s == "success" || s == "done") return FaceState::HAPPY;
  if (s == "wink") return FaceState::WAKE;
  if (s == "smug") return FaceState::THINKING;
  if (s == "confused" || s == "retry") return FaceState::CONFUSED;
  if (s == "crying" || s == "sad" || s == "tearful") return FaceState::CRYING;
  if (s == "error" || s == "offline") return FaceState::ERROR_STATE;
  if (s == "sleep" || s == "sleepy") return FaceState::SLEEPY;
  return FaceState::HAND_IDLE;
}

void AgentClient::resetConnectionAuth() {
  _a6AuthOk = false;
  _gatewayReady = false;
  _a6ProofPending = false;
  _helloSentAt = 0;
}

void AgentClient::stopOnAuthFailure(const char* reason) {
  Serial.printf("[a6][LOCKED] %s; automatic retries disabled until reboot.\n",
                reason ? reason : "authentication_failed");
  _a6Locked = true;
  _ws.disconnect();
  _wsConnected = false;
  resetConnectionAuth();
}

bool AgentClient::sendBootstrapJson(JsonDocument& doc) {
  if (!_wsConnected || _a6Locked) return false;
  String out;
  serializeJson(doc, out);
  return _ws.sendTXT(out);
}

void AgentClient::sendJson(JsonDocument& doc) {
  if (!connected()) return;
  String out;
  serializeJson(doc, out);
  _ws.sendTXT(out);
}

void AgentClient::sendHello() {
  JsonDocument doc;
  doc["type"] = "device.hello";
  doc["device_id"] = HOMEAI_DEVICE_ID;
  doc["device_name"] = HOMEAI_DEVICE_NAME;
  doc["device_kind"] = "m5stickc-plus-se-spk2";
  doc["device_role"] = "companion";
  doc["fw"] = HOMEAI_FW_VERSION;
  doc["token"] = GATEWAY_TOKEN;
  JsonObject cap = doc["capabilities"].to<JsonObject>();
  cap["ptt"] = true;
  cap["display"] = "135x240";
  cap["mic_pcm16"] = MIC_SAMPLE_RATE;
  cap["speaker_pcm16"] = true;
  cap["speaker_rate"] = TTS_DEFAULT_SAMPLE_RATE;
  cap["speaker_rate_min"] = 8000;
  cap["speaker_rate_max"] = 48000;
  cap["speaker_channels"] = 1;
  cap["dock"] = true;
  cap["expressions"] = true;
  cap["dock_voice_wake"] = false; // No automatic wake in the frozen stable build.
  cap["a6_auth"] = true;
  _helloSentAt = millis();
  if (!sendBootstrapJson(doc)) {
    _ws.disconnect();
    _wsConnected = false;
    resetConnectionAuth();
    return;
  }
  Serial.printf("[ws] device.hello sent device_id=%s token=%s\n", HOMEAI_DEVICE_ID, strlen(GATEWAY_TOKEN) ? "SET" : "EMPTY");
}

void AgentClient::sendPttStart(const char* trigger, const char* followupArmId) {
  JsonDocument doc;
  doc["type"] = "ptt.start";
  doc["device_id"] = HOMEAI_DEVICE_ID;
  doc["codec"] = "pcm_s16le";
  doc["sample_rate"] = MIC_SAMPLE_RATE;
  doc["channels"] = 1;
  doc["trigger"] = (trigger && trigger[0]) ? trigger : "button_a";
  if (followupArmId && followupArmId[0]) doc["followup_arm_id"] = followupArmId;
  sendJson(doc);
}

void AgentClient::sendPttStop(const char* trigger) {
  JsonDocument doc;
  doc["type"] = "ptt.stop";
  doc["device_id"] = HOMEAI_DEVICE_ID;
  doc["trigger"] = (trigger && trigger[0]) ? trigger : "button_a";
  sendJson(doc);
}

void AgentClient::sendPttAbort(const char* trigger, const char* reason) {
  JsonDocument doc;
  doc["type"] = "ptt.abort";
  doc["device_id"] = HOMEAI_DEVICE_ID;
  doc["trigger"] = (trigger && trigger[0]) ? trigger : "unknown";
  doc["reason"] = (reason && reason[0]) ? reason : "local_cancel";
  sendJson(doc);
}

void AgentClient::sendDockState(bool docked, int battery) {
  JsonDocument doc;
  doc["type"] = "device.state";
  doc["device_id"] = HOMEAI_DEVICE_ID;
  doc["docked"] = docked;
  doc["battery"] = battery;
  sendJson(doc);
}

bool AgentClient::sendBinary(const uint8_t* data, size_t len) {
  if (!connected() || !data || !len) return false;
  return _ws.sendBIN(data, len);
}
