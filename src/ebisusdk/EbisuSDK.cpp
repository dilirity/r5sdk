#include "core/stdafx.h"
#include "tier0/commandline.h"
#include "ebisusdk/EbisuSDK.h"
#include "engine/server/sv_main.h"

//-----------------------------------------------------------------------------
// Purpose: initialize the EbisuSDK
//-----------------------------------------------------------------------------
void HEbisuSDK_Init()
{
	const bool isDedicated = IsDedicated();
	const bool steamMode = IsSteamMode();

	// Fill with default data if this is a dedicated server, or if the game was
	// launched with the platform system disabled. Engine code requires these
	// to be set for the game to function, else stuff like the "map" command
	// won't run as 'IsPlatformInitialized()' returns false (which got inlined in
	// every place this was called in the game's executable).
	// Steam-only mode: bypass EA/Origin requirement and use placeholder values
	// The g_SteamUserID will be replaced with the actual Steam ID during authentication
	if (isDedicated || steamMode)
	{
		*g_EbisuSDKInit = true;
		*g_EbisuProfileInit = true;

		// Only set fake Steam ID if not already initialized with a real Steam ID
		if (*g_SteamUserID == 0 || *g_SteamUserID == FAKE_BASE_STEAM_ID)
		{
			*g_SteamUserID = FAKE_BASE_STEAM_ID;
		}

		// EA/Origin authentication tokens are no longer needed with Steam authentication
		// Q_snprintf(g_OriginAuthCode, 256, "%s", "INVALID_OAUTH_CODE"); // REMOVED: No longer needed with Steam auth
		Q_snprintf(g_LegacyAuthToken, 1024, "%s", "INVALID_LEGACY_TOKEN"); // Legacy compatibility (renamed from legacy EA token)

		if (!isDedicated)
		{
			// Sync platform_user_id with g_SteamUserID (which may have been set during SDK_Init)
			if (*g_SteamUserID != 0 && *g_SteamUserID != FAKE_BASE_STEAM_ID)
			{
				std::string steamIDStr = Format("%llu", *g_SteamUserID);
				platform_user_id->SetValue(steamIDStr.c_str());
			}
			else
			{
				platform_user_id->SetValue(FAKE_BASE_STEAM_ID);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: runs the EbisuSDK state machine
//-----------------------------------------------------------------------------
void HEbisuSDK_RunFrame()
{
	if (IsSteamMode())
	{
		return;
	}

	EbisuSDK_RunFrame();
}

//-----------------------------------------------------------------------------
// Purpose: returns the currently set language
//-----------------------------------------------------------------------------
const char* HEbisuSDK_GetLanguage()
{
	static bool initialized = false;
	static char languageName[32];

	if (initialized)
	{
		return languageName;
	}

	const char* value = nullptr;
	bool useDefault = true;

	if (CommandLine()->CheckParm("-language", &value))
	{
		if (V_LocaleNameExists(value))
		{
			strncpy(languageName, value, sizeof(languageName));
			useDefault = false;
		}
	}

	if (useDefault)
	{
		strncpy(languageName, g_LanguageNames[0], sizeof(languageName));
	}

	languageName[sizeof(languageName) - 1] = '\0';
	initialized = true;

	return languageName;
}

//-----------------------------------------------------------------------------
// Purpose: checks if the EbisuSDK is disabled
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool IsSteamMode()
{
	const static bool steamMode = CommandLine()->CheckParm("-noorigin");
	return steamMode;
}

//-----------------------------------------------------------------------------
// Purpose: checks if the EbisuSDK is initialized
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool IsPlatformInitialized()
{
	if (IsDedicated())
	{
		return true;
	}
	else if ((!(*g_PlatformErrorLevel)
		&& (*g_EbisuSDKInit)
		&& (*g_SteamUserID)
		&& (*g_EbisuProfileInit)))
	// Note(amos): checks on legacy auth tokens are disabled because we should be able to
	// load into the game without legacy EA/Origin tokens. There won't be
	// a token if the game is launched with -offline for example, these are
	// only used for legacy platform system compatibility.
	//	&& (*g_OriginAuthCode)     [REMOVED - no longer needed]
	//	&& (g_LegacyAuthToken[0])  [DISABLED - legacy compatibility only]))
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: validates if client's persona name meets Steam-friendly criteria (much more lenient than EA)
// Input  : *pszName -
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool IsValidPersonaName(const char* const pszName, const int nMinLen, const int nMaxLen)
{
	const size_t len = strlen(pszName);

	if (len < nMinLen ||
		len > nMaxLen)
	{
		return false;
	}

	// Allow most printable characters including symbols and spaces (Steam-friendly validation)
	// Only block control characters (0x00-0x1F) and DEL (0x7F)
	for (size_t i = 0; i < len; i++)
	{
		unsigned char c = (unsigned char)pszName[i];
		if (c < 0x20 || c == 0x7F) // Block control characters and DEL
		{
			return false;
		}
	}
	
	return true;
}

void VEbisuSDK::Detour(const bool bAttach) const
{
	DetourSetup(&EbisuSDK_RunFrame, &HEbisuSDK_RunFrame, bAttach);
	DetourSetup(&EbisuSDK_GetLanguage, &HEbisuSDK_GetLanguage, bAttach);
}
