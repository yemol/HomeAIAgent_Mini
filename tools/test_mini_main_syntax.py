#!/usr/bin/env python3
"""Host-only syntax check of unmodified main.cpp using minimal typed API shims.
Does not replace the real ESP32 PlatformIO build or claim ABI compatibility.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
import shutil, subprocess
ROOT=Path(__file__).resolve().parents[1]
FAKES={
'Arduino.h':r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
using String=std::string;
struct SerialMock {template<typename ...T> void printf(const char*,T...){} void println(const char* = ""){} void flush(){} };
inline SerialMock Serial;
inline uint32_t millis(){return 123;}
inline void delay(unsigned){}
struct ESPMock {void restart(){}};
inline ESPMock ESP;
''',
'M5Unified.h':r'''
#pragma once
#include "Arduino.h"
namespace m5 {struct Power_Class {enum ChargeState {is_charging,is_not_charging};};}
struct ConfigMock {int serial_baudrate;bool internal_imu;bool output_power;struct {bool hat_spk2;} external_speaker;};
struct PowerMock {int16_t getVBUSVoltage() const{return 5000;} m5::Power_Class::ChargeState isCharging() const{return m5::Power_Class::is_charging;} bool getExtOutput()const{return true;} int16_t getBatteryVoltage()const{return 4200;} int getBatteryLevel()const{return 90;} void setExtOutput(bool){} };
struct DisplayMock {void wakeup(){} };
struct ButtonMock {bool wasPressed()const{return false;} bool wasReleased()const{return false;}bool wasClicked()const{return false;}bool wasHold()const{return false;}};
struct M5Mock {PowerMock Power; DisplayMock Display;ButtonMock BtnA,BtnB;ConfigMock config(){return {};}; void begin(ConfigMock){} void update(){} int getBoard()const{return 4;}};
inline M5Mock M5;
''',
'ui_face.h':r'''
#pragma once
#include "Arduino.h"
#include "app_types.h"
struct FaceUI {void begin(){} void setState(FaceState,bool=false){} FaceState state()const{return FaceState::DOCK_IDLE;}void setStatus(const DeviceStatus&){}void setBrightness(uint8_t){} void setAudioLevel(float){}void endDemoExpression(){}void cycleDemoExpression(){}bool demoExpressionActive()const{return false;}void forceRedraw(){}void update(uint32_t){} };
''',
'audio_io.h':r'''
#pragma once
#include "Arduino.h"
struct AudioIO {using SendFn=bool(*)(const uint8_t*,size_t);using Fn=void(*)();void begin(){}void setSendCallback(SendFn){}void setAutoCaptureCallbacks(Fn,Fn,Fn){}bool capturing()const{return false;}bool autoCapturing()const{return false;}bool autoUtteranceActive()const{return false;}bool playing()const{return false;}void stopAutoCapture(){}bool startAutoCapture(uint32_t,uint32_t,uint32_t){return true;}void serviceAutoCapture(){}void startPlayback(uint32_t){}void pushPlayback(const uint8_t*,size_t){}void requestPlaybackFinish(){}void servicePlayback(){}void stopPlayback(){}bool startCapture(){return true;}void stopCapture(){}void serviceCapture(){}float inputLevel()const{return 0;}float outputLevel()const{return 0;}void playDiagnosticTone(){} };
''',
'agent_client.h':r'''
#pragma once
#include "Arduino.h"
#include "app_types.h"
struct AgentClient {using StateFn=void(*)(FaceState,const String&,uint32_t);using BinFn=void(*)(const uint8_t*,size_t);using FollowupFn=void(*)(const String&,uint32_t);void begin(){}void loop(){}bool authenticated()const{return true;}bool connected()const{return true;}bool wifiConnected()const{return true;}bool sendBinary(const uint8_t*,size_t){return true;}void setStateCallback(StateFn){}void setBinaryCallback(BinFn){}void setFollowupArmCallback(FollowupFn){}void sendPttStart(const char*,const char* =nullptr){}void sendPttStop(const char*){}void sendPttAbort(const char*,const char*){}void sendDockState(bool,int){} };
''',
'mini_a6_console.h':r'''
#pragma once
struct MiniA6Console {static void show(bool){}static bool poll(bool){return false;}};
''',
}
for must_exist in ('src/audio_io.h','src/ui_face.h','src/agent_client.h','src/mini_a6_console.h'):
    assert (ROOT/must_exist).exists(), must_exist
with TemporaryDirectory() as tmp:
    work=Path(tmp)
    for name,content in FAKES.items(): (work/name).write_text(content)
    for name in ('app_config.h','app_types.h','secrets.example.h'):
        shutil.copy2(ROOT/'include'/name,work/name)
    shutil.copy2(ROOT/'src/main.cpp',work/'main.cpp')
    result=subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',
       '-Wno-unused-parameter','-fsyntax-only','-I',str(work),str(work/'main.cpp')],capture_output=True,text=True)
    if result.returncode:raise SystemExit(result.stderr)
print('PASS host C++ syntax of the exact cleaned main.cpp using typed mocks')
print('HOST-ONLY: ESP32 framework build and full cross-translation-unit link remain unverified')
