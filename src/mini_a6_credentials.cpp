#include "mini_a6_credentials.h"
#include "mini_a6_crypto.h"
#include <Preferences.h>
#include <string.h>
#include <stdint.h>

namespace MiniA6Credentials {
namespace {
constexpr char NAMESPACE[] = "homeai_auth";  // same namespace as main Agent
constexpr char KEY[] = "secret";

bool valid(const char* value) {
  uint8_t decoded[MiniA6Crypto::KEY_BYTES] = {};
  const bool ok = MiniA6Crypto::decodeDeviceSecret(value, decoded);
  secureErase(decoded, sizeof(decoded));
  return ok;
}
}  // namespace

void secureErase(void* data, size_t size) {
  volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
  while (size--) *p++ = 0;
}

bool readSecret(char out[SECRET_CAPACITY]) {
  if (!out) return false;
  memset(out, 0, SECRET_CAPACITY);
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, true)) return false;
  const size_t n = prefs.getString(KEY, out, SECRET_CAPACITY);
  prefs.end();
  if (n == 0 || !valid(out)) {
    secureErase(out, SECRET_CAPACITY);
    return false;
  }
  return true;
}

bool isConfigured() {
  char secret[SECRET_CAPACITY] = {};
  const bool ok = readSecret(secret);
  secureErase(secret, sizeof(secret));
  return ok;
}

bool saveSecret(const char* secret) {
  if (!valid(secret)) return false;  // Reject before touching old key.
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return false;
  const size_t written = prefs.putString(KEY, secret);
  prefs.end();
  if (written < SECRET_LENGTH) return false;
  // Verify persisted value, not just the NVS API return code.
  char actual[SECRET_CAPACITY] = {};
  const bool ok = readSecret(actual) && strcmp(secret, actual) == 0;
  secureErase(actual, sizeof(actual));
  return ok;
}

bool clearSecret() {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return false;
  const bool removed = prefs.remove(KEY);
  prefs.end();
  return removed || !isConfigured();
}
}  // namespace MiniA6Credentials
