#pragma once
#include <stddef.h>

// Device credential lives ONLY in the Mini's NVS (namespace homeai_auth).
// No firmware-embedded secret; provisioning is exclusively through physical serial.
namespace MiniA6Credentials {
constexpr size_t SECRET_LENGTH = 43;
constexpr size_t SECRET_CAPACITY = SECRET_LENGTH + 1;
bool readSecret(char out[SECRET_CAPACITY]);
bool isConfigured();
bool saveSecret(const char* secret);
bool clearSecret();
void secureErase(void* data, size_t size);
}
