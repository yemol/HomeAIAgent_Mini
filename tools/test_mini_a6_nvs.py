#!/usr/bin/env python3
"""Exercise actual Mini NVS credentials + physical serial parser under host mocks.
Not a target ESP32 compile and not a claim that target NVS has been tested.
"""
from __future__ import annotations
import base64
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_mini_a6_crypto import MOCK_MBEDTLS
from test_mini_a6_exchange import PREFERENCES_MOCK

ROOT=Path(__file__).resolve().parents[1]
ARDUINO=r'''
#pragma once
#include <string>
#include <cstdio>
struct MockSerial {
 std::string in, log;
 int available(){return (int)in.size();}
 int read(){int ch=in[0];in.erase(0,1);return ch;}
 void println(const char* s){log+=s; log+='\n';}
 template<class... T>void printf(const char* fmt,T...args){
   char b[512]={};snprintf(b,sizeof(b),fmt,args...);log+=b;
 }
 void feed(const std::string& s){in+=s;}
};
extern MockSerial Serial;
'''
MAIN=r'''
#include "mini_a6_credentials.h"
#include "mini_a6_console.h"
#include "Preferences.h"
#include "Arduino.h"
#include <cassert>
#include <cstdio>
#include <string>
MockSerial Serial;
int main(int argc,char** argv){
 assert(argc==2);
 const std::string key=argv[1];
 assert(!MiniA6Credentials::isConfigured());
 MiniA6Console::show(false);
 assert(Serial.log.find("credential=MISSING")!=std::string::npos);
 Serial.log.clear();
 // Ignore invalid key and avoid echoing it.
 Serial.feed("AUTH SET obviously-invalid\n");
 assert(!MiniA6Console::poll(false));
 assert(!MiniA6Credentials::isConfigured());
 assert(Serial.log.find("obviously-invalid")==std::string::npos);
 Serial.log.clear();
 Serial.feed("AUTH SET "+key+"\r\n");
 assert(MiniA6Console::poll(false));
 assert(Serial.log.find("credential saved")!=std::string::npos);
 assert(Serial.log.find(key)==std::string::npos);
 assert(MiniA6Credentials::isConfigured());
 char stored[MiniA6Credentials::SECRET_CAPACITY]={};
 assert(MiniA6Credentials::readSecret(stored));
 assert(key==stored);
 MiniA6Credentials::secureErase(stored,sizeof(stored));
 // Distinct preferences instance observes the same saved NVS value.
 Preferences fresh;
 assert(fresh.begin("homeai_auth",true));
 char persisted[44]={};
 assert(fresh.getString("secret",persisted,sizeof(persisted))>0);
 assert(key==persisted);fresh.end();
 Serial.log.clear();
 Serial.feed("AUTH SHOW\n");
 assert(!MiniA6Console::poll(true));
 assert(Serial.log.find("credential=SET session=AUTHENTICATED")!=std::string::npos);
 assert(Serial.log.find(key)==std::string::npos);
 Serial.log.clear();
 // Invalid changes must preserve the original.
 Serial.feed("AUTH SET invalid!\n");
 assert(!MiniA6Console::poll(true));
 assert(MiniA6Credentials::isConfigured());
 assert(Serial.log.find(key)==std::string::npos);
 // Overflow must discard, not interpret the following valid-looking suffix.
 Serial.log.clear();
 Serial.feed("AUTH SET "+std::string(200,'Z')+"\n");
 while(Serial.available())assert(!MiniA6Console::poll(true));
 assert(MiniA6Credentials::isConfigured());
 assert(Serial.log.find("too long")!=std::string::npos);
 // Set/Clear instructions are only those on the local serial interface.
 Serial.log.clear();
 Serial.feed("AUTH CLEAR\n");
 assert(MiniA6Console::poll(true));
 assert(!MiniA6Credentials::isConfigured());
 assert(Serial.log.find(key)==std::string::npos);
 Serial.log.clear();
 Serial.feed("AUTH SHOW\n");
 assert(!MiniA6Console::poll(false));
 assert(Serial.log.find("credential=MISSING")!=std::string::npos);
 printf("PASS NVS: provision, re-read, view-without-leak, invalid-key retention, overflow discard, clear\n");
}
'''

def main():
 cc=shutil.which('clang++') or shutil.which('g++')
 if not cc:raise RuntimeError('Missing host C++ compiler')
 fixture=base64.urlsafe_b64encode(bytes(range(32))).decode().rstrip('=')
 with tempfile.TemporaryDirectory() as td:
  work=Path(td)
  for name,body in {'mbedtls/md.h':MOCK_MBEDTLS,'Preferences.h':PREFERENCES_MOCK,
                    'Arduino.h':ARDUINO,'main.cpp':MAIN}.items():
   f=work/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(body)
  exe=work/'test_nvs'
  sources=['mini_a6_crypto.cpp','mini_a6_credentials.cpp','mini_a6_console.cpp']
  cmd=[cc,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(work),'-I',str(ROOT/'src'),
       str(work/'main.cpp'),*[str(ROOT/'src'/s) for s in sources],'-lcrypto','-o',str(exe)]
  subprocess.run(cmd,check=True,capture_output=True,text=True)
  print('PASS NVS: real Mini credential and console source compile with host API mocks')
  proc=subprocess.run([str(exe),fixture],check=True,capture_output=True,text=True)
  print(proc.stdout.strip())
 print('HOST NVS TEST PASS (not actual ESP32 hardware)')
if __name__=='__main__':main()
