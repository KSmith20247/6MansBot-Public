#pragma once
#include "pch.h"
#include "WebSocketClient.h"
#include "ThreadSafeQueue.h"
#include <string>
#include <memory>
#include <atomic>
#include <nlohmann/json.hpp>

#include <mutex>
#include <thread>
#include <queue>
#include <optional>

using json = nlohmann::json;

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // Non-copyable
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // Start the network manager with given URL and token
    bool start(const std::string& url, const std::string& token);

    // Stop the network manager
    void stop();

    // Check if connected to the server
    bool isConnected() const;

    // Get the next message from the queue (non-blocking)
    // Returns std::nullopt if no messages available
    std::optional<json> getNextMessage();

    // Send a message to the server
    bool sendMessage(const json& message);

    // Get current queue size
    size_t getQueueSize() const;

    // Clear all pending messages
    void clearQueue();

private:
    // Callback functions for WebSocket client
    void onMessageReceived(const json& message);
    void onConnectionChanged(bool connected);

    // Validate and process incoming messages
    bool validateMessage(const json& message);
    void processMessage(const json& message);

    // Member variables
    std::unique_ptr<WebSocketClient> wsClient_;
    ThreadSafeQueue<json> messageQueue_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;

    std::string currentUrl_;
    std::string currentToken_;
};