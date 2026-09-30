#!/usr/bin/env python3
"""Host-only syntax check using narrow external API shims.
Catches integration mistakes but is NOT a real Arduino/PIO compilation.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

P = Path(__file__).resolve().parents[1]
HEADERS = {
'Arduino.h': r'''
#pragma once
#include <string>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/time.h>
using String = std::string;
unsigned long millis();
void delay(unsigned long ms);
void configTime(long tz,long dst,const char*,const char*,const char*);
struct SerialClass { void println(const char*); int available(); int read(); template<class... T>void printf(const char*,T...); };
extern SerialClass Serial;
''',
'app_types.h': r'''
#pragma once
#include <stdint.h>
enum class FaceState:uint8_t {DOCK_IDLE,HAND_IDLE,WAKE,LISTENING,THINKING,SPEAKING,HAPPY,CONFUSED,CRYING,ERROR_STATE,SLEEPY};
''',
'secrets.example.h': r'''
#pragma once
static constexpr const char* WIFI_SSID="TEST";
static constexpr const char* WIFI_PASSWORD="TEST";
static constexpr const char* GATEWAY_HOST="127.0.0.1";
static constexpr uint16_t GATEWAY_PORT=8765;
static constexpr const char* GATEWAY_PATH="/ws/device";
static constexpr const char* GATEWAY_TOKEN="";
''',
'WiFi.h': r'''
#pragma once
#include "Arduino.h"
using wl_status_t = int;
constexpr int WIFI_STA=1;
constexpr int WL_IDLE_STATUS=0, WL_NO_SSID_AVAIL=1, WL_SCAN_COMPLETED=2;
constexpr int WL_CONNECTED=3, WL_CONNECT_FAILED=4, WL_CONNECTION_LOST=5, WL_DISCONNECTED=6;
struct IPAddress {String toString();};
struct WifiMock {
 void mode(int);void setSleep(bool); void disconnect(bool,bool);void begin(const char*,const char*);
 wl_status_t status(); String SSID(); IPAddress localIP(); IPAddress gatewayIP(); IPAddress dnsIP(int);
 int RSSI();int channel();
};
extern WifiMock WiFi;
''',
'WebSocketsClient.h': r'''
#pragma once
#include "Arduino.h"
enum WStype_t {WStype_ERROR,WStype_DISCONNECTED,WStype_CONNECTED,WStype_TEXT,WStype_BIN};
struct WebSocketsClient {
 void begin(const char*,uint16_t,const char*);
 void onEvent(void(*)(WStype_t,uint8_t*,size_t));
 void setReconnectInterval(unsigned long);
 void enableHeartbeat(unsigned long,unsigned long,unsigned);
 void loop();void disconnect();
 bool sendTXT(const String&);bool sendBIN(const uint8_t*,size_t);
};
''',
'ArduinoJson.h': r'''
#pragma once
#include "Arduino.h"
struct JsonVariantMock {
 const char* operator|(const char*) const;
 unsigned int operator|(unsigned int) const;
 int operator|(int) const;
 template<class T> JsonVariantMock& operator=(const T&){return *this;}
 template<class T>T to() const {return T{};}
 JsonVariantMock operator[](const char*) const {return {};}
};
using JsonObject = JsonVariantMock;
struct JsonDocument {JsonVariantMock operator[](const char*) {return {};}};
struct DeserializationError {explicit operator bool() const;};
DeserializationError deserializeJson(JsonDocument&,const uint8_t*,size_t);
size_t serializeJson(JsonDocument&,String&);
''',
'M5Unified.h': r'''
#pragma once
#include <time.h>
namespace m5 { struct rtc_datetime_t { rtc_datetime_t(const tm&); }; }
struct RtcMock {bool isEnabled();void setDateTime(m5::rtc_datetime_t);};
struct M5Mock {RtcMock Rtc;};
extern M5Mock M5;
''',
'WiFiClient.h': r'''
#pragma once
struct WiFiClient {bool connect(const char*,unsigned,unsigned long);void stop();};
''',
'Preferences.h': r'''
#pragma once
#include <stddef.h>
class Preferences {public: bool begin(const char*,bool=false); size_t getString(const char*,char*,size_t) const; size_t putString(const char*,const char*); bool remove(const char*); void end();};
''',
'esp_random.h': r'''
#pragma once
#include <stddef.h>
void esp_fill_random(void*,size_t);
''',
'mbedtls/md.h': r'''
#pragma once
#include <stddef.h>
#define MBEDTLS_MD_SHA256 6
typedef struct mbedtls_md_info_t mbedtls_md_info_t;
const mbedtls_md_info_t* mbedtls_md_info_from_type(int);
int mbedtls_md_hmac(const mbedtls_md_info_t*,const unsigned char*,size_t,
                    const unsigned char*,size_t,unsigned char*);
''',
}

def main():
 cc=shutil.which('clang++') or shutil.which('g++')
 if not cc: raise RuntimeError('No host compiler: syntax test not run')
 with tempfile.TemporaryDirectory() as t:
  root=Path(t)
  for name,content in HEADERS.items():
   p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content)
  sources=['mini_a6_crypto.cpp','mini_a6_credentials.cpp','mini_a6_auth.cpp','mini_a6_console.cpp','agent_client.cpp']
  for src in sources:
   cmd=[cc,'-std=c++17','-Wall','-Wextra','-Werror','-fsyntax-only','-I',str(root),
        '-I',str(P/'src'),'-I',str(P/'include'),str(P/'src'/src)]
   proc=subprocess.run(cmd,text=True,capture_output=True)
   if proc.returncode:
    print(proc.stdout+proc.stderr)
    raise RuntimeError(f'FAIL host shim compile: {src}')
   print(f'PASS host syntax: {src}')
 print('HOST SYNTAX PASS (not a target PlatformIO firmware build)')
if __name__=='__main__':main()
