#pragma once

#include <string>

// Forward declarations
class ConVar;

// These functions are safe to call even if Steamworks is not compiled in.
// They return false in that case.

bool Steam_EnsureInitialized();
bool Steam_GetAuthSessionTicketBase64(std::string& outTicket);
void Steam_CancelCurrentAuthTicket();
bool Steam_GetUsername(std::string& outUsername);
uint64_t Steam_GetUserID();
void Steam_RunFrame();
void Steam_Shutdown();
bool Steam_IsOfflineMode();


// Steam ConVars accessible from other files
extern ConVar steam_debug_auth;


