#pragma once
// Serial-only local A6 credential provisioning.
// Never logs/echoes secret or accepts secrets from Wi-Fi / Gateway.
namespace MiniA6Console {
void show(bool authenticated);
// Returns true only when successful SET/CLEAR requires immediate reboot.
bool poll(bool authenticated);
}
