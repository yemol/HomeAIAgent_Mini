#!/usr/bin/env python3
"""Host Mini responder <-> actual Gateway A6 handshake integration.
Production Mini .cpp files are compiled with test ArduinoJson/RNG/mbedTLS
shims. No claim of ESP32 firmware compilation or hardware randomness test.
"""
from __future__ import annotations
import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SRC = Path(__file__).resolve().parents[1]/'src'
ARDUINO=r'''
#pragma once
#include <string>
using String=std::string;
'''
JSON_MOCK=r'''
#pragma once
#include <string>
#include <map>
#include <stdlib.h>
using String=std::string;
struct Field {
 std::string value;
 const char* operator|(const char* fallback) const {return value.empty()?fallback:value.c_str();}
 int operator|(int fallback) const {return value.empty()?fallback:atoi(value.c_str());}
 Field& operator=(const char* s) {value=s?s:"";return *this;}
 Field& operator=(int n){value=std::to_string(n);return *this;}
};
struct JsonDocument {std::map<std::string,Field> entries;
 Field& operator[](const char* key){return entries[key];}
};
inline void serializeJson(JsonDocument& d,String& s){
 s="{";
 for(const auto& f:d.entries){
  if(s.size()>1)s+=",";
  s+="\""+f.first+"\":\""+f.second.value+"\"";
 }
 s+="}";
}
'''
MBEDTLS=r'''
#pragma once
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <stddef.h>
#define MBEDTLS_MD_SHA256 6
typedef struct {int type;} mbedtls_md_info_t;
static inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int t){
 static mbedtls_md_info_t info={6};return t==6?&info:nullptr;
}
static inline int mbedtls_md_hmac(const mbedtls_md_info_t* i,
 const unsigned char* key,size_t k,const unsigned char* msg,size_t n,unsigned char* digest){
 unsigned int len=0;return i && HMAC(EVP_sha256(),key,static_cast<int>(k),msg,n,digest,&len) && len==32?0:-1;
}
'''
PREFERENCES_MOCK=r'''
#pragma once
#include <string>
#include <cstring>
#include <stddef.h>
class Preferences {
 public:
  inline static std::string stored="";
  bool begin(const char*,bool=false){return true;}
  size_t getString(const char*,char* out,size_t maxLen) const {
    if(stored.empty()||stored.size()+1>maxLen)return 0;
    memcpy(out,stored.c_str(),stored.size()+1);return stored.size();
  }
  size_t putString(const char*,const char* s){stored=s;return stored.size();}
  bool remove(const char*){bool existing=!stored.empty();stored.clear();return existing;}
  void end(){}
};
'''
RANDOM=r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
// TEST-ONLY deterministic substitute; production includes ESP-IDF hardware RNG.
static inline void esp_fill_random(void* data,size_t size){
 auto* p=(uint8_t*)data;for(size_t i=0;i<size;i++)p[i]=(uint8_t)(19+i*17);
}
'''
MAIN=r'''
#include "mini_a6_auth.h"
#include "mini_a6_credentials.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
int main(int argc,char** argv){
 if(argc!=8)return 12;
 if(!MiniA6Credentials::saveSecret(argv[7])) return 14;
 if(!MiniA6Auth::isConfigured())return 13;
 JsonDocument req;
 req["protocol"]="homeai-auth/1";
 req["device_id"]=argv[1];
 req["challenge_id"]=argv[2];
 req["server_nonce"]=argv[3];
 req["expires_in_ms"]=atoi(argv[4]);
 req["algorithm"]=argv[5];
 String output;
 const char* error="";
 if(!MiniA6Auth::createChallengeResponse(req,argv[6],output,error)){
   printf("REJECTED:%s\n",error);return 2;
 }
 puts(output.c_str());
 return 0;
}
'''

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--gateway',type=Path,required=True)
 args=ap.parse_args()
 sys.path.insert(0,str(args.gateway.resolve()))
 from security.device_auth import DeviceAuthManager, AuthContext
 cc=shutil.which('clang++') or shutil.which('g++')
 if not cc:raise RuntimeError('No host compiler')
 fixture=bytes(range(32))
 secret=base64.urlsafe_b64encode(fixture).decode().rstrip('=')
 did='homeai-mini-bedroom-01'
 with tempfile.TemporaryDirectory() as td:
  root=Path(td);(root/'mbedtls').mkdir()
  for name,content in {
   'Arduino.h':ARDUINO,'ArduinoJson.h':JSON_MOCK,'esp_random.h':RANDOM,
   'Preferences.h':PREFERENCES_MOCK,
   'mbedtls/md.h':MBEDTLS,'main.cpp':MAIN,
  }.items():
   p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content)
  cmd=[cc,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(root),'-I',str(SRC),
    str(root/'main.cpp'),str(SRC/'mini_a6_auth.cpp'),str(SRC/'mini_a6_credentials.cpp'),str(SRC/'mini_a6_crypto.cpp'),
    '-lcrypto','-o',str(root/'responder')]
  subprocess.run(cmd,check=True,capture_output=True,text=True)
  print('PASS 1: actual Mini responder and crypto compile with host API shims')
  config=root/'config';config.mkdir()
  (config/'registry.json').write_text(json.dumps({'version':1,'devices':{
    did:{'enabled':True,'role':'companion','scopes':['voice.send']}}}))
  (config/'secret_store.json').write_text(json.dumps({'version':1,'secrets':{did:secret}}))
  manager=DeviceAuthManager(mode='enforce',registry_file=config/'registry.json',
                           secrets_file=config/'secret_store.json',audit_log=config/'audit.log')
  def run(ch,alg='HMAC-SHA256',expected=did):
   return subprocess.run([str(root/'responder'),ch['device_id'],ch['challenge_id'],
    ch['server_nonce'],str(ch['expires_in_ms']),alg,expected,secret],capture_output=True,text=True)
  ctx=AuthContext()
  result=manager.begin(ctx,device_id=did,role='companion',peer='10.12.2.3')
  assert result['status']=='challenge',result
  challenge=result['challenge']
  generated=run(challenge)
  assert generated.returncode==0,generated.stderr+generated.stdout
  reply=json.loads(generated.stdout)
  assert reply['device_id']==did
  assert reply['type']=='security.auth'
  assert 'secret' not in reply
  verified,reason=manager.verify(ctx,reply,peer='10.12.2.3')
  assert verified and reason=='ok',(verified,reason)
  print('PASS 2: actual Gateway DeviceAuthManager accepts actual Mini responder proof')
  ok,why=manager.verify(ctx,reply,peer='10.12.2.3')
  assert not ok and why=='no_active_challenge'
  print('PASS 3: replaying a completed response is rejected by Gateway')
  for mutation in (
   {**challenge,'device_id':'homeai-agent-main-01'},
   {**challenge,'challenge_id':'not-hex'},
   {**challenge,'server_nonce':'invalid!'},
   {**challenge,'expires_in_ms':0},
  ):
   assert run(mutation).returncode==2
  assert run(challenge,alg='SHA-1').returncode==2
  assert run(challenge,expected='homeai-agent-main-01').returncode==2
  print('PASS 4: malformed, mismatched and invalid-expiry-hint challenges rejected by Mini')
  # Full Gateway WebSocket handler helpers, not only the cryptographic manager.
  import asyncio
  import companion_gateway as gateway
  gateway.DEVICE_AUTH=manager
  class FakeWS:
   remote_address=('10.12.2.3',9876)
   def __init__(self):self.sent=[];self.closed=False
   async def send(self,raw):self.sent.append(json.loads(raw))
   async def close(self,**kwargs):self.closed=True
  async def companion_roundtrip():
   ws=FakeWS()
   session=gateway.ClientSession(ws=ws)
   gateway._apply_device_hello(session,{'type':'device.hello','device_id':did,
         'device_role':'companion','capabilities':{'ptt':True}})
   assert not manager.should_allow_application(session.security)
   assert await gateway._security_begin_ws_session(session,role='companion')
   chal=ws.sent[-1]
   assert chal['type']=='security.challenge'
   resp=run(chal)
   assert resp.returncode==0,(resp.stderr,resp.stdout)
   assert await gateway._security_verify_ws_session(session,json.loads(resp.stdout))
   await gateway._finish_device_hello(session)
   assert any(e.get('type')=='gateway.ready' and e.get('security',{}).get('authenticated') for e in ws.sent)
   assert session.device_id==did and session.device_role=='companion'
   assert session.openclaw_user==gateway._openclaw_user_for_device(did)
   assert not ws.closed
  asyncio.run(companion_roundtrip())
  print('PASS 5: real Gateway companion handshake, authenticated ready and Mini session ownership')
 print('HOST EXCHANGE PASS (not a target-device test)')
if __name__=='__main__': main()
