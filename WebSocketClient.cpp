#include "pch.h"
#include "WebSocketClient.h"
#include "logging.h"
#include "Config.h"
#include <regex>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <chrono>


WebSocketClient::WebSocketClient()
    : running_(false), connected_(false), context_(nullptr), websocket_(nullptr),
    serverPort_(443), useSSL_(false), writePending_(false) { //use ssl later

    // Initialize protocols array
    protocols_[0] = {
        "sixmans-protocol",
        WebSocketClient::websocketCallback,
        sizeof(ConnectionData*),
        1024,
        0, nullptr, 0
    };
    protocols_[1] = { nullptr, nullptr, 0, 0, 0, nullptr, 0 }; // null terminator
}

WebSocketClient::~WebSocketClient() {
    stop();
}

bool WebSocketClient::start(const std::string& url, const std::string& token,
    MessageCallback messageCallback,
    ConnectionCallback connectionCallback) {
    if (running_.load()) {
        LOG("WebSocket client already running");
        return false;
    }

    if (!parseUrl(url, serverHost_, serverPort_, serverPath_, useSSL_)) {
        LOG("Failed to parse WebSocket URL: {}", url);
        return false;
    }

    // Create connection data
    connectionData_ = std::make_unique<ConnectionData>();
    connectionData_->client = this;
    connectionData_->token = token;
    connectionData_->messageCallback = messageCallback;
    connectionData_->connectionCallback = connectionCallback;
    connectionData_->connectionEstablished = false;
    connectionData_->shouldReconnect.store(true);

    running_.store(true);

    // Start the event loop thread
    eventThread_ = std::make_unique<std::thread>(&WebSocketClient::runEventLoop, this);

    LOG("WebSocket client started for URL: {}", url);
    return true;
}

void WebSocketClient::stop() {
    if (!running_.load()) {
        return;
    }

    LOG("Stopping WebSocket client");
    running_.store(false);
    connected_.store(false);

    if (connectionData_) {
        connectionData_->shouldReconnect.store(false);
    }

    // Cancel the context to wake up the event loop
    if (context_) {
        lws_cancel_service(context_);
    }

    // Wait for thread to finish
    if (eventThread_ && eventThread_->joinable()) {
        eventThread_->join();
    }

    // Cleanup
    if (context_) {
        lws_context_destroy(context_);
        context_ = nullptr;
    }

    connectionData_.reset();
    eventThread_.reset();

    LOG("WebSocket client stopped");
}

bool WebSocketClient::sendMessage(const json& message) {
    if (!connected_.load()) {
        LOG("Cannot send message: WebSocket not connected");
        return false;
    }

    std::lock_guard<std::mutex> lock(writeMutex_);
    pendingWrite_ = message.dump();
    writePending_ = true;

    // Wake up the event loop
    if (context_) {
        lws_callback_on_writable(websocket_);
        lws_cancel_service(context_);
    }

    return true;
}

bool WebSocketClient::isConnected() const {
    return connected_.load();
}

void WebSocketClient::runEventLoop() {
    // The lws_context should be created only once.
    struct lws_context_creation_info info = {};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols_;
    info.gid = -1;
    info.uid = -1;
    if (useSSL_) {
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    }

    context_ = lws_create_context(&info);
    if (!context_) {
        LOG("Failed to create WebSocket context");
        return;
    }

    // This is the main service loop. It will run as long as the client is running.
    while (running_.load()) {
        // If we are not connected and not trying to reconnect, start a connection attempt.
        if (!connected_.load() && !connectionData_->shouldReconnect.load()) {
            connectionData_->shouldReconnect.store(true); // Mark that we are attempting a connect/reconnect

            struct lws_client_connect_info connectInfo = {};
            connectInfo.context = context_;
            connectInfo.address = serverHost_.c_str();
            connectInfo.port = serverPort_;
            connectInfo.path = serverPath_.c_str();
            connectInfo.host = serverHost_.c_str();
            connectInfo.origin = serverHost_.c_str();
            connectInfo.protocol = protocols_[0].name;
            connectInfo.userdata = connectionData_.get();
            if (useSSL_) {
                connectInfo.ssl_connection = LCCSCF_USE_SSL;
            }

            LOG("Attempting to connect to {}:{}{}", serverHost_, serverPort_, serverPath_);
            websocket_ = lws_client_connect_via_info(&connectInfo);
            if (!websocket_) {
                LOG("Failed to start WebSocket connection attempt. Retrying in {}ms", SixMansConfig::RECONNECT_DELAY_MS);
                std::this_thread::sleep_for(std::chrono::milliseconds(SixMansConfig::RECONNECT_DELAY_MS));
                continue; // Loop again to retry
            }
        }

        // lws_service is the heart of the event loop.
        // We give it a 50ms timeout to allow it to wait for network events
        // without burning 100% CPU.
        int n = lws_service(context_, 50);

        // If a reconnect has been scheduled (e.g., from a disconnect), sleep before looping again
        if (connected_.load() == false && connectionData_->shouldReconnect.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(SixMansConfig::RECONNECT_DELAY_MS));
        }
    }

    lws_context_destroy(context_);
    context_ = nullptr;
}


bool WebSocketClient::parseUrl(const std::string& url, std::string& host, int& port,
    std::string& path, bool& useSSL) {
    // Simple regex to parse WebSocket URLs
    std::regex urlRegex(R"(^(wss?)://([^:/]+)(?::(\d+))?(/.*)?$)");
    std::smatch matches;

    if (!std::regex_match(url, matches, urlRegex)) {
        return false;
    }

    std::string protocol = matches[1].str();
    useSSL = (protocol == "wss");
    host = matches[2].str();

    if (matches[3].matched) {
        port = std::stoi(matches[3].str());
    }
    else {
        port = useSSL ? 443 : 80;
    }

    if (matches[4].matched) {
        path = matches[4].str();
    }
    else {
        path = "/";
    }

    return true;
}

void WebSocketClient::handleMessage(const std::string& message) {
    try {
        json parsedMessage = json::parse(message);
        if (connectionData_ && connectionData_->messageCallback) {
            connectionData_->messageCallback(parsedMessage);
        }
    }
    catch (const json::exception& e) {
        LOG("Failed to parse JSON message: {}", e.what());
    }
}

int WebSocketClient::websocketCallback(struct lws* wsi, enum lws_callback_reasons reason,
    void* user, void* in, size_t len) {

    // Safely get the ConnectionData pointer using the recommended lws function.
    ConnectionData* connectionData = static_cast<ConnectionData*>(lws_get_opaque_user_data(wsi));

    // The client pointer is inside ConnectionData
    WebSocketClient* client = nullptr;
    if (connectionData) {
        client = connectionData->client;
    }

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        if (client && connectionData) {
            LOG("WebSocket connection established");
            client->connected_.store(true);
            client->websocket_ = wsi; // Store the websocket instance

            // Copy the pointer into the session-specific user data for future callbacks
            *(static_cast<ConnectionData**>(user)) = connectionData;

            // CORRECTED: Access callback through connectionData
            if (connectionData->connectionCallback) {
                connectionData->connectionCallback(true);
            }

            // Immediately request a write callback to send the auth token
            lws_callback_on_writable(wsi);
        }
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (client && connectionData && in && len > 0) {
            // Use the callback stored in connectionData
            if (connectionData->messageCallback) {
                try {
                    json received_json = json::parse(static_cast<const char*>(in), static_cast<const char*>(in) + len);
                    connectionData->messageCallback(received_json);
                }
                catch (const json::parse_error& e) {
                    LOG("JSON parse error: {}", e.what());
                }
            }
        }
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
        if (client && connectionData) {
            // Priority 1: Send the initial auth token message from the buffer
            if (!connectionData->writeBuffer.empty()) {
                size_t msgLen = connectionData->writeBuffer.length();
                // LWS_PRE is padding libwebsockets needs before your data
                unsigned char* buffer = new unsigned char[LWS_PRE + msgLen];
                memcpy(buffer + LWS_PRE, connectionData->writeBuffer.c_str(), msgLen);
                int result = lws_write(wsi, buffer + LWS_PRE, msgLen, LWS_WRITE_TEXT);
                delete[] buffer;

                if (result < 0) {
                    LOG("Failed to send authentication message");
                    return -1;
                }
                connectionData->writeBuffer.clear();
            }
            // Priority 2: Send any pending message from the sendMessage function
            else if (client->writePending_) {
                std::lock_guard<std::mutex> lock(client->writeMutex_);
                size_t msgLen = client->pendingWrite_.length();
                unsigned char* buffer = new unsigned char[LWS_PRE + msgLen];
                memcpy(buffer + LWS_PRE, client->pendingWrite_.c_str(), msgLen);
                int result = lws_write(wsi, buffer + LWS_PRE, msgLen, LWS_WRITE_TEXT);
                delete[] buffer;

                if (result < 0) {
                    LOG("Failed to send WebSocket message");
                    return -1;
                }
                client->pendingWrite_.clear();
                client->writePending_ = false;
            }
        }
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        LOG("WebSocket connection error: {}", in ? std::string(static_cast<const char*>(in), len) : "Unknown error");
        if (client && connectionData) {
            client->connected_.store(false);
            // CORRECTED: Access callback through connectionData
            if (connectionData->connectionCallback) {
                connectionData->connectionCallback(false);
            }
            client->scheduleReconnect();
        }
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        LOG("WebSocket connection closed");
        if (client && connectionData) {
            client->connected_.store(false);
            // CORRECTED: Access callback through connectionData
            if (connectionData->connectionCallback) {
                connectionData->connectionCallback(false);
            }
            client->scheduleReconnect();
        }
        break;

    default:
        break;
    }

    return 0;
}

void WebSocketClient::scheduleReconnect() {
    // This function is now much simpler.
    // It is called from the callback to signal that the loop should attempt a new connection.
    if (connectionData_) {
        // Setting this flag will cause the main loop to re-initiate connection.
        connectionData_->shouldReconnect.store(true);
    }
}