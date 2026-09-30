#pragma once
#include <stddef.h>
#include <stdint.h>

// Host-testable, Arduino-independent implementation of the exact Gateway
// homeai-auth/1 HMAC message. Callers own the 32-byte key and nonce.
namespace MiniA6Crypto {
constexpr size_t KEY_BYTES = 32;
constexpr size_t NONCE_BYTES = 24;
constexpr size_t PROOF_B64_CAP = 44; // 43 URL-safe base64 chars + NUL
constexpr size_t NONCE_B64_CAP = 33; // 32 chars + NUL
bool decodeDeviceSecret(const char* base64url, uint8_t outKey[KEY_BYTES]);
bool encodeBase64Url(const uint8_t* bytes, size_t bytesLength, char* out, size_t outCapacity);
bool createProof(const uint8_t key[KEY_BYTES], const char* deviceId,
                 const char* challengeId, const char* serverNonce,
                 const char* clientNonce, char outProof[PROOF_B64_CAP]);
} // namespace MiniA6Crypto
