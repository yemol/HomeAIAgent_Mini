#include <Arduino.h>
#include <M5Unified.h>
#include "app_config.h"
#include "app_types.h"
#include "ui_face.h"
#include "audio_io.h"
#include "agent_client.h"
#include "mini_a6_console.h"

static FaceUI ui;
static AudioIO audio;
static AgentClient agent;
static DeviceStatus status;
static bool lastDockRaw = false;
static bool pendingDock = false;
static uint32_t dockChangedAt = 0;
static uint32_t lastStatusPush = 0;
static uint32_t demoThinkingAt = 0;
static uint32_t ttsRate = TTS_PLAYBACK_SAMPLE_RATE;
static bool pendingAudioDrain = false;
static FaceState postAudioState = FaceState::HAND_IDLE;
static uint32_t localTransientUntil = 0;

// PTT end-to-end timing diagnostics. These do not change the audio path.
static uint32_t pttPressedAt = 0;
static uint32_t pttReleasedAt = 0;
static bool pttWaitingReply = false;
static bool pttFirstStateLogged = false;
static bool pttFirstAudioStartLogged = false;
static bool pttFirstPcmLogged = false;

enum class AutoVoiceMode : uint8_t {
  NONE = 0,
  FOLLOWUP,
};

static AutoVoiceMode autoVoiceMode = AutoVoiceMode::NONE;
static String followupArmId;

static bool sendMicBinary(const uint8_t* data, size_t len) {
  return agent.sendBinary(data, len);
}

static void setActiveBrightness() {
  ui.setBrightness(status.docked ? LCD_BRIGHT_DOCK_ACTIVE : LCD_BRIGHT_HAND);
}

static void setDockStandbyBrightness() {
  if (status.docked) ui.setBrightness(LCD_BRIGHT_DOCK_STANDBY);
}

static void clearAutoVoiceState(bool stopCapture = true, const char* reason = "local_cancel") {
  const AutoVoiceMode previousMode = autoVoiceMode;
  const bool hadActiveUtterance = audio.autoUtteranceActive();

  // Preserve in-progress Follow-up: abort its server stream on button or Dock change.
  if (hadActiveUtterance && agent.connected() && previousMode == AutoVoiceMode::FOLLOWUP) {
    agent.sendPttAbort("follow_up", reason);
  }

  if (stopCapture) audio.stopAutoCapture();
  autoVoiceMode = AutoVoiceMode::NONE;
  followupArmId = "";
}

static void serviceDockStandbyBrightness();

static void markAutoPttStarted(const char* trigger) {
  pttPressedAt = millis();
  pttReleasedAt = 0;
  pttWaitingReply = false;
  pttFirstStateLogged = false;
  pttFirstAudioStartLogged = false;
  pttFirstPcmLogged = false;
  Serial.printf("[voice] automatic PTT start trigger=%s t=%lu\n",
                trigger, (unsigned long)pttPressedAt);
}

static void markAutoPttStopped(const char* trigger) {
  pttReleasedAt = millis();
  pttWaitingReply = true;
  ui.setState(FaceState::THINKING, true);
  Serial.printf("[voice] automatic PTT stop trigger=%s t=%lu -> THINKING\n",
                trigger, (unsigned long)pttReleasedAt);
}

static void onAutoSpeechStart() {
  switch (autoVoiceMode) {
    case AutoVoiceMode::FOLLOWUP:
      setActiveBrightness();
      ui.setState(FaceState::LISTENING, true);
      markAutoPttStarted("follow_up");
      if (agent.connected()) agent.sendPttStart("follow_up", followupArmId.c_str());
      break;
    case AutoVoiceMode::NONE:
    default:
      break;
  }
}

static void onAutoSpeechStop() {
  const AutoVoiceMode ended = autoVoiceMode;
  if (ended == AutoVoiceMode::FOLLOWUP) {
    if (agent.connected()) agent.sendPttStop("follow_up");
    autoVoiceMode = AutoVoiceMode::NONE;
    followupArmId = "";
    markAutoPttStopped("follow_up");
  }
}

static void onAutoSpeechTimeout() {
  const AutoVoiceMode timedOut = autoVoiceMode;
  autoVoiceMode = AutoVoiceMode::NONE;
  followupArmId = "";

  if (!status.docked) return;

  if (timedOut == AutoVoiceMode::FOLLOWUP) {
    Serial.println("[followup] local listen window expired -> dock standby");
  }
  ui.setState(FaceState::DOCK_IDLE, true);
  setDockStandbyBrightness();
}

static void onFollowupArm(const String& armId, uint32_t timeoutMs) {
  if (!status.docked || !agent.connected() || armId.length() == 0 || timeoutMs == 0) return;
  if (audio.capturing() || audio.playing()) return;

  audio.stopAutoCapture();
  followupArmId = armId;
  autoVoiceMode = AutoVoiceMode::FOLLOWUP;
  pttWaitingReply = false;  // Gateway arms only after routed Dock playback completed.

  setActiveBrightness();
  ui.setState(FaceState::LISTENING, true);
  audio.startAutoCapture(
      timeoutMs,
      DOCK_COMMAND_MAX_MS,
      DOCK_COMMAND_END_SILENCE_MS);
  Serial.printf("[followup] hands-free listen armed id=%s timeout=%lums\n",
                armId.c_str(), (unsigned long)timeoutMs);
}

static void onAgentState(FaceState state, const String& text, uint32_t audioRate) {
  const bool autoListening = audio.autoCapturing()
      && (autoVoiceMode == AutoVoiceMode::FOLLOWUP);

  if (state == FaceState::HAND_IDLE && status.docked) state = FaceState::DOCK_IDLE;

  // Gateway sends idle immediately after followup.arm. Keep the local hands-free
  // listener visible/bright until the follow-up window itself expires.
  if ((state == FaceState::HAND_IDLE || state == FaceState::DOCK_IDLE)
      && autoVoiceMode == AutoVoiceMode::FOLLOWUP && audio.autoCapturing()) {
    return;
  }

  // A delayed remote "listening" event must not pull the face backwards after
  // the user has already released PTT. Automatic voice capture is an exception.
  if (state == FaceState::LISTENING && !audio.capturing() && !autoListening) {
    Serial.println("[ptt][guard] ignored stale remote LISTENING after release");
    return;
  }

  if (pttWaitingReply && !pttFirstStateLogged && pttReleasedAt) {
    pttFirstStateLogged = true;
    Serial.printf("[ptt][latency] first_state=%u after_release=%lums\n",
                  (unsigned)state,
                  (unsigned long)(millis() - pttReleasedAt));
  }

  if (state == FaceState::SPEAKING) {
    // Any routed reply/notification owns the acoustic foreground. Stop an
    // active Follow-up capture before entering speaker playback.
    if (audio.autoCapturing() || autoVoiceMode != AutoVoiceMode::NONE) {
      clearAutoVoiceState();
    }
    setActiveBrightness();
    pendingAudioDrain = false;
    ui.setState(FaceState::SPEAKING, true);

    if (audioRate) {
      ttsRate = audioRate;
      if (pttWaitingReply && !pttFirstAudioStartLogged && pttReleasedAt) {
        pttFirstAudioStartLogged = true;
        Serial.printf("[ptt][latency] audio.start after_release=%lums rate=%lu\n",
                      (unsigned long)(millis() - pttReleasedAt),
                      (unsigned long)audioRate);
      }
      if (!audio.playing()) audio.startPlayback(audioRate);
    }
    return;
  }

  if (audio.playing()
      && (state == FaceState::HAND_IDLE
          || state == FaceState::DOCK_IDLE
          || state == FaceState::SLEEPY)) {
    pendingAudioDrain = true;
    postAudioState = status.docked ? FaceState::DOCK_IDLE : FaceState::HAND_IDLE;
    audio.requestPlaybackFinish();
    return;
  }

  if (audio.playing()) audio.stopPlayback();

  if (state == FaceState::HAND_IDLE || state == FaceState::DOCK_IDLE) {
    if (pttWaitingReply && pttReleasedAt) {
      Serial.printf("[ptt][latency] reply_complete after_release=%lums -> IDLE\n",
                    (unsigned long)(millis() - pttReleasedAt));
    }
    pttWaitingReply = false;
  }
  ui.setState(state, true);
}

static void onAgentBinary(const uint8_t* data, size_t len) {
  pendingAudioDrain = false;
  if (audio.autoCapturing() || autoVoiceMode != AutoVoiceMode::NONE) {
    clearAutoVoiceState();
  }
  setActiveBrightness();

  if (pttWaitingReply && !pttFirstPcmLogged && pttReleasedAt) {
    pttFirstPcmLogged = true;
    Serial.printf("[ptt][latency] first_pcm after_release=%lums bytes=%u source_rate=%lu\n",
                  (unsigned long)(millis() - pttReleasedAt),
                  (unsigned)len,
                  (unsigned long)ttsRate);
  }

  if (ui.state() != FaceState::SPEAKING) ui.setState(FaceState::SPEAKING, true);
  if (!audio.playing()) {
    Serial.printf("[audio][WARN] PCM arrived without active audio.start; fallback rate=%lu\n",
                  (unsigned long)ttsRate);
    audio.startPlayback(ttsRate);
  }
  audio.pushPlayback(data, len);
}

static bool detectDockRaw() {
  const int16_t vbus = M5.Power.getVBUSVoltage();
  const auto charging = M5.Power.isCharging();
  return (vbus >= VBUS_DOCK_THRESHOLD_MV) || (charging == m5::Power_Class::is_charging);
}

static void serviceDock(uint32_t now) {
  const bool raw = detectDockRaw();
  if (raw != lastDockRaw) {
    lastDockRaw = raw;
    pendingDock = raw;
    dockChangedAt = now;
  }

  if (pendingDock != status.docked && now - dockChangedAt >= DOCK_DEBOUNCE_MS) {
    status.docked = pendingDock;
    clearAutoVoiceState(true, "dock_transition");

    if (status.docked) {
      const FaceState currentState = ui.state();
      const bool idleEnough = (currentState == FaceState::HAND_IDLE
                            || currentState == FaceState::DOCK_IDLE
                            || currentState == FaceState::SLEEPY);
      if (!audio.capturing() && !audio.playing() && !audio.autoCapturing()
          && !pttWaitingReply && idleEnough) {
        ui.setState(FaceState::DOCK_IDLE, true);
        setDockStandbyBrightness();
        Serial.println("[dock] external power present -> standby brightness 20%");
      } else {
        setActiveBrightness();
        Serial.println("[dock] external power present while busy -> active brightness until idle");
      }
    } else {
      setActiveBrightness();
      ui.setState(FaceState::WAKE, true);
      Serial.println("[dock] external power removed -> portable brightness restored");
    }
    M5.Display.wakeup();
    ui.forceRedraw();
    agent.sendDockState(status.docked, status.battery);
  }
}

// Dock standby is display-only. No wake listening, polling, or mic upload.
static void serviceDockStandbyBrightness() {
  if (status.docked && ui.state() == FaceState::DOCK_IDLE && !audio.capturing()
      && !audio.playing() && !audio.autoCapturing() && !pendingAudioDrain
      && !pttWaitingReply && !ui.demoExpressionActive()) {
    setDockStandbyBrightness();
  }
}

static void startPtt() {
  M5.Display.wakeup();
  clearAutoVoiceState(true, "button_override");
  setActiveBrightness();
  localTransientUntil = 0;
  ui.endDemoExpression();
  pendingAudioDrain = false;

  pttPressedAt = millis();
  pttReleasedAt = 0;
  pttWaitingReply = false;
  pttFirstStateLogged = false;
  pttFirstAudioStartLogged = false;
  pttFirstPcmLogged = false;

  if (audio.playing()) audio.stopPlayback();

  ui.setState(FaceState::LISTENING, true);
  if (agent.connected()) agent.sendPttStart("button_a");
  const bool captureOk = audio.startCapture();

  Serial.printf("[ptt] PRESS t=%lu ws=%u capture=%u mic_rate=%lu\n",
                (unsigned long)pttPressedAt,
                (unsigned)agent.connected(),
                (unsigned)captureOk,
                (unsigned long)MIC_SAMPLE_RATE);
}

static void stopPtt() {
  const uint32_t releaseEdgeAt = millis();
  audio.stopCapture();
  pttReleasedAt = millis();

  Serial.printf("[ptt] RELEASE hold=%lums capture_flush=%lums\n",
                (unsigned long)(releaseEdgeAt - pttPressedAt),
                (unsigned long)(pttReleasedAt - releaseEdgeAt));

  if (agent.connected()) {
    agent.sendPttStop("button_a");
    pttWaitingReply = true;
    ui.setState(FaceState::THINKING, true);
    Serial.printf("[ptt] STOP sent t=%lu -> THINKING\n",
                  (unsigned long)pttReleasedAt);
  } else if (ENABLE_DEMO_FALLBACK) {
    ui.setState(FaceState::THINKING, true);
    demoThinkingAt = millis();
  } else {
    ui.setState(FaceState::ERROR_STATE, true);
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false; // StickC-Plus SE intentionally removes the IMU.
  cfg.output_power = true;  // Hat SPK2 requires the external 5 V rail.
  cfg.external_speaker.hat_spk2 = true;
  M5.begin(cfg);
  M5.Power.setExtOutput(true);

  Serial.println();
  Serial.println("HomeAIAgent Mini A1.36.4 A6 NVS boot");
  Serial.printf("board=%d fw=%s\n", (int)M5.getBoard(), HOMEAI_FW_VERSION);
  Serial.printf("[power] ext5v=%u vbus=%dmV bat=%dmV\n",
                (unsigned)M5.Power.getExtOutput(),
                (int)M5.Power.getVBUSVoltage(),
                (int)M5.Power.getBatteryVoltage());

  M5.Display.wakeup();

  ui.begin();
  audio.begin();
  audio.setSendCallback(sendMicBinary);
  audio.setAutoCaptureCallbacks(onAutoSpeechStart, onAutoSpeechStop, onAutoSpeechTimeout);
  agent.setStateCallback(onAgentState);
  agent.setBinaryCallback(onAgentBinary);
  agent.setFollowupArmCallback(onFollowupArm);
  MiniA6Console::show(false);
  agent.begin();

  status.docked = detectDockRaw();
  pendingDock = status.docked;
  lastDockRaw = status.docked;
  status.battery = M5.Power.getBatteryLevel();
  status.wifi = agent.wifiConnected();
  status.websocket = agent.connected();
  ui.setStatus(status);
  ui.setState(status.docked ? FaceState::DOCK_IDLE : FaceState::HAND_IDLE, true);
  if (status.docked) setDockStandbyBrightness();
  else setActiveBrightness();
}

void loop() {
  M5.update();
  // USB-only serial provisioning. Successful SET/CLEAR immediately restarts
  // so no stale authenticated WebSocket survives a credential change.
  if (MiniA6Console::poll(agent.authenticated())) {
    Serial.flush();
    delay(40);
    ESP.restart();
    return;
  }
  const uint32_t now = millis();

  agent.loop();
  serviceDock(now);

  status.wifi = agent.wifiConnected();
  status.websocket = agent.connected();
  status.battery = M5.Power.getBatteryLevel();
  ui.setStatus(status);

  // A: hold-to-talk PTT remains available in both portable and docked modes.
  if (M5.BtnA.wasPressed()) startPtt();
  if (audio.capturing()) {
    audio.serviceCapture();
    ui.setAudioLevel(audio.inputLevel());
  }
  if (M5.BtnA.wasReleased() && audio.capturing()) stopPtt();

  if (audio.autoCapturing()) {
    audio.serviceAutoCapture();
    ui.setAudioLevel(audio.inputLevel());
  }

  audio.servicePlayback();
  if (pendingAudioDrain && !audio.playing()) {
    pendingAudioDrain = false;
    ui.setState(postAudioState, true);

    if (pttWaitingReply && pttReleasedAt) {
      Serial.printf("[ptt][latency] reply_complete after_release=%lums -> IDLE\n",
                    (unsigned long)(millis() - pttReleasedAt));
    }
    pttWaitingReply = false;
  }

  // Hold B: local SPK2 hardware tone test, independent of Gateway/TTS.
  // Short B click: preview the next expression for 10 seconds.
  if (M5.BtnB.wasHold() && !audio.capturing() && !audio.playing() && !audio.autoCapturing()) {
    localTransientUntil = 0;
    ui.endDemoExpression();
    audio.playDiagnosticTone();
  } else if (M5.BtnB.wasClicked()) {
    clearAutoVoiceState();
    setActiveBrightness();
    ui.cycleDemoExpression();
    localTransientUntil = now + 10000;
  }

  if (ui.state() == FaceState::SPEAKING) ui.setAudioLevel(audio.outputLevel());

  if (demoThinkingAt && now - demoThinkingAt > 1800) {
    demoThinkingAt = 0;
    ui.setState(FaceState::CONFUSED, true);
  }

  if (localTransientUntil && now >= localTransientUntil) {
    localTransientUntil = 0;
    ui.endDemoExpression();
  }
  if (ui.state() == FaceState::WAKE && now - dockChangedAt > 1500 && !audio.capturing()) {
    ui.setState(FaceState::HAND_IDLE, true);
  }

  if (now - lastStatusPush > 30000) {
    lastStatusPush = now;
    if (agent.connected()) agent.sendDockState(status.docked, status.battery);
  }

  // Preserve 20% standby brightness without opening the microphone.
  serviceDockStandbyBrightness();

  ui.update(now);
  delay(2);
}
