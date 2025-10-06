//SixMansGui.cpp
#include "pch.h"
#include "SixMansPlugin.h"
#include "config.h"


void SixMansPlugin::VerifyToken(const std::string& token) {
    if (token.empty()) {
        LOG("Cannot verify empty token");
        return;
    }

    // If network is already initialized, restart it with the new token
    if (networkInitialized_ && networkManager_) {
        LOG("Restarting network with new token");
        networkManager_->stop();
        networkManager_.reset();
        networkInitialized_ = false;
    }

    // Initialize network with new token
    gameWrapper->SetTimeout([this](GameWrapper*) {
        InitializeNetwork();
        }, 1.0f);

    LOG("Token verification initiated");
}

void SixMansPlugin::RenderSettings() {
    ImGui::Spacing();

    // Plugin Enabled Checkbox
    CVarWrapper pluginEnabledCvar = cvarManager->getCvar("pluginEnabled");
    bool pluginEnabled = pluginEnabledCvar.getBoolValue();
    if (ImGui::Checkbox("Enable Plugin", &pluginEnabled)) {
        pluginEnabledCvar.setValue(static_cast<int>(pluginEnabled));
        cvarManager->executeCommand("writeconfig", false);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Verification Token Field
    CVarWrapper verificationTokenCvar = cvarManager->getCvar("verificationToken");
    std::string currentToken = verificationTokenCvar.getStringValue();
    // Create a local buffer large enough for the token
    char verificationToken[128];
    strncpy(verificationToken, currentToken.c_str(), sizeof(verificationToken) - 1);
    verificationToken[sizeof(verificationToken) - 1] = '\0';

    ImGui::TextUnformatted("Verification Token");
    if (ImGui::InputText("##VerificationToken", verificationToken, IM_ARRAYSIZE(verificationToken))) {
        verificationTokenCvar.setValue(std::string(verificationToken));
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        verificationTokenCvar.setValue(std::string(verificationToken));
        VerifyToken(std::string(verificationToken));
        cvarManager->executeCommand("writeconfig", false);
    }

    // Status Display - Enhanced to show network connection status
    bool hasToken = (strlen(verificationToken) > 0);
    bool isConnected = networkManager_ && networkManager_->isConnected();

    ImVec4 statusColor;
    std::string statusText;

    if (!hasToken) {
        statusColor = ImVec4(1, 0, 0, 1); // Red
        statusText = "No Token";
    }
    else if (!networkInitialized_) {
        statusColor = ImVec4(1, 1, 0, 1); // Yellow
        statusText = "Initializing...";
    }
    else if (isConnected) {
        statusColor = ImVec4(0, 1, 0, 1); // Green
        statusText = "Connected";
    }
    else {
        statusColor = ImVec4(1, 0.5, 0, 1); // Orange
        statusText = "Disconnected";
    }

    ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
    ImGui::Text("Status: %s", statusText.c_str());
    ImGui::PopStyleColor();

    // Show queue size if network is active
    if (networkManager_) {
        size_t queueSize = networkManager_->getQueueSize();
        if (queueSize > 0) {
            ImGui::SameLine();
            ImGui::Text("(Queue: %zu)", queueSize);
        }
    }

    ImGui::TextUnformatted("Get your token by typing !bmverify in the #bakkes-verify channel in the RL6Mans discord.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Network Settings Section
    ImGui::TextUnformatted("Network Settings");

    // Show connection info if connected
    if (isConnected) {
        ImGui::Text("Connected to: %s", SixMansConfig::buildWebSocketUrl().c_str());
    }

    // Manual reconnect button
    if (!isConnected && hasToken) {
        if (ImGui::Button("Reconnect")) {
            if (networkManager_) {
                networkManager_->stop();
                networkManager_.reset();
                networkInitialized_ = false;
            }
            gameWrapper->SetTimeout([this](GameWrapper*) {
                InitializeNetwork();
                }, 0.5f);
        }
    }

    // Clear message queue button (useful for debugging)
    if (networkManager_ && networkManager_->getQueueSize() > 0) {
        ImGui::SameLine();
        if (ImGui::Button("Clear Queue")) {
            networkManager_->clearQueue();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextUnformatted("Automatic Lobby Settings");
    // Auto Join Checkbox
    CVarWrapper autoJoinCvar = cvarManager->getCvar("autoJoin");
    bool autoJoinEnabled = autoJoinCvar.getBoolValue();
    if (ImGui::Checkbox("Auto Join 6Mans Lobbies", &autoJoinEnabled)) {
        autoJoinCvar.setValue(static_cast<int>(autoJoinEnabled));
        cvarManager->executeCommand("writeconfig", false);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("If checked, the plugin will automatically join every 6mans lobby when instructed by the server.");
    }

    // Auto Create Checkbox
    CVarWrapper autoCreateCvar = cvarManager->getCvar("autoCreate");
    bool autoCreateEnabled = autoCreateCvar.getBoolValue();
    if (ImGui::Checkbox("Auto Create 6Mans Lobbies", &autoCreateEnabled)) {
        autoCreateCvar.setValue(static_cast<int>(autoCreateEnabled));
        cvarManager->executeCommand("writeconfig", false);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("If checked, the plugin will automatically create every 6mans lobby you're assigned to create.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Create 6mans Lobby Button
    ImGui::TextUnformatted("Manual Lobby Functions");
    if (ImGui::Button("Create 6mans Lobby")) {
        this->CreatePrivateLobby();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Creates a standard private match for 6mans using the generated credentials.");
    }

    ImGui::SameLine();  // Place the next button on the same line

    // Join 6mans Lobby Button
    if (ImGui::Button("Join 6mans Lobby")) {
        this->JoinPrivateLobby();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("If this fails, it may not be up yet!");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Debug Section (only show if connected)
    if (isConnected) {
        ImGui::TextUnformatted("Debug");
        if (ImGui::Button("Send Test Message")) {
            if (networkManager_) {
                json testMessage;
                testMessage["type"] = "test";
                testMessage["message"] = "Hello from BakkesMod plugin!";
                testMessage["timestamp"] = std::time(nullptr);
                networkManager_->sendMessage(testMessage);
                LOG("Sent test message to server");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sends a test message to the server for debugging purposes.");
        }
    }
}