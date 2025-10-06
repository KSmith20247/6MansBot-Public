#pragma once
#include "pch.h"
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <libwebsockets.h>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class WebSocketClient {
public:
    // Callback function type for receiving messages
    using MessageCallback = std::function<void(const json& message)>;
    using ConnectionCallback = std::function<void(bool connected)>;

    WebSocketClient();
    ~WebSocketClient();

    // Non-copyable
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    // Start the WebSocket connection
    bool start(const std::string& url, const std::string& token,
        MessageCallback messageCallback,
        ConnectionCallback connectionCallback = nullptr);

    // Stop the WebSocket connection
    void stop();

    // Send a message to the server
    bool sendMessage(const json& message);

    // Check if connected
    bool isConnected() const;

private:
    struct ConnectionData {
        WebSocketClient* client;
        std::string token;
        MessageCallback messageCallback;
        ConnectionCallback connectionCallback;
        std::string writeBuffer;
        bool connectionEstablished;
        std::atomic<bool> shouldReconnect;
    };

    // LibWebSockets callback
    static int websocketCallback(struct lws* wsi, enum lws_callback_reasons reason,
        void* user, void* in, size_t len);

    // Helper methods
    void runEventLoop();
    bool parseUrl(const std::string& url, std::string& host, int& port, std::string& path, bool& useSSL);
    void handleMessage(const std::string& message);
    void scheduleReconnect();

    // Member variables
    std::unique_ptr<std::thread> eventThread_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;

    struct lws_context* context_;
    struct lws* websocket_;
    struct lws_protocols protocols_[2]; // null-terminated

    std::string serverHost_;
    int serverPort_;
    std::string serverPath_;
    bool useSSL_;

    std::unique_ptr<ConnectionData> connectionData_;

    mutable std::mutex writeMutex_;
    std::string pendingWrite_;
    bool writePending_;
};