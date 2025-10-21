#include <string>
#include "steam_integration.h"
#include "tier0/dbg.h"
#include "tier0/commandline.h"
#include "ebisusdk/EbisuSDK.h"

// C-style interface to pure Steam wrapper (no type conflicts)
extern "C" {
    int Steam_InitAPI();
    void Steam_ShutdownAPI();
    void Steam_RunCallbacksAPI();
    int Steam_GetUsernameC(char* outBuffer, int bufferSize);
    unsigned long long Steam_GetUserIDC();
    int Steam_GetAuthTicketHex(char* outBuffer, int bufferSize);
    void Steam_CancelAuthTicket();
    int Steam_IsOverlayEnabled();
    void Steam_SetOverlayNotificationPosition(int position);
}

static bool g_SteamInitialized = false;
static bool g_SteamShuttingDown = false;
static bool g_SteamOverlayActive = false;
static bool g_SteamOverlayTransitioning = false;
static float g_LastCallbackTime = 0.0f;
static float g_OverlayTransitionTime = 0.0f;
static float g_OverlayActiveStartTime = 0.0f;
static bool g_OverlayTimeoutProtection = false;

// ConVars for overlay control and debugging
#include "tier1/convar.h"
#include "tier0/commandline.h"
static ConVar steam_overlay_pos("steam_overlay_pos", "1", FCVAR_ARCHIVE, "Steam overlay notification position (0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right)");
ConVar steam_debug("steam_debug", "0", FCVAR_ARCHIVE, "Enable general Steam API debug logging (0=disabled, 1=enabled)"); // Non-static so other files can access it
ConVar steam_debug_auth("steam_debug_auth", "0", FCVAR_ARCHIVE, "Enable detailed Steam authentication debug logging (0=disabled, 1=enabled)"); // Non-static so other files can access it
static ConVar steam_offline_username("steam_offline_username", "unnamed", FCVAR_ARCHIVE, "Username to use when Steam is unavailable in offline mode");
static ConVar steam_offline_userid("steam_offline_userid", "7656119800000000", FCVAR_ARCHIVE, "Steam ID to use when Steam is unavailable in offline mode (must be valid Steam ID format)");
static ConVar steam_enabled("steam_enabled", "0", FCVAR_RELEASE, "Shows whether Steam is enabled and available (1=enabled, 0=disabled/offline)");

// Update steam_enabled ConVar based on current status
static void UpdateSteamEnabledStatus()
{
    bool isEnabled = false;
    
#ifdef USE_STEAMWORKS
    // Steam is enabled if Steamworks is compiled in and we're not in offline mode and Steam is initialized
    if (!Steam_IsOfflineMode() && g_SteamInitialized)
    {
        isEnabled = true;
    }
#endif
    
    steam_enabled.SetValue(isEnabled ? "1" : "0");
}

// Check if we should use offline mode for Steam
bool Steam_IsOfflineMode()
{
    // Force offline mode if Steamworks is not available
#ifndef USE_STEAMWORKS
    return true;
#else
    // Check for explicit offline mode parameter
    return CommandLine()->CheckParm("-offline") != nullptr;
#endif
}

bool Steam_EnsureInitialized()
{
    if (g_SteamShuttingDown)
    {
        return false; // Don't initialize during shutdown
    }
    
#ifdef USE_STEAMWORKS
    // Steamworks is available - proceed with initialization
    if (g_SteamInitialized)
    {
        return true;
    }

    if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Attempting to initialize Steam API...\n");
    if (Steam_InitAPI())
    {
        g_SteamInitialized = true;
        UpdateSteamEnabledStatus();
        if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Steam API initialized successfully\n");

        // Configure overlay settings to reduce crash risk
        Steam_SetOverlayNotificationPosition(steam_overlay_pos.GetInt());

        return true;
    }

    // Automatically enable offline mode and Steam mode when Steam fails to initialize
    if (!CommandLine()->CheckParm("-offline"))
    {
        CommandLine()->AppendParm("-offline", "");
    }
    if (!CommandLine()->CheckParm("-noorigin"))
    {
        CommandLine()->AppendParm("-noorigin", "");
    }

    UpdateSteamEnabledStatus(); // Update status after failed init
    return false;
#else
    // If Steamworks is not available, we're always in offline mode
    Msg(eDLL_T::STEAM, "Steamworks not available - using offline mode\n");

    // Add offline and Steam mode parameters if not already present
    if (!CommandLine()->CheckParm("-offline"))
    {
        CommandLine()->AppendParm("-offline", "");
    }
    if (!CommandLine()->CheckParm("-noorigin"))
    {
        CommandLine()->AppendParm("-noorigin", "");
    }

    UpdateSteamEnabledStatus(); // Update status for offline mode
    return false; // Return false to indicate no Steam API available
#endif
}

__declspec(dllexport) bool Steam_GetAuthSessionTicketBase64(std::string& outTicket)
{
    if (g_SteamShuttingDown)
    {
        return false; // Don't get tickets during shutdown
    }

    if (steam_debug_auth.GetBool()) Msg(eDLL_T::STEAM, "Attempting to get auth session ticket...\n");

    if (!Steam_EnsureInitialized())
    {
        if (steam_debug_auth.GetBool()) Msg(eDLL_T::STEAM, "Steam not initialized, cannot get ticket\n");
        return false;
    }

    char ticketBuffer[8192]; // Large enough for hex-encoded ticket
    int len = Steam_GetAuthTicketHex(ticketBuffer, sizeof(ticketBuffer));

    if (len > 0)
    {
        outTicket.assign(ticketBuffer, len);
        return true;
    }

    Msg(eDLL_T::STEAM, "Failed to get auth ticket\n"); // Keep error messages
    return false;
}

void Steam_CancelCurrentAuthTicket()
{
    if (g_SteamShuttingDown)
    {
        return; // Don't cancel tickets during shutdown
    }
    
#ifdef USE_STEAMWORKS
    if (!Steam_EnsureInitialized())
    {
        return;
    }
    
    Steam_CancelAuthTicket();
    if (steam_debug_auth.GetBool()) Msg(eDLL_T::STEAM, "Cancelled current auth ticket\n");
#endif
}

__declspec(dllexport) bool Steam_GetUsername(std::string& outUsername)
{
    if (g_SteamShuttingDown)
    {
        return false; // Don't get username during shutdown
    }

    // Check if we're in offline mode first
    if (Steam_IsOfflineMode())
    {
        outUsername = steam_offline_username.GetString();
        if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Using offline username: %s\n", outUsername.c_str());
        return true;
    }

    if (!Steam_EnsureInitialized())
    {
        // Fallback to offline username if Steam init fails
        outUsername = steam_offline_username.GetString();
        if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Steam not initialized, using offline username: %s\n", outUsername.c_str());
        return true;
    }

    char usernameBuffer[256];
    int len = Steam_GetUsernameC(usernameBuffer, sizeof(usernameBuffer));

    if (len > 0)
    {
        outUsername.assign(usernameBuffer, len);
        if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Got username: %s\n", outUsername.c_str());
        return true;
    }

    // Fallback to offline username if Steam call fails
    outUsername = steam_offline_username.GetString();
    if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Failed to get Steam username, using offline fallback: %s\n", outUsername.c_str());
    return true;
}

__declspec(dllexport) uint64_t Steam_GetUserID()
{
    if (g_SteamShuttingDown)
    {
        return 0; // Don't get SteamID during shutdown
    }

    // Check if we're in offline mode first
    if (Steam_IsOfflineMode())
    {
        uint64_t offlineID = (uint64_t)strtoull(steam_offline_userid.GetString(), nullptr, 10);
        if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Using offline SteamID: %llu\n", offlineID);
        return offlineID;
    }

    if (!Steam_EnsureInitialized())
    {
        // Fallback to offline user ID if Steam init fails
        uint64_t offlineID = (uint64_t)strtoull(steam_offline_userid.GetString(), nullptr, 10);
        if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Steam not initialized, using offline SteamID: %llu\n", offlineID);
        return offlineID;
    }

    uint64_t userID = Steam_GetUserIDC();
    if (userID > 0)
    {
        if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Got SteamID: %llu\n", userID);
        return userID;
    }

    // Fallback to offline user ID if Steam call fails
    uint64_t offlineID = (uint64_t)strtoull(steam_offline_userid.GetString(), nullptr, 10);
    if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Failed to get SteamID, using offline fallback: %llu\n", offlineID);
    return offlineID;
}

void Steam_RunFrame()
{
    if (g_SteamShuttingDown || !g_SteamInitialized)
        return;
    
    // Get current time (rough approximation)
    static int frameCount = 0;
    frameCount++;
    double currentTime = frameCount * 0.016; // Assume ~60 FPS
    
    // Enhanced callback processing with always-active overlay freeze protection
    bool shouldProcessCallbacks = true;
    
    // Always use safe mode with overlay protection
    if (g_SteamOverlayTransitioning)
    {
        // During transition, skip callbacks to prevent freeze
        shouldProcessCallbacks = false;
    }
    else if (g_SteamOverlayActive)
    {
        // While overlay is open, significantly reduce callback frequency
        if (frameCount % 10 == 0) // Every 10th frame when overlay active
        {
            shouldProcessCallbacks = true;
        }
        else
        {
            shouldProcessCallbacks = false;
        }
    }
    else if (g_OverlayTimeoutProtection)
    {
        // Emergency protection mode - minimal callbacks
        if (frameCount % 20 == 0) // Every 20th frame in emergency mode
        {
            shouldProcessCallbacks = true;
        }
        else
        {
            shouldProcessCallbacks = false;
        }
    }
    else if (frameCount % 3 == 0) // Every 3rd frame when overlay inactive
    {
        shouldProcessCallbacks = true;
    }
    else
    {
        shouldProcessCallbacks = false;
    }
    
    if (shouldProcessCallbacks)
    {
        Steam_RunCallbacksAPI();
        g_LastCallbackTime = (float)currentTime;
    }
    
    // Periodically check if we need to update Steam persona name
    static double lastPersonaCheck = 0.0;
    static double checkInterval = 5.0; // Check every 5 seconds
    
    if (currentTime - lastPersonaCheck > checkInterval)
    {
        lastPersonaCheck = currentTime;
        
        // Check if persona name needs to be set (avoid during overlay)
        extern void SetSteamPersonaName();
        if (!g_SteamOverlayActive && g_PersonaName && (!g_PersonaName[0] || strcmp(g_PersonaName, "unnamed") == 0))
        {
            SetSteamPersonaName();
        }
    }
    
    // Enhanced overlay state checking with transition and timeout detection
    if (frameCount % 30 == 0) // Check twice per second for faster detection
    {
        bool overlayEnabled = Steam_IsOverlayEnabled();
        if (overlayEnabled != g_SteamOverlayActive)
        {
            // Overlay state changed - enter transition period
            g_SteamOverlayActive = overlayEnabled;
            g_SteamOverlayTransitioning = true;
            g_OverlayTransitionTime = (float)currentTime;
            
            // Track when overlay becomes active
            if (g_SteamOverlayActive)
            {
                g_OverlayActiveStartTime = (float)currentTime;
                g_OverlayTimeoutProtection = false; // Reset timeout protection
            }
            else
            {
                g_OverlayActiveStartTime = 0.0f;
                g_OverlayTimeoutProtection = false;
            }
        }
        
        // Check for overlay timeout (stuck open) - always use 30 second timeout
        const float OVERLAY_TIMEOUT_SECONDS = 30.0f;
        if (g_SteamOverlayActive && !g_OverlayTimeoutProtection)
        {
            float overlayActiveTime = (float)currentTime - g_OverlayActiveStartTime;
            if (overlayActiveTime > OVERLAY_TIMEOUT_SECONDS)
            {
                g_OverlayTimeoutProtection = true;
            }
        }
    }
    
    // Clear transition state after a safe period
    if (g_SteamOverlayTransitioning && (currentTime - g_OverlayTransitionTime) > 1.0) // 1 second
    {
        g_SteamOverlayTransitioning = false;
    }
}

// Console command for overlay debugging
static void Steam_OverlayInfo_f(const CCommand& args)
{
    Msg(eDLL_T::STEAM, "=====  STEAM STATUS  =====\n");
#ifndef USE_STEAMWORKS
    Msg(eDLL_T::STEAM, "Steamworks support: DISABLED (compiled without USE_STEAMWORKS)\n");
    Msg(eDLL_T::STEAM, "Offline mode: FORCED (no Steamworks)\n");
#else
    Msg(eDLL_T::STEAM, "Steamworks support: ENABLED\n");
    Msg(eDLL_T::STEAM, "Offline mode: %s\n", Steam_IsOfflineMode() ? "YES" : "NO");
#endif
    Msg(eDLL_T::STEAM, "Steam initialized: %s\n", g_SteamInitialized ? "YES" : "NO");
    Msg(eDLL_T::STEAM, "Steam enabled: %s\n", steam_enabled.GetBool() ? "YES" : "NO");
    
    if (Steam_IsOfflineMode())
    {
        Msg(eDLL_T::STEAM, "Offline username: %s\n", steam_offline_username.GetString());
        Msg(eDLL_T::STEAM, "Offline user ID: %s\n", steam_offline_userid.GetString());
    }
    else if (g_SteamInitialized)
    {
        bool overlayEnabled = Steam_IsOverlayEnabled();
        Msg(eDLL_T::STEAM, "Overlay enabled: %s\n", overlayEnabled ? "YES" : "NO");
        Msg(eDLL_T::STEAM, "Overlay currently active: %s\n", g_SteamOverlayActive ? "YES" : "NO");
        Msg(eDLL_T::STEAM, "Overlay transitioning: %s\n", g_SteamOverlayTransitioning ? "YES" : "NO");
        Msg(eDLL_T::STEAM, "Timeout protection: %s\n", g_OverlayTimeoutProtection ? "ACTIVE" : "INACTIVE");
        Msg(eDLL_T::STEAM, "Freeze protection: ALWAYS ENABLED\n");
        Msg(eDLL_T::STEAM, "Active protection: ALWAYS ENABLED\n");
        Msg(eDLL_T::STEAM, "Timeout threshold: 30.0s\n");
        if (g_SteamOverlayActive && g_OverlayActiveStartTime > 0.0f)
        {
            float activeTime = g_LastCallbackTime - g_OverlayActiveStartTime;
            Msg(eDLL_T::STEAM, "Overlay active for: %.1fs\n", activeTime);
        }
        Msg(eDLL_T::STEAM, "Overlay position: %d\n", steam_overlay_pos.GetInt());
        Msg(eDLL_T::STEAM, "Last callback time: %.2f\n", g_LastCallbackTime);
    }
    
    // Show current effective Steam data
    std::string currentUsername;
    uint64_t currentUserID = Steam_GetUserID();
    Steam_GetUsername(currentUsername);
    
    Msg(eDLL_T::STEAM, "Current username: %s\n", currentUsername.c_str());
    Msg(eDLL_T::STEAM, "Current user ID: %llu\n", currentUserID);
    Msg(eDLL_T::STEAM, "Debug logging: %s\n", steam_debug_auth.GetBool() ? "ENABLED" : "DISABLED");
}

static ConCommand steam_status("steam_status", Steam_OverlayInfo_f, "Display comprehensive Steam status and configuration");

void Steam_Shutdown()
{
    g_SteamShuttingDown = true; // Mark as shutting down first
    
    if (g_SteamInitialized)
    {
        if (steam_debug.GetBool()) Msg(eDLL_T::STEAM, "Shutting down Steam API\n");
        Steam_ShutdownAPI();
        g_SteamInitialized = false;
        UpdateSteamEnabledStatus(); // Update status after shutdown
    }
}


