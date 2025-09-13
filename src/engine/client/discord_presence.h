#pragma once

#ifndef DEDICATED

#include "core/stdafx.h"

class CDiscordPresence
{
public:
    static void Initialize();
    static void Shutdown();
    static void Update();
    static void SetGameState(const char* state, const char* details = nullptr);
    static void SetServerInfo(const char* serverName, int currentPlayers, int maxPlayers, const char* playlist = nullptr);
    static void SetMapInfo(const char* mapName);
    static void ClearServerInfo();
    static void ClearPresence();
    static bool IsEnabled();
    static bool IsConnected();

private:
    static void UpdatePresenceInternal();
    static void UpdateServerInfoFromGame();
    static void OnDiscordReady(const struct DiscordRPCUser* user);
    static void OnDiscordDisconnected(int errorCode, const char* message);
    static void OnDiscordError(int errorCode, const char* message);

    static bool s_bInitialized;
    static bool s_bConnected;
    static char s_szCurrentState[128];
    static char s_szCurrentDetails[128];
    static char s_szCurrentMap[64];
    static char s_szServerName[128];
    static char s_szPlaylist[64];
    static int s_nCurrentPlayers;
    static int s_nMaxPlayers;
    static int64_t s_nStartTime;
    static bool s_bNeedsUpdate;
};

#endif // !DEDICATED
