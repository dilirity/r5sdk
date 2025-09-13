//=============================================================================//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// server.cpp: implementation of the CServer class.
//
/////////////////////////////////////////////////////////////////////////////////
#include "core/stdafx.h"
#include "common/protocol.h"
#include "tier0/frametask.h"
#include "tier1/cvar.h"
#include "tier1/strtools.h"
#include "engine/server/sv_main.h"
#include "engine/server/server.h"
#include "networksystem/pylon.h"
#include "networksystem/bansystem.h"
#include "ebisusdk/EbisuSDK.h"
#include "public/edict.h"
#include "pluginsystem/pluginsystem.h"
#include "game/server/gameinterface.h"
#include "filesystem/filesystem.h"
#include <algorithm>
#include <locale>
#include <codecvt>

//---------------------------------------------------------------------------------
// Console variables
//---------------------------------------------------------------------------------
ConVar sv_showconnecting("sv_showconnecting", "1", FCVAR_RELEASE, "Logs information about the connecting client to the console");

ConVar sv_globalBanlist("sv_globalBanlist", "1", FCVAR_RELEASE, "Determines whether or not to use the global banned list.", false, 0.f, false, 0.f, "0 = Disable, 1 = Enable.");
ConVar sv_banlistRefreshRate("sv_banlistRefreshRate", "30.0", FCVAR_DEVELOPMENTONLY, "Banned list refresh rate (seconds).", true, 1.f, false, 0.f);

static ConVar sv_validatePersonaName("sv_validatePersonaName", "1", FCVAR_RELEASE, "Validate the client's textual persona name on connect.");
static ConVar sv_minPersonaNameLength("sv_minPersonaNameLength", "1", FCVAR_RELEASE, "The minimum length of the client's textual persona name.", true, 0.f, false, 0.f);
static ConVar sv_maxPersonaNameLength("sv_maxPersonaNameLength", "32", FCVAR_RELEASE, "The maximum length of the client's textual persona name.", true, 0.f, false, 0.f);
static ConVar sv_allowIconsInNames("sv_allowIconsInNames", "0", FCVAR_RELEASE, "Allow game icon characters in player names. 0 = Block icons, 1 = Allow icons");
static ConVar sv_nameFilterEnabled("sv_nameFilterEnabled", "1", FCVAR_RELEASE, "Kick players whose names contain words from the bad word list asset");
static ConVar sv_nameFilterPath("sv_nameFilterPath", "chatfilters/badwords.txt", FCVAR_RELEASE, "Relative path (in VPK) to the bad word list file");

//---------------------------------------------------------------------------------
// Purpose: load bad word list from VPK (englishserver_mp_common / englishclient_mp_common)
//---------------------------------------------------------------------------------
static CUtlVector<string> g_NameFilterWords;
static bool g_NameFilterLoaded = false;

// Unicode-aware case conversion for better international character support
static std::string SV_ToLowerUnicode(const std::string& input)
{
    std::string result = input;
    
    // First handle ASCII characters with standard tolower
    std::transform(result.begin(), result.end(), result.begin(), 
        [](unsigned char c) { 
            return (c <= 127) ? (char)tolower(c) : (char)c; 
        });
    
    // For better Unicode support, we could add more sophisticated conversion here
    // This basic version handles ASCII properly and leaves Unicode characters unchanged
    // which is safer than corrupting them with ASCII-only tolower()
    
    return result;
}

// Function to check if a name contains blocked game icon characters
static bool SV_NameContainsBlockedIcons(const char* name)
{
    if (!name) return false;
    
    const unsigned char* p = reinterpret_cast<const unsigned char*>(name);
    
    while (*p)
    {
        // Check for UTF-8 sequences that represent the blocked icon characters
        // These characters are in the Private Use Area around U+F0000-U+F0FFF
        
        // UTF-8 encoding for U+F0000-U+F0FFF:
        // 4-byte sequence: 0xF3 0xB0 0x80-0xBF 0x80-0xBF
        if (p[0] == 0xF3 && p[1] == 0xB0)
        {
            // This is likely one of the blocked icon characters
            return true;
        }
        
        // Also check for some other common Private Use Area ranges that might contain icons
        // U+E000-U+F8FF (3-byte UTF-8: 0xEE-0xEF)
        if (p[0] >= 0xEE && p[0] <= 0xEF)
        {
            // Check if this matches the specific icon pattern
            // The icons you listed seem to be in a specific range
            if (p[0] == 0xEF && p[1] >= 0x80 && p[1] <= 0xBF)
            {
                return true; // Block these specific Private Use Area characters
            }
        }
        
        // Move to next character
        if (*p < 0x80)
        {
            // ASCII character
            p++;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            // 2-byte UTF-8
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            // 3-byte UTF-8
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            // 4-byte UTF-8
            p += 4;
        }
        else
        {
            // Invalid UTF-8, skip
            p++;
        }
    }
    
    return false;
}

static void SV_LoadNameFilter()
{
    g_NameFilterWords.RemoveAll();
    g_NameFilterLoaded = false;

    const char* const filePath = sv_nameFilterPath.GetString();
    if (!filePath || !*filePath)
        return;

    FileHandle_t hFile = FileSystem()->Open(filePath, "rb", "GAME");
    if (hFile == FILESYSTEM_INVALID_HANDLE)
        return;

    const ssize_t nFileSize = FileSystem()->Size(hFile);
    if (nFileSize <= 0)
    {
        FileSystem()->Close(hFile);
        return;
    }

    const u64 nBufSize = FileSystem()->GetOptimalReadSize(hFile, nFileSize + 2);
    char* const pBuf = (char*)FileSystem()->AllocOptimalReadBuffer(hFile, nBufSize, 0);
    if (!pBuf)
    {
        FileSystem()->Close(hFile);
        return;
    }

    const ssize_t nRead = FileSystem()->ReadEx(pBuf, nBufSize, nFileSize, hFile);
    FileSystem()->Close(hFile);
    if (nRead <= 0)
    {
        FileSystem()->FreeOptimalReadBuffer(pBuf);
        return;
    }

    pBuf[nRead] = '\0';

    // Parse lines
    const char* p = pBuf;
    while (*p)
    {
        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') ++p;
        if (!*p) break;

        const char* start = p;
        while (*p && *p != '\r' && *p != '\n') ++p;
        string word(start, p - start);

        while (!word.empty() && (word.back() == ' ' || word.back() == '\t')) word.pop_back();
        if (!word.empty())
        {
            // Use Unicode-aware case conversion
            std::string lowerWord = SV_ToLowerUnicode(word);
            g_NameFilterWords.AddToTail(lowerWord);
        }
    }

    FileSystem()->FreeOptimalReadBuffer(pBuf);
    g_NameFilterLoaded = true;
}

static bool SV_NameContainsBadWord(const char* pszName)
{
    if (!sv_nameFilterEnabled.GetBool() || !pszName || !*pszName)
        return false;

    if (!g_NameFilterLoaded)
        SV_LoadNameFilter();

    if (!g_NameFilterLoaded || g_NameFilterWords.IsEmpty())
        return false;

    std::string lowered = SV_ToLowerUnicode(std::string(pszName));

    FOR_EACH_VEC(g_NameFilterWords, i)
    {
        if (lowered.find(g_NameFilterWords[i]) != string::npos)
            return true;
    }
    return false;
}

//---------------------------------------------------------------------------------
// Purpose: Gets the number of human players on the server
// Output : int
//---------------------------------------------------------------------------------
int CServer::GetNumHumanPlayers(void) const
{
	int nHumans = 0;
	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient->IsHumanPlayer())
			nHumans++;
	}

	return nHumans;
}

//---------------------------------------------------------------------------------
// Purpose: Gets the number of fake clients on the server
// Output : int
//---------------------------------------------------------------------------------
int CServer::GetNumFakeClients(void) const
{
	int nBots = 0;
	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient->IsConnected() && pClient->IsFakeClient())
			nBots++;
	}

	return nBots;
}

//---------------------------------------------------------------------------------
// Purpose: Gets the number of clients on the server
// Output : int
//---------------------------------------------------------------------------------
int CServer::GetNumClients(void) const
{
	int nClients = 0;
	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient->IsConnected())
			nClients++;
	}

	return nClients;
}

//---------------------------------------------------------------------------------
// Purpose: Rejects connection request and sends back a message
// Input  : iSocket - 
//			*pChallenge - 
//			*szMessage - 
//---------------------------------------------------------------------------------
void CServer::RejectConnection(int iSocket, netadr_t* pNetAdr, const char* szMessage)
{
	CServer__RejectConnection(this, iSocket, pNetAdr, szMessage);
}

//---------------------------------------------------------------------------------
// Purpose: Initializes a CSVClient for a new net connection. This will only be called
//			once for a player each game, not once for each level change.
// Input  : *pServer - 
//			*pChallenge - 
// Output : pointer to client instance on success, nullptr on failure
//---------------------------------------------------------------------------------
CClient* CServer::ConnectClient(CServer* pServer, user_creds_s* pChallenge)
{
	if (pServer->m_State < server_state_t::ss_active)
		return nullptr;

	char* pszPersonaName = pChallenge->personaName;
	SteamID_t nSteamID = pChallenge->personaId;

	const bool bEnableLogging = sv_showconnecting.GetBool();
	const int nPort = int(ntohs(pChallenge->netAdr.GetPort()));

	char szAddresBuffer[128];
	const char* pszAddresBuffer = nullptr;

	if (bEnableLogging)
	{
		// Render the client address once.
		pChallenge->netAdr.ToString(szAddresBuffer, sizeof(szAddresBuffer), true);
		pszAddresBuffer = szAddresBuffer;

		Msg(eDLL_T::SERVER, "Processing connectionless challenge for '[%s]:%i' ('%llu')\n",
			pszAddresBuffer, nPort, nSteamID);
	}

	// Reject if persona name contains a filtered word from asset (VPK)
	if (SV_NameContainsBadWord(pszPersonaName))
	{
		//#Client_Reject_Banned_Name: Your name contains a banned word.
		pServer->RejectConnection(pServer->m_Socket, &pChallenge->netAdr, "#Client_Reject_Banned_Name");
		if (bEnableLogging)
		{
			if (!pszAddresBuffer)
			{
				pChallenge->netAdr.ToString(szAddresBuffer, sizeof(szAddresBuffer), true);
				pszAddresBuffer = szAddresBuffer;
			}
			Warning(eDLL_T::SERVER, "Connection rejected for '[%s]:%i' ('%llu' name contains banned word)\n",
				pszAddresBuffer, nPort, nSteamID);
		}
		return nullptr;
	}

	// Reject if persona name contains blocked game icon characters (unless allowed)
	if (!sv_allowIconsInNames.GetBool() && SV_NameContainsBlockedIcons(pszPersonaName))
	{
		//#Client_Reject_Invalid_Name: Your name contains invalid characters.
		pServer->RejectConnection(pServer->m_Socket, &pChallenge->netAdr, "#Client_Reject_Invalid_Name");
		if (bEnableLogging)
		{
			if (!pszAddresBuffer)
			{
				pChallenge->netAdr.ToString(szAddresBuffer, sizeof(szAddresBuffer), true);
				pszAddresBuffer = szAddresBuffer;
			}
			Warning(eDLL_T::SERVER, "Connection rejected for '[%s]:%i' ('%llu' name contains blocked icon characters)\n",
				pszAddresBuffer, nPort, nSteamID);
		}
		return nullptr;
	}

	bool bValidName = false;

	if (VALID_CHARSTAR(pszPersonaName) &&
		V_IsValidUTF8(pszPersonaName))
	{
		if (sv_validatePersonaName.GetBool() && 
			!IsValidPersonaName(pszPersonaName, sv_minPersonaNameLength.GetInt(), sv_maxPersonaNameLength.GetInt()))
		{
			bValidName = false;
		}
		else
		{
			bValidName = true;
		}
	}

	// Only proceed connection if the client's name is valid and UTF-8 encoded.
	if (!bValidName)
	{
		pServer->RejectConnection(pServer->m_Socket, &pChallenge->netAdr, "#Valve_Reject_Invalid_Name");

		if (bEnableLogging)
		{
			Warning(eDLL_T::SERVER, "Connection rejected for '[%s]:%i' ('%llu' has an invalid name!)\n",
				pszAddresBuffer, nPort, nSteamID);
		}

		return nullptr;
	}

	if (g_BanSystem.IsBanned(&pChallenge->netAdr, nSteamID))
	{
		pServer->RejectConnection(pServer->m_Socket, &pChallenge->netAdr, "#Valve_Reject_Banned");

		if (bEnableLogging)
		{
			Warning(eDLL_T::SERVER, "Connection rejected for '[%s]:%i' ('%llu' is banned from this server!)\n",
				pszAddresBuffer, nPort, nSteamID);
		}

		return nullptr;
	}

	CClient* const pClient = CServer__ConnectClient(pServer, pChallenge);

	for (auto& callback : !PluginSystem()->GetConnectClientCallbacks())
	{
		if (!callback.Function()(pServer, pClient, pChallenge))
		{
			pClient->Disconnect(REP_MARK_BAD, "#Valve_Reject_Banned");
			return nullptr;
		}
	}

	if (pClient && sv_globalBanlist.GetBool())
	{
		if (!pClient->GetNetChan()->GetRemoteAddress().IsLoopback())
		{
			if (!pszAddresBuffer)
			{
				pChallenge->netAdr.ToString(szAddresBuffer, sizeof(szAddresBuffer), true);
				pszAddresBuffer = szAddresBuffer;
			}

			const string addressBufferCopy(pszAddresBuffer);
			const string personaNameCopy(pszPersonaName);

			std::thread th(SV_CheckForBanAndDisconnect, pClient, addressBufferCopy, nSteamID, personaNameCopy, nPort);
			th.detach();
		}
	}

	return pClient;
}

//---------------------------------------------------------------------------------
// Purpose: Sends netmessage to all active clients
// Input  : *msg       -
//          onlyActive - 
//          reliable   - 
//---------------------------------------------------------------------------------
void CServer::BroadcastMessage(CNetMessage* const msg, const bool onlyActive, const bool reliable)
{
	CServer__BroadcastMessage(this, msg, onlyActive, reliable);
}

//---------------------------------------------------------------------------------
// Purpose: Runs the server frame
// Input  : *pServer - 
//---------------------------------------------------------------------------------
void CServer::RunFrame(CServer* pServer)
{
	CServer__RunFrame(pServer);
}

///////////////////////////////////////////////////////////////////////////////
void VServer::Detour(const bool bAttach) const
{
	DetourSetup(&CServer__RunFrame, &CServer::RunFrame, bAttach);
	DetourSetup(&CServer__ConnectClient, &CServer::ConnectClient, bAttach);
}

///////////////////////////////////////////////////////////////////////////////
CServer* g_pServer = nullptr;
CClientExtended CServer::sm_ClientsExtended[MAX_PLAYERS];
