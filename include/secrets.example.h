#pragma once

// ---- Wi-Fi ----
static constexpr const char* WIFI_SSID = "YOUR_WIFI_SSID";
static constexpr const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---- Existing HomeAIAgent Gateway ----
// Example: ws://192.168.1.20:8765/companion
static constexpr const char* GATEWAY_HOST = "192.168.1.20";
static constexpr uint16_t    GATEWAY_PORT = 8765;
static constexpr const char* GATEWAY_PATH = "/companion";

// Optional shared token. Keep empty if your LAN Gateway does not use one.
static constexpr const char* GATEWAY_TOKEN = "";
