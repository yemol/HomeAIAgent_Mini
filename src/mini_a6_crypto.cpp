#include "mini_a6_crypto.h"
#include <mbedtls/md.h>
#include <stdio.h>
#include <string.h>

namespace MiniA6Crypto {
namespace {
constexpr char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
int b64Digit(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}
// Generated Gateway secrets contain exactly 32 random bytes encoded without '='.
// Reject noncanonical inputs, including silently truncated or padded keys.
} // namespace

bool decodeDeviceSecret(const char* encoded, uint8_t outKey[KEY_BYTES]) {
  if (!encoded || !outKey || strlen(encoded) != 43) return false;
  uint32_t bits = 0;
  unsigned count = 0;
  size_t written = 0;
  for (size_t n = 0; n < 43; ++n) {
    const int d = b64Digit(encoded[n]);
    if (d < 0) return false;
    bits = (bits << 6) | static_cast<uint32_t>(d);
    count += 6;
    if (count >= 8) {
      count -= 8;
      if (written >= KEY_BYTES) return false;
      outKey[written++] = static_cast<uint8_t>((bits >> count) & 0xffu);
      bits &= (1u << count) - 1u;
    }
  }
  return written == KEY_BYTES && count == 2 && bits == 0;
}

bool encodeBase64Url(const uint8_t* in, size_t len, char* out, size_t capacity) {
  if (!in || !out || len > 64) return false;
  const size_t size = ((len / 3) * 4) + ((len % 3) ? (len % 3) + 1 : 0);
  if (capacity <= size) return false;
  size_t i = 0, p = 0;
  while (i + 3 <= len) {
    const uint32_t triplet = (uint32_t(in[i]) << 16) | (uint32_t(in[i+1]) << 8) | in[i+2];
    out[p++] = B64[(triplet >> 18) & 63];
    out[p++] = B64[(triplet >> 12) & 63];
    out[p++] = B64[(triplet >> 6) & 63];
    out[p++] = B64[triplet & 63];
    i += 3;
  }
  if (len - i == 1) {
    const uint32_t rest = uint32_t(in[i]) << 16;
    out[p++] = B64[(rest >> 18) & 63];
    out[p++] = B64[(rest >> 12) & 63];
  } else if (len - i == 2) {
    const uint32_t rest = (uint32_t(in[i]) << 16) | (uint32_t(in[i+1]) << 8);
    out[p++] = B64[(rest >> 18) & 63];
    out[p++] = B64[(rest >> 12) & 63];
    out[p++] = B64[(rest >> 6) & 63];
  }
  out[p] = '\0';
  return p == size;
}

bool createProof(const uint8_t key[KEY_BYTES], const char* did, const char* challenge,
                 const char* serverNonce, const char* clientNonce,
                 char outProof[PROOF_B64_CAP]) {
  if (!key || !did || !challenge || !serverNonce || !clientNonce || !outProof) return false;
  // Strict limits prevent accidental truncation changing the signed message.
  if (!*did || strlen(did) > 128 || !*challenge || strlen(challenge) > 64 ||
      !*serverNonce || strlen(serverNonce) > 128 || !*clientNonce || strlen(clientNonce) > 128)
    return false;
  char message[512] = {};
  const int n = snprintf(message, sizeof(message), "homeai-auth/1\n%s\n%s\n%s\n%s",
                         did, challenge, serverNonce, clientNonce);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(message)) return false;
  const mbedtls_md_info_t* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!sha256) return false;
  uint8_t digest[32] = {};
  const int rc = mbedtls_md_hmac(sha256, key, KEY_BYTES,
      reinterpret_cast<const unsigned char*>(message), static_cast<size_t>(n), digest);
  memset(message, 0, sizeof(message));
  if (rc != 0) return false;
  const bool ok = encodeBase64Url(digest, sizeof(digest), outProof, PROOF_B64_CAP);
  memset(digest, 0, sizeof(digest));
  return ok;
}
} // namespace MiniA6Crypto
