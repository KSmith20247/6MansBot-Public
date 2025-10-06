//SixMansPlugin.cpp
#include "pch.h"
#include "SixMansPlugin.h"
#include "logging.h"
#include "Config.h"
#include <vector>
#include <random>  
#include <iostream>
#include <nlohmann/json.hpp>

BAKKESMOD_PLUGIN(SixMansPlugin, "The official Bakkesmod plugin for 6mans", "1.0", PLUGINTYPE_FREEPLAY) // type freeplay doesn't matter, all plugintypes now work everywhere. 

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

void SixMansPlugin::onLoad() {
    _globalCvarManager = cvarManager;
    LOG("Plugin loaded!");

    // !! Enable debug logging by setting DEBUG_LOG = true in logging.h !!
    // DEBUGLOG("SixMansPlugin debug mode enabled");

    // Register the pluginEnabled CVar (default: "1" meaning enabled)
    CVarWrapper pluginEnabledCvar = cvarManager->registerCvar(
        "pluginEnabled",
        "1",
        "Enable/Disable the plugin. 0 = false, 1 = true",
        true
    );
    pluginEnabledCvar.setValue(pluginEnabledCvar.getIntValue());

    // Register the verificationToken CVar (default: empty string)
    CVarWrapper verificationTokenCvar = cvarManager->registerCvar(
        "verificationToken",
        "",
        "Verification token",
        true
    );

    // Register the autoJoin CVar (default: "0")
    CVarWrapper autoJoinCvar = cvarManager->registerCvar(
        "autoJoin",
        "0",
        "If checked, will automatically try to join every 6mans lobby",
        true, true, 0, true, 1
    );
    autoJoinCvar.setValue(autoJoinCvar.getIntValue());

    // Register the autoCreate CVar (default: "0")
    CVarWrapper autoCreateCvar = cvarManager->registerCvar(
        "autoCreate",
        "0",
        "If checked, will automatically create a 6mans lobby",
        true, true, 0, true, 1
    );
    autoCreateCvar.setValue(autoCreateCvar.getIntValue());

    // Register a notifier for joining a private lobby
    cvarManager->registerNotifier("joinprivate", [this](std::vector<std::string> params) {
        this->JoinPrivateLobby();
        }, "Join a private lobby with a preset name and password", PERMISSION_ALL);

    // Hook the game tick event to process network messages
    gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.InitGame",
        std::bind(&SixMansPlugin::onTick, this, std::placeholders::_1));

    // Also hook car spawn to ensure we're processing messages during gameplay
    gameWrapper->HookEvent("Function TAGame.Car_TA.SetVehicleInput",
        std::bind(&SixMansPlugin::onTick, this, std::placeholders::_1));

    // Initialize network with delay to ensure game is fully loaded
    gameWrapper->SetTimeout([this](GameWrapper*) {
        InitializeNetwork();
        }, 3.0f); // Delay network initialization by 3 seconds after game loads
}

void SixMansPlugin::InitializeNetwork() {
    LOG("InitializeNetwork called! Attempting to connect to {}", SixMansConfig::buildWebSocketUrl());
    if (networkInitialized_) {
        LOG("Network already initialized");
        return;
    }

    // Get verification token
    CVarWrapper verificationTokenCvar = cvarManager->getCvar("verificationToken");
    std::string token = verificationTokenCvar.getStringValue();

    if (token.empty()) {
        LOG("No verification token provided, network initialization skipped");
        return;
    }

    // Create network manager
    networkManager_ = std::make_unique<NetworkManager>();

    // Build WebSocket URL
    std::string wsUrl = SixMansConfig::buildWebSocketUrl();

    // Start network manager
    if (networkManager_->start(wsUrl, token)) {
        networkInitialized_ = true;
        LOG("Network initialized successfully with URL: {}", wsUrl);
    }
    else {
        LOG("Failed to initialize network");
        networkManager_.reset();
    }
}

void SixMansPlugin::onTick(std::string eventName) {
    // Process network messages on the game thread
    messageDispatcher();
}

void SixMansPlugin::messageDispatcher() {
    if (!networkManager_ || !networkInitialized_) {
        return;
    }

    // Process up to 5 messages per tick to avoid blocking the game thread
    for (int i = 0; i < 5; ++i) {
        auto messageOpt = networkManager_->getNextMessage();
        if (!messageOpt.has_value()) {
            break; // No more messages
        }

        try {
            json message = messageOpt.value();
            std::string messageStr = message.dump();
            DEBUGLOG("Processing message on game thread: {}", messageStr);

            // Call the existing handleLobbyMessage method
            handleLobbyMessage(messageStr);
        }
        catch (const std::exception& e) {
            LOG("Error processing message in dispatcher: {}", e.what());
        }
    }
}

void SixMansPlugin::handleLobbyMessage(const std::string& message) {
    // Log the received message
    LOG("Processing lobby message: {}", message);

    try {
        // Parse the JSON message
        json messageJson = json::parse(message);

        // Handle different message types
        if (messageJson.contains("type")) {
            std::string messageType = messageJson["type"];

            if (messageType == "lobby_action" && messageJson.contains("action")) {
                std::string action = messageJson["action"];

                if (action == "join") {
                    // Check if auto-join is enabled
                    CVarWrapper autoJoinCvar = cvarManager->getCvar("autoJoin");
                    if (autoJoinCvar.getBoolValue()) {
                        // Extract lobby details if they exist in the message
                        if (messageJson.contains("lobbyName") && messageJson.contains("password")) {
                            std::string lobbyName = messageJson["lobbyName"];
                            std::string password = messageJson["password"];

                            // Join the lobby with the provided details
                            LOG("Auto-joining lobby: {} with password: {}", lobbyName, password);
                            // TODO: Update JoinPrivateLobby to accept parameters
                            JoinPrivateLobby();
                        }
                        else {
                            LOG("Received join action but missing lobby details");
                        }
                    }
                }
                else if (action == "create") {
                    // Check if auto-create is enabled
                    CVarWrapper autoCreateCvar = cvarManager->getCvar("autoCreate");
                    if (autoCreateCvar.getBoolValue()) {
                        LOG("Auto-creating lobby");
                        CreatePrivateLobby();
                    }
                }
            }
            else if (messageType == "auth_response") {
                bool success = messageJson.value("success", false);
                if (success) {
                    LOG("Server authentication successful");
                }
                else {
                    LOG("Server authentication failed: {}", messageJson.value("error", "Unknown error"));
                }
            }
        }
    }
    catch (const json::exception& e) {
        LOG("Failed to parse message as JSON: {}", e.what());
    }
    catch (const std::exception& e) {
        LOG("Error processing message: {}", e.what());
    }
}

void SixMansPlugin::getMap() {
    gameWrapper->Execute([this](GameWrapper* gw) {
        std::string currentMap = gw->GetCurrentMap();
        cvarManager->log("Current Map: " + currentMap);
        });
}

void SixMansPlugin::CreatePrivateLobby() {
    if (!gameWrapper) {
        LOG("GameWrapper not available.");
        return;
    }

    gameWrapper->Execute([this](GameWrapper* gw) {
        // Ensure the game is not already in an online match
        if (!gw || gw->IsInOnlineGame()) {
            LOG("Already in an online match, cannot create a private match.");
            return;
        }

        // Obtain MatchmakingWrapper
        MatchmakingWrapper matchmaking = gw->GetMatchmakingWrapper();
        if (matchmaking.IsNull()) {
            LOG("MatchmakingWrapper is NULL!");
            return;
        }

        const std::vector<std::string> mapNames = {
            "Stadium_Day_P",       // DFH Stadium (Day)
            "Stadium_Foggy_P",     // DFH Stadium (Stormy)
            "Stadium_P",           // DFH Stadium (Night)
            "EuroStadium_P",       // MannField (Day)
            "EuroStadium_Night_P", // Mannfield (Night) 
            "EuroStadium_Rainy_P", // Mannfield (Stormy)
            "UtopiaStadium_P",     // Utopia Coliseum (Day)
            "UtopiaStadium_Dusk_P",// Utopia Coliseum (Dusk)
            "cs_p",                // Champions Field (Night)
            "cs_day_p",            // Champions Field (Day)
            "Park_P",              // Beckwith Park (Day)
            "Park_Night_P",        // Beckwith Park (Night)
            "Park_Rainy_P"         // Beckwith Park (Stormy)
        };

        // Random map selection
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> distr(0, mapNames.size() - 1);
        std::string randomMap = mapNames[distr(gen)];

        LOG("Selected map: " + randomMap);

        // Configure match settings
        CustomMatchSettings matchSettings;
        matchSettings.MapName = randomMap;
        matchSettings.ServerName = "smtty";
        matchSettings.Password = "secure123";
        matchSettings.GameMode = 0;
        matchSettings.GameTags = "BotsNone,PlayerCount3";

        // Set team settings
        matchSettings.BlueTeamSettings.Name = "Team 1";
        matchSettings.OrangeTeamSettings.Name = "Team 2";

        // Pass the region
        Region region = Region::USE;

        LOG("Creating match on USE server...");
        matchmaking.CreatePrivateMatch(region, static_cast<int>(PlaylistIds::PrivateMatch), matchSettings);
        });
}

void SixMansPlugin::JoinPrivateLobby() {
    static bool joinAttemptInProgress = false;
    if (joinAttemptInProgress) {
        LOG("JoinPrivateLobby already in progress, ignoring duplicate call.");
        return;
    }
    joinAttemptInProgress = true;

    std::string lobbyName = "c1721";
    std::string lobbyPassword = "ky1w";

    gameWrapper->Execute([this, lobbyName, lobbyPassword](GameWrapper* gw) {
        // Ensure game is not already in an online match
        if (!gw || gw->IsInOnlineGame()) {
            LOG("GameWrapper not available or already in an online match.");
            joinAttemptInProgress = false;
            return;
        }

        // Get MatchmakingWrapper dynamically to avoid crashes
        MatchmakingWrapper matchmaking = gw->GetMatchmakingWrapper();
        if (!matchmaking.IsNull()) {
            matchmaking.JoinPrivateMatch(lobbyName, lobbyPassword);
            LOG("Attempting to join private match: {} with password: {}", lobbyName, lobbyPassword);
        }
        else {
            LOG("Failed to get MatchmakingWrapper.");
            joinAttemptInProgress = false;
            return;
        }

        // Reset the flag after a short delay using GameWrapper context
        gw->SetTimeout([this](GameWrapper*) {
            joinAttemptInProgress = false;
            }, 1.0f); // Delay in seconds
        });
}


void SixMansPlugin::onUnload() {
    // Clean up network connection
    if (networkManager_) {
        networkManager_->stop();
        networkManager_.reset();
    }
    networkInitialized_ = false;

    LOG("Plugin unloaded");
}