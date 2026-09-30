#!/usr/bin/env python3
"""Frozen Mini A1.36.4 Clean source/package guards (static only, no target build)."""
from __future__ import annotations
from pathlib import Path
import re, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT/'src/main.cpp').read_text()
CLIENT = (ROOT/'src/agent_client.cpp').read_text()
HEADER = (ROOT/'src/agent_client.h').read_text()
CONFIG = (ROOT/'include/app_config.h').read_text()
AUDIO = (ROOT/'src/audio_io.cpp').read_text()
IGNORE = (ROOT/'.gitignore').read_text()

def check(label, condition):
    if not condition:
        raise AssertionError(label)
    print('PASS:', label)

check('wake probe removed from Mini code', not re.search(
    r'wake\.probe|wake\.accepted|wake\.rejected|WakeProbe|WAKE_PROBE|wakeProbe|onWakeResult|sendWakeProbe|"wake_word"',
    '\n'.join([MAIN,CLIENT,HEADER])))
check('no custom wake phrase/model in Mini source', 'MINI_WAKE_PHRASE' not in MAIN+CLIENT+HEADER+CONFIG)
check('device advertises automatic wake disabled', 'cap["dock_voice_wake"] = false' in CLIENT)
check('automatic microphone only armed on explicit authenticated followup',
      MAIN.count('audio.startAutoCapture(')==1 and
      'if (!status.docked || !agent.connected() || armId.length() == 0 || timeoutMs == 0) return;' in MAIN and
      'agent.setFollowupArmCallback(onFollowupArm);' in MAIN)
check('followup window is Gateway-supplied, not forcibly shortened',
      'audio.startAutoCapture(\n      timeoutMs,' in MAIN and 'DOCK_COMMAND_MAX_MS' in MAIN)
check('followup cancel/abort and PTT path retained',
      'agent.sendPttAbort("follow_up", reason);' in MAIN and
      'agent.sendPttStart("follow_up", followupArmId.c_str());' in MAIN and
      'agent.sendPttStart("button_a");' in MAIN and
      'agent.sendPttStop("button_a");' in MAIN)
check('A6 gates both app messages and microphone upload',
      'return _wsConnected && _a6AuthOk && _gatewayReady;' in HEADER and
      'if (!connected() || !data || !len) return false;' in CLIENT and
      'if (!connected()) {' in CLIENT and
      'MiniA6Auth::createChallengeResponse' in CLIENT)
check('Dock standby is 20%, independent of battery threshold',
      'LCD_BRIGHT_DOCK_STANDBY = 51;' in CONFIG and
      'M5.Power.getVBUSVoltage()' in MAIN and
      'charging == m5::Power_Class::is_charging' in MAIN)
check('Dock brightness controller does not start mic or deep sleep',
      'static void serviceDockStandbyBrightness()' in MAIN and
      'setDockStandbyBrightness();' in MAIN and
      not re.search(r'deepSleep|esp_deep_sleep|esp_light_sleep', MAIN))
check('original audio algorithm remains available to followup',
      'AudioIO::startAutoCapture(' in AUDIO and
      'AudioIO::processAutoFrame(' in AUDIO)
check('private files and cache ignored', all(s in IGNORE for s in
      ('/include/secrets.h','/.pio-local/','/.pio-venv/','/.pio/')))
check('public example contains only placeholders',
      'YOUR_WIFI_SSID' in (ROOT/'include/secrets.example.h').read_text() and
      'YOUR_WIFI_PASSWORD' in (ROOT/'include/secrets.example.h').read_text())
check('no private files included in released repository',
      not any((ROOT/p).exists() for p in ('include/secrets.h','include/mini_a6_local.h','.pio-local','.pio-venv','.pio')))
check('retained existing auth + network + audio interfaces',
      all((ROOT/'src'/p).exists() for p in
          ('mini_a6_auth.cpp','mini_a6_credentials.cpp','mini_a6_console.cpp',
           'agent_client.cpp','audio_io.cpp','face_frames.cpp','ui_face.cpp')))
print('STATIC CLEAN REPOSITORY TEST PASS (no firmware build performed)')
