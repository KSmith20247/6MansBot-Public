#pragma once

#include "GuiBase.h"
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <thread>
#include <string>
#include "version.h"
#include "NetworkManager.h"

constexpr auto plugin_version = stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH) "." stringify(VERSION_BUILD);

class SixMansPlugin : public BakkesMod::Plugin::BakkesModPlugin, public SettingsWindowBase {
private:
    std::unique_ptr<NetworkManager> networkManager_;
    bool networkInitialized_ = false;

    // Message processing on game thread
    void messageDispatcher();
    void handleLobbyMessage(const std::string& message);

public:
    void onLoad();
    void onUnload();
    void CreatePrivateLobby();
    void getMap();
    void JoinPrivateLobby();
    void InitializeNetwork();
    void VerifyToken(const std::string& token);
    void RenderSettings() override;

    // Game tick handler for processing network messages
    void onTick(std::string eventName);
};

using json = nlohmann::json;