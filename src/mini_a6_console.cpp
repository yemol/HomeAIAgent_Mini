#include "mini_a6_console.h"
#include "mini_a6_credentials.h"
#include <Arduino.h>
#include <string.h>

namespace MiniA6Console {
namespace {
char line[80] = {};
size_t used = 0;
bool overflow = false;

void wipeLine() {
  MiniA6Credentials::secureErase(line, sizeof(line));
  used = 0;
}

bool process(bool authenticated) {
  bool restart = false;
  if (!strcmp(line, "AUTH SHOW")) {
    show(authenticated);
  } else if (!strcmp(line, "AUTH CLEAR")) {
    if (MiniA6Credentials::clearSecret()) {
      Serial.println("[AUTH] credential cleared; rebooting");
      restart = true;
    } else {
      Serial.println("[AUTH] credential clear failed (NVS)");
    }
  } else if (!strncmp(line, "AUTH SET ", 9)) {
    // Do not print invalid input, even in debug logs.
    const char* secret = line + 9;
    if (MiniA6Credentials::saveSecret(secret)) {
      Serial.println("[AUTH] credential saved; rebooting");
      restart = true;
    } else {
      Serial.println("[AUTH] invalid secret or NVS write failed; current key unchanged if validation failed");
    }
  } else if (!strcmp(line, "AUTH HELP")) {
    Serial.println("[AUTH] commands: AUTH SHOW | AUTH SET <secret> | AUTH CLEAR");
  } else if (used) {
    Serial.println("[AUTH] unknown command; AUTH HELP");
  }
  wipeLine();
  return restart;
}
}  // namespace

void show(bool authenticated) {
  Serial.printf("[AUTH] device=homeai-mini-bedroom-01 credential=%s session=%s\n",
    MiniA6Credentials::isConfigured() ? "SET" : "MISSING",
    authenticated ? "AUTHENTICATED" : "NOT_AUTHENTICATED");
}

bool poll(bool authenticated) {
  // Bound work each UI iteration; never allow an unbounded serial burst to
  // starve audio, WebSocket or the main-loop watchdog.
  unsigned serviced = 0;
  while (Serial.available() > 0 && serviced++ < 80) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      if (overflow) {
        Serial.println("[AUTH] input too long; discarded");
        overflow = false;
        wipeLine();
      } else if (process(authenticated)) {
        return true;
      }
      continue;
    }
    // Only printable ASCII is allowed in console commands; secrets are URL-safe.
    if (c < 32 || c > 126) {
      overflow = true;
      continue;
    }
    if (overflow) continue;
    if (used + 1 >= sizeof(line)) {
      overflow = true;
      wipeLine();
      continue;
    }
    line[used++] = static_cast<char>(c);
    line[used] = '\0';
  }
  return false;
}
}  // namespace MiniA6Console
