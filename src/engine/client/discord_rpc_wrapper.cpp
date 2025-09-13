#include "core/stdafx.h"

#ifndef DEDICATED

#include "discord_rpc_wrapper.h"
#include "discord.h"
#include <memory>
#include <string>
#include <cstring>

// Discord Game SDK wrapper implementation
class DiscordRPCImpl {
private:
    std::unique_ptr<discord::Core> core;
    DiscordEventHandlers handlers;
    std::string applicationId;
    bool initialized = false;
    bool connected = false;
    
    // Store user info for callback
    std::string userId;
    std::string username;
    std::string discriminator;
    std::string avatar;

public:
    void Initialize(const char* appId, DiscordEventHandlers* eventHandlers) {
        if (initialized) return;
        
        // Validate Application ID
        if (!appId || strlen(appId) == 0 || strlen(appId) > 32) {
            return; // Invalid app ID
        }
        
        // Validate that appId contains only digits (Discord App IDs are numeric)
        for (const char* p = appId; *p; ++p) {
            if (*p < '0' || *p > '9') {
                return; // Invalid character in app ID
            }
        }
        
        applicationId = appId;
        if (eventHandlers) {
            handlers = *eventHandlers;
        }

        // Create Discord Core with validated ID
        discord::Core* corePtr = nullptr;
        uint64_t appIdNum = 0;
        try {
            appIdNum = std::stoull(applicationId);
        } catch (...) {
            return; // Failed to convert app ID
        }
        
        auto result = discord::Core::Create(appIdNum, DiscordCreateFlags_Default, &corePtr);
        if (result != discord::Result::Ok) {
            // Log the error for debugging
            return; // Failed to create core
        }
        core.reset(corePtr);

        // Set up event handlers
        core->SetLogHook(discord::LogLevel::Debug, [](discord::LogLevel level, const char* message) {
            // Optional: log Discord SDK messages
        });

        // Set up user manager events
        core->UserManager().OnCurrentUserUpdate.Connect([this]() {
            if (handlers.ready) {
                discord::User user;
                auto userResult = core->UserManager().GetCurrentUser(&user);
                if (userResult == discord::Result::Ok) {
                    // Store user info in member variables to ensure they persist
                    userId = std::to_string(user.GetId());
                    username = user.GetUsername();
                    discriminator = user.GetDiscriminator();
                    avatar = user.GetAvatar();
                    
                    DiscordRPCUser discordUser;
                    discordUser.userId = userId.c_str();
                    discordUser.username = username.c_str();
                    discordUser.discriminator = discriminator.c_str();
                    discordUser.avatar = avatar.c_str();
                    
                    connected = true;
                    handlers.ready(&discordUser);
                }
            }
        });

        initialized = true;
    }

    void Shutdown() {
        if (core) {
            core.reset();
        }
        initialized = false;
        connected = false;
    }

    void RunCallbacks() {
        if (core) {
            core->RunCallbacks();
        }
    }

    void UpdatePresence(const DiscordRichPresence* presence) {
        if (!core || !presence) return;

        discord::Activity activity{};
        
        // Validate and sanitize state field (max 128 bytes per Discord spec)
        if (presence->state) {
            size_t stateLen = strlen(presence->state);
            if (stateLen > 0 && stateLen <= 128) {
                activity.SetState(presence->state);
            }
        }
        
        // Validate and sanitize details field (max 128 bytes per Discord spec)
        if (presence->details) {
            size_t detailsLen = strlen(presence->details);
            if (detailsLen > 0 && detailsLen <= 128) {
                activity.SetDetails(presence->details);
            }
        }
        
        if (presence->startTimestamp > 0) {
            activity.GetTimestamps().SetStart(presence->startTimestamp);
        }
        
        // Validate and sanitize image fields
        if (presence->largeImageKey) {
            size_t largeKeyLen = strlen(presence->largeImageKey);
            if (largeKeyLen > 0 && largeKeyLen <= 32) { // Discord spec: max 32 bytes
                activity.GetAssets().SetLargeImage(presence->largeImageKey);
                
                if (presence->largeImageText) {
                    size_t largeTextLen = strlen(presence->largeImageText);
                    if (largeTextLen > 0 && largeTextLen <= 128) { // Discord spec: max 128 bytes
                        activity.GetAssets().SetLargeText(presence->largeImageText);
                    }
                }
            }
            
            if (presence->smallImageKey) {
                size_t smallKeyLen = strlen(presence->smallImageKey);
                if (smallKeyLen > 0 && smallKeyLen <= 32) { // Discord spec: max 32 bytes
                    activity.GetAssets().SetSmallImage(presence->smallImageKey);
                    
                    if (presence->smallImageText) {
                        size_t smallTextLen = strlen(presence->smallImageText);
                        if (smallTextLen > 0 && smallTextLen <= 128) { // Discord spec: max 128 bytes
                            activity.GetAssets().SetSmallText(presence->smallImageText);
                        }
                    }
                }
            }
        }
        
        // Validate party information to prevent integer overflow
        if (presence->partySize >= 0 && presence->partyMax >= 0 && 
            presence->partySize <= presence->partyMax && 
            presence->partyMax <= 10000) { // Reasonable upper limit
            activity.GetParty().GetSize().SetCurrentSize(presence->partySize);
            activity.GetParty().GetSize().SetMaxSize(presence->partyMax);
        }

        core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
            // Activity update callback - could log result if needed
        });
    }

    void ClearPresence() {
        if (core) {
            core->ActivityManager().ClearActivity([](discord::Result result) {
                // Clear activity callback
            });
        }
    }

    bool IsConnected() const {
        return connected;
    }
};

static DiscordRPCImpl* g_discordRPC = nullptr;

extern "C" {

void Discord_Initialize(const char* applicationId, DiscordEventHandlers* handlers, int autoRegister, const char* optionalSteamId) {
    if (!g_discordRPC) {
        g_discordRPC = new DiscordRPCImpl();
    }
    g_discordRPC->Initialize(applicationId, handlers);
}

void Discord_Shutdown(void) {
    if (g_discordRPC) {
        g_discordRPC->Shutdown();
        delete g_discordRPC;
        g_discordRPC = nullptr;
    }
}

void Discord_RunCallbacks(void) {
    if (g_discordRPC) {
        g_discordRPC->RunCallbacks();
    }
}

void Discord_UpdatePresence(const DiscordRichPresence* presence) {
    if (g_discordRPC) {
        g_discordRPC->UpdatePresence(presence);
    }
}

void Discord_ClearPresence(void) {
    if (g_discordRPC) {
        g_discordRPC->ClearPresence();
    }
}

void Discord_Respond(const char* userid, int reply) {
    // Not implemented in this version
}

void Discord_UpdateHandlers(DiscordEventHandlers* handlers) {
    // Not implemented in this version
}

}

#endif // !DEDICATED
