#pragma once

#include <string>

namespace SixMansConfig {
    // WebSocket Configuration
    constexpr const char* DEFAULT_WS_HOST = "192.168.1.2";
    constexpr int DEFAULT_WS_PORT = 8080;
    constexpr const char* DEFAULT_WS_PATH = "/";
    constexpr bool DEFAULT_WS_USE_SSL = false; //remember to flip to true for real 6mans

    // Default values
    constexpr const char* DEFAULT_TOKEN = "";
    constexpr int RECONNECT_DELAY_MS = 5000;
    constexpr int PING_INTERVAL_MS = 60000;
    constexpr int MAX_QUEUE_SIZE = 100;

    // Build full WebSocket URL
    inline std::string buildWebSocketUrl() {
        std::string protocol = DEFAULT_WS_USE_SSL ? "wss://" : "ws://";
        return protocol + DEFAULT_WS_HOST + ":" + std::to_string(DEFAULT_WS_PORT) + DEFAULT_WS_PATH;
    }
}
