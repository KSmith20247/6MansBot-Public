#include "pch.h"
#include "NetworkManager.h"
#include "Config.h"
#include "logging.h"
#include <mutex>
#include <thread>
#include <queue>
#include <optional>

NetworkManager::NetworkManager()
    : messageQueue_(SixMansConfig::MAX_QUEUE_SIZE),
    running_(false),
    connected_(false) {
    wsClient_ = std::make_unique<WebSocketClient>();
}

NetworkManager::~NetworkManager() {
    stop();
}

bool NetworkManager::start(const std::string& url, const std::string& token) {
    if (running_.load()) {
        LOG("NetworkManager already running");
        return false;
    }

    currentUrl_ = url;
    currentToken_ = token;
    running_.store(true);

    // Set up callbacks
    auto messageCallback = [this](const json& message) {
        this->onMessageReceived(message);
        };

    auto connectionCallback = [this](bool connected) {
        this->onConnectionChanged(connected);
        };

    // Start WebSocket client
    bool started = wsClient_->start(url, token, messageCallback, connectionCallback);
    if (!started) {
        LOG("Failed to start WebSocket client");
        running_.store(false);
        return false;
    }

    LOG("NetworkManager started successfully");
    return true;
}

void NetworkManager::stop() {
    if (!running_.load()) {
        return;
    }

    LOG("Stopping NetworkManager");
    running_.store(false);
    connected_.store(false);

    // Stop WebSocket client
    if (wsClient_) {
        wsClient_->stop();
    }

    // Clear any pending messages
    messageQueue_.clear();

    LOG("NetworkManager stopped");
}

bool NetworkManager::isConnected() const {
    return connected_.load() && wsClient_ && wsClient_->isConnected();
}

std::optional<json> NetworkManager::getNextMessage() {
    if (!running_.load()) {
        return std::nullopt;
    }

    return messageQueue_.tryPop();
}

bool NetworkManager::sendMessage(const json& message) {
    if (!isConnected()) {
        LOG("Cannot send message: NetworkManager not connected");
        return false;
    }

    return wsClient_->sendMessage(message);
}

size_t NetworkManager::getQueueSize() const {
    return messageQueue_.size();
}

void NetworkManager::clearQueue() {
    messageQueue_.clear();
}

void NetworkManager::onMessageReceived(const json& message) {
    if (!running_.load()) {
        return;
    }

    // Validate the message
    if (!validateMessage(message)) {
        LOG("Received invalid message format");
        return;
    }

    // Process the message
    processMessage(message);
}

void NetworkManager::onConnectionChanged(bool connected) {
    bool wasConnected = connected_.load();
    connected_.store(connected);

    if (connected && !wasConnected) {
        LOG("NetworkManager connected to server");
    }
    else if (!connected && wasConnected) {
        LOG("NetworkManager disconnected from server");
    }
}

bool NetworkManager::validateMessage(const json& message) {
    // Basic validation - ensure message has required fields
    if (!message.is_object()) {
        LOG("Message is not a JSON object");
        return false;
    }

    // Check for required fields
    if (!message.contains("type")) {
        LOG("Message missing 'type' field");
        return false;
    }

    std::string messageType = message["type"];

    // Validate based on message type
    if (messageType == "lobby_action") {
        if (!message.contains("action")) {
            LOG("lobby_action message missing 'action' field");
            return false;
        }

        std::string action = message["action"];
        if (action == "join") {
            if (!message.contains("lobbyName") || !message.contains("password")) {
                LOG("join action missing required fields (lobbyName, password)");
                return false;
            }
        }
    }
    else if (messageType == "auth_response") {
        if (!message.contains("success")) {
            LOG("auth_response message missing 'success' field");
            return false;
        }
    }
    else if (messageType == "ping") {
        // Ping messages are always valid
        return true;
    }
    else {
        LOG("Unknown message type: {}", messageType);
        // Don't reject unknown message types, just log them
    }

    return true;
}

void NetworkManager::processMessage(const json& message) {
    // Handle special message types that don't need to go to the game thread
    std::string messageType = message["type"];

    if (messageType == "ping") {
        // Respond to ping with pong
        json pongResponse;
        pongResponse["type"] = "pong";
        if (message.contains("timestamp")) {
            pongResponse["timestamp"] = message["timestamp"];
        }
        sendMessage(pongResponse);
        return;
    }

    if (messageType == "auth_response") {
        bool success = message["success"];
        if (success) {
            LOG("Authentication successful");
        }
        else {
            LOG("Authentication failed: {}", message.value("error", "Unknown error"));
        }
        // Auth responses can still be queued for the game thread to handle
    }

    // Queue the message for the game thread
    if (!messageQueue_.push(message)) {
        LOG("Message queue full, dropping message");
    }
    else {
        DEBUGLOG("Queued message for game thread: {}", message.dump());
    }
}