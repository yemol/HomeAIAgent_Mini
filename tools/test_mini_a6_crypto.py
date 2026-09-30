#!/usr/bin/env python3
"""Host cross-check: compile actual Mini crypto source with an mbedTLS-API shim
backed by host OpenSSL; test every vector against Gateway's real Python HMAC.
No private credentials, network or PlatformIO downloads are used.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import hmac
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SOURCE = Path(__file__).resolve().parents[1] / 'src'

MOCK_MBEDTLS = r'''
#pragma once
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <stddef.h>
#include <stdint.h>
#define MBEDTLS_MD_SHA256 6
typedef struct { int type; } mbedtls_md_info_t;
static inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int type) {
  static const mbedtls_md_info_t info = {MBEDTLS_MD_SHA256};
  return type == MBEDTLS_MD_SHA256 ? &info : nullptr;
}
static inline int mbedtls_md_hmac(const mbedtls_md_info_t* info,
 const unsigned char* key, size_t keylen,
 const unsigned char* input, size_t ilen, unsigned char* output) {
 unsigned int outlen = 0;
 return info && HMAC(EVP_sha256(), key, static_cast<int>(keylen),
                   input, ilen, output, &outlen) && outlen == 32 ? 0 : -1;
}
'''
NATIVE = r'''
#include "mini_a6_crypto.h"
#include <cstdio>
#include <cstdint>
int main(int argc, char** argv) {
 if (argc != 6) return 10;
 uint8_t key[MiniA6Crypto::KEY_BYTES] = {};
 if (!MiniA6Crypto::decodeDeviceSecret(argv[1], key)) return 2;
 char out[MiniA6Crypto::PROOF_B64_CAP] = {};
 if (!MiniA6Crypto::createProof(key,argv[2],argv[3],argv[4],argv[5],out)) return 3;
 puts(out);
 return 0;
}
'''

def b64(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).decode().rstrip('=')

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--gateway', type=Path, help='Current Gateway folder containing security/device_auth.py')
    args = ap.parse_args()
    cc = shutil.which('clang++') or shutil.which('g++')
    if not cc:
        raise RuntimeError('HOST_COMPILE_NOT_RUN: compiler missing')
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        (root/'mbedtls').mkdir()
        (root/'mbedtls/md.h').write_text(MOCK_MBEDTLS)
        (root/'main.cpp').write_text(NATIVE)
        exe = root/'mini_a6_native_test'
        cmd = [cc,'-std=c++17','-Wall','-Wextra','-Werror', '-I', str(root),
               '-I', str(SOURCE), str(root/'main.cpp'),str(SOURCE/'mini_a6_crypto.cpp'),
               '-lcrypto','-o',str(exe)]
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        print('PASS 1: native Mini crypto source compiles with mock mbedTLS-to-OpenSSL shim')
        gateway_proof = None
        if args.gateway:
            sys.path.insert(0, str(args.gateway.resolve()))
            from security.device_auth import build_auth_proof
            gateway_proof = build_auth_proof
        secret_raw = bytes(range(32)) # Synthetic fixture ONLY.
        secret = b64(secret_raw)
        for i in range(12):
            did = 'homeai-mini-bedroom-01'
            challenge = f'{i:032x}'
            server = b64(bytes((j + i) % 256 for j in range(24)))
            client = b64(bytes((j * 7 + i) % 256 for j in range(24)))
            out = subprocess.run([str(exe),secret,did,challenge,server,client],
                                 check=True, capture_output=True,text=True).stdout.strip()
            msg = '\n'.join(('homeai-auth/1',did,challenge,server,client)).encode()
            expected = b64(hmac.new(secret_raw,msg,hashlib.sha256).digest())
            assert out == expected, (i,out,expected)
            if gateway_proof:
                assert out == gateway_proof(secret, device_id=did,
                    challenge_id=challenge,server_nonce=server,client_nonce=client)
        print('PASS 2: 12 independent vectors match Gateway wire format'+
              (' and actual Gateway build_auth_proof' if gateway_proof else ' and Python HMAC'))
        invalid = ['',secret+'=',secret[:-1]+'!',secret[:-1],secret[:-1]+'A', 'A'*43]
        # A*43 is canonical for a zero key: valid and not an error.
        for bad in invalid[:-1]:
            # Changing final char may preserve canonically valid decoded data in
            # some cases. Only test inputs known invalid by length/charset.
            if bad == secret[:-1]+'A': continue
            proc = subprocess.run([str(exe),bad,did,challenge,server,client],capture_output=True)
            assert proc.returncode == 2, (bad,proc.returncode)
        print('PASS 3: malformed, padded and truncated device secrets are rejected')
        for badid in ('', 'x'*129):
            proc = subprocess.run([str(exe),secret,badid,challenge,server,client],capture_output=True)
            assert proc.returncode == 3
        print('PASS 4: invalid device identity length fails closed')
    print('HOST TEST PASS (not a target PlatformIO firmware build)')
if __name__ == '__main__': main()
