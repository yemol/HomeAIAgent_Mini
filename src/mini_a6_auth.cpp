#include "mini_a6_auth.h"
#include "mini_a6_crypto.h"
#include "mini_a6_credentials.h"
#include <esp_random.h>
#include <string.h>


namespace {
// Used only to validate structure of the Gateway's server-provided challenge.
bool isLowercaseHex32(const char* s) {
  if (!s || strlen(s) != 32) return false;
  for (size_t i = 0; i < 32; ++i) {
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return false;
  }
  return true;
}
bool isBase64Url32(const char* s) {
  if (!s || strlen(s) != 32) return false; // Gateway generates 24 random bytes
  for (size_t i = 0; i < 32; ++i) {
    char c = s[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}
} // namespace

bool MiniA6Auth::isConfigured() {
  return MiniA6Credentials::isConfigured();
}

bool MiniA6Auth::createChallengeResponse(JsonDocument& challenge, const char* expectedId,
                                         String& outJson, const char*& error) {
  error = "invalid_challenge";
  const char* protocol = challenge["protocol"] | "";
  const char* deviceId = challenge["device_id"] | "";
  const char* challengeId = challenge["challenge_id"] | "";
  const char* serverNonce = challenge["server_nonce"] | "";
  const char* algorithm = challenge["algorithm"] | "";
  const int expiryMs = challenge["expires_in_ms"] | 0;
  if (strcmp(protocol, "homeai-auth/1") != 0 || !expectedId ||
      strcmp(deviceId, expectedId) != 0 || strcmp(algorithm, "HMAC-SHA256") != 0 ||
      !isLowercaseHex32(challengeId) || !isBase64Url32(serverNonce) ||
      expiryMs <= 0 || expiryMs > 60000) return false;

  char secret[MiniA6Credentials::SECRET_CAPACITY] = {};
  if (!MiniA6Credentials::readSecret(secret)) {
    error = "secret_missing_or_invalid";
    return false;
  }
  uint8_t key[MiniA6Crypto::KEY_BYTES] = {};
  const bool decoded = MiniA6Crypto::decodeDeviceSecret(secret, key);
  MiniA6Credentials::secureErase(secret, sizeof(secret));
  if (!decoded) {
    error = "secret_decode_failed";
    return false;
  }
  uint8_t nonce[MiniA6Crypto::NONCE_BYTES] = {};
  esp_fill_random(nonce, sizeof(nonce));
  char nonceB64[MiniA6Crypto::NONCE_B64_CAP] = {};
  char proofB64[MiniA6Crypto::PROOF_B64_CAP] = {};
  bool ok = MiniA6Crypto::encodeBase64Url(nonce, sizeof(nonce), nonceB64, sizeof(nonceB64));
  if (ok) ok = MiniA6Crypto::createProof(key, deviceId, challengeId, serverNonce, nonceB64, proofB64);
  memset(key, 0, sizeof(key));
  memset(nonce, 0, sizeof(nonce));
  if (!ok) {
    error = "proof_build_failed";
    return false;
  }
  JsonDocument reply;
  reply["type"] = "security.auth";
  reply["device_id"] = expectedId;
  reply["challenge_id"] = challengeId;
  reply["client_nonce"] = nonceB64;
  reply["proof"] = proofB64;
  outJson = "";
  serializeJson(reply, outJson);
  memset(proofB64, 0, sizeof(proofB64));
  memset(nonceB64, 0, sizeof(nonceB64));
  error = "";
  return true;
}
