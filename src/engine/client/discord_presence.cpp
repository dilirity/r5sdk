#include "core/stdafx.h"

#ifndef DEDICATED

#include "discord_presence.h"
#include "discord_rpc_wrapper.h"
#include "tier1/cvar.h"
#include "common/global.h"
#include "engine/client/clientstate.h"
#include "engine/host_state.h"
#include "engine/server/server.h"
#include "rtech/playlists/playlists.h"
#include <chrono>
#include <tier0/commandline.h>

// Discord Application ID
#define DISCORD_APP_ID "1416532212215709938"

ConVar discord_enable("discord_enable", "1", FCVAR_RELEASE, "Enable Discord Rich Presence updates");

// Static member definitions
bool CDiscordPresence::s_bInitialized = false;
bool CDiscordPresence::s_bConnected = false;
char CDiscordPresence::s_szCurrentState[128] = {0};
char CDiscordPresence::s_szCurrentDetails[128] = {0};
char CDiscordPresence::s_szCurrentMap[64] = {0};
char CDiscordPresence::s_szServerName[128] = {0};
char CDiscordPresence::s_szPlaylist[64] = {0};
int CDiscordPresence::s_nCurrentPlayers = 0;
int CDiscordPresence::s_nMaxPlayers = 0;
int64_t CDiscordPresence::s_nStartTime = 0;
bool CDiscordPresence::s_bNeedsUpdate = false;

//-----------------------------------------------------------------------------
// Purpose: Initialize Discord Rich Presence
//-----------------------------------------------------------------------------
void CDiscordPresence::Initialize()
{
    if (s_bInitialized)
        return;


    // Check if Discord is disabled via ConVar (which respects -nodiscord argument)
    if (!IsEnabled())
    {
        DevMsg(eDLL_T::CLIENT, "Discord Rich Presence disabled\n");
        return;
    }

    DiscordEventHandlers handlers = {};
    handlers.ready = OnDiscordReady;
    handlers.disconnected = OnDiscordDisconnected;
    handlers.errored = OnDiscordError;

    DevMsg(eDLL_T::CLIENT, "Initializing Discord Rich Presence with App ID: %s\n", DISCORD_APP_ID);
    
    Discord_Initialize(DISCORD_APP_ID, &handlers, 1, nullptr);
    
    s_bInitialized = true;
    s_nStartTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    DevMsg(eDLL_T::CLIENT, "Discord Rich Presence initialized\n");
}

//-----------------------------------------------------------------------------
// Purpose: Shutdown Discord Rich Presence
//-----------------------------------------------------------------------------
void CDiscordPresence::Shutdown()
{
    if (!s_bInitialized)
        return;

    Discord_Shutdown();
    s_bInitialized = false;
    s_bConnected = false;
    
    DevMsg(eDLL_T::CLIENT, "Discord Rich Presence shutdown\n");
}

//-----------------------------------------------------------------------------
// Purpose: Update Discord Rich Presence (called every frame)
//-----------------------------------------------------------------------------
void CDiscordPresence::Update()
{
    if (!s_bInitialized || !IsEnabled())
        return;

    Discord_RunCallbacks();

    // Update server info from game state
    UpdateServerInfoFromGame();

    // Update presence if needed
    if (s_bNeedsUpdate)
    {
        UpdatePresenceInternal();
        s_bNeedsUpdate = false;
    }
}

//-----------------------------------------------------------------------------
// Purpose: Set game state information
//-----------------------------------------------------------------------------
void CDiscordPresence::SetGameState(const char* state, const char* details)
{
    if (!s_bInitialized)
        return;

    bool changed = false;

    if (state)
    {
        // Validate and sanitize state string
        size_t stateLen = strlen(state);
        if (stateLen > 0 && stateLen < sizeof(s_szCurrentState) && strcmp(s_szCurrentState, state) != 0)
        {
            V_strncpy(s_szCurrentState, state, sizeof(s_szCurrentState));
            s_szCurrentState[sizeof(s_szCurrentState) - 1] = '\0'; // Ensure null termination
            changed = true;
        }
    }

    if (details)
    {
        // Validate and sanitize details string
        size_t detailsLen = strlen(details);
        if (detailsLen > 0 && detailsLen < sizeof(s_szCurrentDetails) && strcmp(s_szCurrentDetails, details) != 0)
        {
            V_strncpy(s_szCurrentDetails, details, sizeof(s_szCurrentDetails));
            s_szCurrentDetails[sizeof(s_szCurrentDetails) - 1] = '\0'; // Ensure null termination
            changed = true;
        }
    }

    if (changed)
        s_bNeedsUpdate = true;
}

//-----------------------------------------------------------------------------
// Purpose: Set server information
//-----------------------------------------------------------------------------
void CDiscordPresence::SetServerInfo(const char* serverName, int currentPlayers, int maxPlayers, const char* playlist)
{
    if (!s_bInitialized)
        return;

    bool changed = false;

    if (serverName)
    {
        // Validate and sanitize server name
        size_t serverNameLen = strlen(serverName);
        if (serverNameLen > 0 && serverNameLen < sizeof(s_szServerName) && strcmp(s_szServerName, serverName) != 0)
        {
            V_strncpy(s_szServerName, serverName, sizeof(s_szServerName));
            s_szServerName[sizeof(s_szServerName) - 1] = '\0'; // Ensure null termination
            changed = true;
        }
    }

    if (playlist)
    {
        // Validate and sanitize playlist name
        size_t playlistLen = strlen(playlist);
        if (playlistLen > 0 && playlistLen < sizeof(s_szPlaylist) && strcmp(s_szPlaylist, playlist) != 0)
        {
            V_strncpy(s_szPlaylist, playlist, sizeof(s_szPlaylist));
            s_szPlaylist[sizeof(s_szPlaylist) - 1] = '\0'; // Ensure null termination
            changed = true;
        }
    }

    // Validate player counts to prevent integer overflow/underflow
    if (currentPlayers >= 0 && currentPlayers <= 10000 && s_nCurrentPlayers != currentPlayers)
    {
        s_nCurrentPlayers = currentPlayers;
        changed = true;
    }

    if (maxPlayers >= 0 && maxPlayers <= 10000 && s_nMaxPlayers != maxPlayers)
    {
        s_nMaxPlayers = maxPlayers;
        changed = true;
    }

    if (changed)
        s_bNeedsUpdate = true;
}

//-----------------------------------------------------------------------------
// Purpose: Clear server information
//-----------------------------------------------------------------------------
void CDiscordPresence::ClearServerInfo()
{
    if (!s_bInitialized)
        return;

    bool changed = false;

    if (s_szServerName[0])
    {
        s_szServerName[0] = '\0';
        changed = true;
    }

    if (s_szPlaylist[0])
    {
        s_szPlaylist[0] = '\0';
        changed = true;
    }

    if (s_nCurrentPlayers != 0)
    {
        s_nCurrentPlayers = 0;
        changed = true;
    }

    if (s_nMaxPlayers != 0)
    {
        s_nMaxPlayers = 0;
        changed = true;
    }

    if (changed)
        s_bNeedsUpdate = true;
}

//-----------------------------------------------------------------------------
// Purpose: Set map information
//-----------------------------------------------------------------------------
void CDiscordPresence::SetMapInfo(const char* mapName)
{
    if (!s_bInitialized || !mapName)
        return;

    // Validate and sanitize map name
    size_t mapNameLen = strlen(mapName);
    if (mapNameLen > 0 && mapNameLen < sizeof(s_szCurrentMap) && strcmp(s_szCurrentMap, mapName) != 0)
    {
        V_strncpy(s_szCurrentMap, mapName, sizeof(s_szCurrentMap));
        s_szCurrentMap[sizeof(s_szCurrentMap) - 1] = '\0'; // Ensure null termination
        s_bNeedsUpdate = true;
    }
}

//-----------------------------------------------------------------------------
// Purpose: Clear Discord presence
//-----------------------------------------------------------------------------
void CDiscordPresence::ClearPresence()
{
    if (!s_bInitialized)
        return;

    Discord_ClearPresence();
    
    // Clear internal state
    s_szCurrentState[0] = '\0';
    s_szCurrentDetails[0] = '\0';
    s_szCurrentMap[0] = '\0';
    s_szServerName[0] = '\0';
    s_nCurrentPlayers = 0;
    s_nMaxPlayers = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Check if Discord Rich Presence is enabled
//-----------------------------------------------------------------------------
bool CDiscordPresence::IsEnabled()
{
    return discord_enable.GetBool();
}

//-----------------------------------------------------------------------------
// Purpose: Check if Discord is connected
//-----------------------------------------------------------------------------
bool CDiscordPresence::IsConnected()
{
    return s_bConnected;
}

//-----------------------------------------------------------------------------
// Purpose: Internal function to update Discord presence
//-----------------------------------------------------------------------------
void CDiscordPresence::UpdatePresenceInternal()
{
    if (!s_bConnected || !IsEnabled())
        return;

    DiscordRichPresence presence = {};
    
    // If we have server info, show server details
    if (s_szServerName[0] && s_nMaxPlayers > 0)
    {
        // Show server name as state
        presence.state = s_szServerName;
        
        // Show map and playlist as details with safe formatting
        static char detailsBuffer[256];
        memset(detailsBuffer, 0, sizeof(detailsBuffer));
        
        if (s_szCurrentMap[0] && s_szPlaylist[0])
        {
            // Ensure we don't overflow the buffer
            int written = V_snprintf(detailsBuffer, sizeof(detailsBuffer) - 1, "%s (%s)", s_szCurrentMap, s_szPlaylist);
            if (written < 0 || written >= (int)(sizeof(detailsBuffer) - 1))
            {
                V_strncpy(detailsBuffer, "In game", sizeof(detailsBuffer));
            }
        }
        else if (s_szCurrentMap[0])
        {
            V_strncpy(detailsBuffer, s_szCurrentMap, sizeof(detailsBuffer));
        }
        else if (s_szPlaylist[0])
        {
            int written = V_snprintf(detailsBuffer, sizeof(detailsBuffer) - 1, "Playing %s", s_szPlaylist);
            if (written < 0 || written >= (int)(sizeof(detailsBuffer) - 1))
            {
                V_strncpy(detailsBuffer, "In game", sizeof(detailsBuffer));
            }
        }
        else
        {
            V_strncpy(detailsBuffer, "In game", sizeof(detailsBuffer));
        }
        detailsBuffer[sizeof(detailsBuffer) - 1] = '\0'; // Ensure null termination
        presence.details = detailsBuffer;
        
        // Set party information
        presence.partySize = s_nCurrentPlayers;
        presence.partyMax = s_nMaxPlayers;
    }
    else
    {
        // Fallback to basic state/details
        if (s_szCurrentState[0])
            presence.state = s_szCurrentState;
        
        if (s_szCurrentDetails[0])
            presence.details = s_szCurrentDetails;
    }
    
    // Set timestamps
    presence.startTimestamp = s_nStartTime;
    
    // Set images
    presence.largeImageKey = "r5v_logo";
    presence.largeImageText = "R5Valkyrie";

    Discord_UpdatePresence(&presence);
}

//-----------------------------------------------------------------------------
// Purpose: Update server info from game state
//-----------------------------------------------------------------------------
void CDiscordPresence::UpdateServerInfoFromGame()
{
    if (!s_bInitialized)
        return;

    // Check if we're connected to a server
    if (!g_pClientState || !g_pClientState->IsConnected())
    {
        // Clear server info if not connected
        s_szServerName[0] = '\0';
        s_szPlaylist[0] = '\0';
        s_nCurrentPlayers = 0;
        s_nMaxPlayers = 0;
        return;
    }

    // Get server name from hostname ConVar
    const char* serverName = "Unknown Server";
    if (hostname && hostname->GetString()[0])
    {
        serverName = hostname->GetString();
    }

    // Get current playlist
    const char* currentPlaylist = "Unknown";
    if (v_Playlists_GetCurrent)
    {
        const char* playlist = v_Playlists_GetCurrent();
        if (playlist && playlist[0])
        {
            currentPlaylist = playlist;
        }
    }

    // Get player counts
    int currentPlayers = 0;
    int maxPlayers = 0;
    
#ifndef CLIENT_DLL
    // If we're on the server side, get info from server
    if (g_pServer && g_pServer->IsActive())
    {
        currentPlayers = g_pServer->GetNumClients();
        maxPlayers = g_pServer->GetMaxClients();
    }
#else
    // If we're on the client side, get info from client state
    if (g_pClientState)
    {
        maxPlayers = g_pClientState->m_nMaxClients;
        // For current players, we'll estimate based on what we know
        currentPlayers = 1; // At least us
    }
#endif

    // Update server info
    SetServerInfo(serverName, currentPlayers, maxPlayers, currentPlaylist);
}

//-----------------------------------------------------------------------------
// Purpose: Discord ready callback
//-----------------------------------------------------------------------------
void CDiscordPresence::OnDiscordReady(const DiscordRPCUser* user)
{
    s_bConnected = true;
    s_bNeedsUpdate = true;
    
    if (user && user->username && user->discriminator)
    {
        // Validate user data before logging to prevent format string attacks
        char safeUsername[64] = {0};
        char safeDiscriminator[16] = {0};
        
        V_strncpy(safeUsername, user->username, sizeof(safeUsername));
        V_strncpy(safeDiscriminator, user->discriminator, sizeof(safeDiscriminator));
        
        // Ensure null termination
        safeUsername[sizeof(safeUsername) - 1] = '\0';
        safeDiscriminator[sizeof(safeDiscriminator) - 1] = '\0';
        
        DevMsg(eDLL_T::CLIENT, "Discord Rich Presence connected (User: %s#%s)\n", 
               safeUsername, safeDiscriminator);
    }
    else
    {
        DevMsg(eDLL_T::CLIENT, "Discord Rich Presence connected\n");
    }
    
    // Set initial presence
    SetGameState("In menu", "R5Valkyrie");
}

//-----------------------------------------------------------------------------
// Purpose: Discord disconnected callback
//-----------------------------------------------------------------------------
void CDiscordPresence::OnDiscordDisconnected(int errorCode, const char* message)
{
    s_bConnected = false;
    
    if (message)
    {
        // Sanitize message to prevent format string attacks
        char safeMessage[256] = {0};
        V_strncpy(safeMessage, message, sizeof(safeMessage));
        safeMessage[sizeof(safeMessage) - 1] = '\0';
        
        DevMsg(eDLL_T::CLIENT, "Discord Rich Presence disconnected (Error: %d, Message: %s)\n", 
               errorCode, safeMessage);
    }
    else
    {
        DevMsg(eDLL_T::CLIENT, "Discord Rich Presence disconnected (Error: %d)\n", errorCode);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Discord error callback
//-----------------------------------------------------------------------------
void CDiscordPresence::OnDiscordError(int errorCode, const char* message)
{
    if (message)
    {
        // Sanitize message to prevent format string attacks
        char safeMessage[256] = {0};
        V_strncpy(safeMessage, message, sizeof(safeMessage));
        safeMessage[sizeof(safeMessage) - 1] = '\0';
        
        DevWarning(eDLL_T::CLIENT, "Discord Rich Presence error (Error: %d, Message: %s)\n", 
                   errorCode, safeMessage);
    }
    else
    {
        DevWarning(eDLL_T::CLIENT, "Discord Rich Presence error (Error: %d)\n", errorCode);
    }
}

#endif // !DEDICATED
