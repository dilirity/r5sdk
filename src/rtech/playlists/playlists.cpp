//===========================================================================//
//
// Purpose: Playlists system
//
//===========================================================================//
#include "core/stdafx.h"
#include "engine/sys_dll2.h"
#include "engine/cmodel_bsp.h"
#include "playlists.h"
#include "pluginsystem/modsystem.h"
#include "filesystem/filesystem.h"

static ConVar playlist_debug("playlist_debug", "0", FCVAR_RELEASE, "Enable debug logging for playlist mod system");

KeyValues** g_pPlaylistKeyValues = nullptr; // The KeyValue for the playlist file.
char* g_pPlaylistMapToLoad = nullptr;

CUtlVector<CUtlString> g_vecAllPlaylists;   // Cached playlists entries.

//-----------------------------------------------------------------------------
// Purpose: Merges mod playlist patches into the base playlist file at startup
//-----------------------------------------------------------------------------
void MergeModPlaylistsIntoFile()
{
	const char* playlistFilePath = "platform/playlists_r5_patch.txt";
	
	// Load the base playlist file
	KeyValues* pBaseKV = new KeyValues("playlists");
	if (!pBaseKV->LoadFromFile(FileSystem(), playlistFilePath, "GAME"))
	{
		Msg(eDLL_T::ENGINE, "Could not load base playlist file for mod merging\n");
		delete pBaseKV;
		return;
	}

	bool hasChanges = false;

	// First, clean up orphaned mod playlists from the base file
	KeyValues* pBasePlaylists = pBaseKV->FindKey("Playlists");
	if (pBasePlaylists)
	{
		if (playlist_debug.GetBool())
			Msg(eDLL_T::ENGINE, "Checking for orphaned playlists in base file...\n");
		int playlistCount = 0;
		for (KeyValues* pPlaylist = pBasePlaylists->GetFirstTrueSubKey(); pPlaylist; )
		{
			KeyValues* pNext = pPlaylist->GetNextTrueSubKey();
			const char* playlistName = pPlaylist->GetName();
			// Check both direct for_mod and vars/for_mod
			const char* forMod = pPlaylist->GetString("for_mod", "");
			if (!forMod || !forMod[0])
			{
				// Try looking in vars subsection
				KeyValues* pVars = pPlaylist->FindKey("vars");
				if (pVars)
				{
					forMod = pVars->GetString("for_mod", "");
				}
			}
			playlistCount++;
			
			if (playlist_debug.GetBool())
				Msg(eDLL_T::ENGINE, "Playlist #%d: '%s', for_mod='%s'\n", playlistCount, playlistName, forMod ? forMod : "(null)");
			
			if (forMod && forMod[0]) // Has for_mod var
			{
				if (playlist_debug.GetBool())
					Msg(eDLL_T::ENGINE, "Found playlist '%s' with for_mod='%s', checking if mod is enabled...\n", 
						playlistName, forMod);
				
				// Check if the mod is still enabled and still provides this playlist
				bool playlistStillProvided = false;
				if (ModSystem()->IsEnabled())
				{
					ModSystem()->LockModList();
					FOR_EACH_VEC(ModSystem()->GetModList(), i)
					{
						CModSystem::ModInstance_t* const pMod = ModSystem()->GetModList()[i];
						if (pMod && pMod->IsEnabled() && V_strcmp(pMod->name.String(), forMod) == 0)
						{
							// Check if this mod still provides this specific playlist
							static const char* kPatchFiles[] = {
								"playlists_r5_patch.txt",
								"playlist_r5_patch.txt"
							};

							for (int j = 0; j < Q_ARRAYSIZE(kPatchFiles); ++j)
							{
								CUtlString patchPath = pMod->GetBasePath();
								patchPath += kPatchFiles[j];

								if (!FileSystem()->FileExists(patchPath.Get(), "GAME"))
									continue;

								// Load mod's playlist patch to check if it still provides this playlist
								KeyValues* pModKV = new KeyValues("playlists");
								if (pModKV->LoadFromFile(FileSystem(), patchPath.Get(), "GAME"))
								{
									KeyValues* pModPlaylists = pModKV->FindKey("Playlists");
									if (pModPlaylists && pModPlaylists->FindKey(playlistName, false))
									{
										playlistStillProvided = true;
										if (playlist_debug.GetBool())
											Msg(eDLL_T::ENGINE, "Mod '%s' still provides playlist '%s', keeping it\n", forMod, playlistName);
									}
								}
								delete pModKV;
								break; // Only check first found patch file
							}
							break;
						}
					}
					ModSystem()->UnlockModList();
				}
				
				if (!playlistStillProvided)
				{
					Msg(eDLL_T::ENGINE, "Removing orphaned playlist '%s' (mod '%s' no longer provides it)\n", 
						playlistName, forMod);
					pBasePlaylists->RemoveSubKey(pPlaylist);
					hasChanges = true;
				}
			}
			// Playlists without for_mod are base game playlists, leave them alone
			
			pPlaylist = pNext;
		}
	}

	// Do the same for Gamemodes
	KeyValues* pBaseGamemodes = pBaseKV->FindKey("Gamemodes");
	if (pBaseGamemodes)
	{
		if (playlist_debug.GetBool())
			Msg(eDLL_T::ENGINE, "Checking for orphaned gamemodes in base file...\n");
		for (KeyValues* pGamemode = pBaseGamemodes->GetFirstTrueSubKey(); pGamemode; )
		{
			KeyValues* pNext = pGamemode->GetNextTrueSubKey();
			const char* gamemodeName = pGamemode->GetName();
			// Check both direct for_mod and vars/for_mod
			const char* forMod = pGamemode->GetString("for_mod", "");
			if (!forMod || !forMod[0])
			{
				// Try looking in vars subsection
				KeyValues* pVars = pGamemode->FindKey("vars");
				if (pVars)
				{
					forMod = pVars->GetString("for_mod", "");
				}
			}
			
			if (forMod && forMod[0]) // Has for_mod var
			{
				if (playlist_debug.GetBool())
					Msg(eDLL_T::ENGINE, "Found gamemode '%s' with for_mod='%s', checking if mod is enabled...\n", 
						gamemodeName, forMod);
				
				// Check if the mod is still enabled and still provides this gamemode
				bool gamemodeStillProvided = false;
				if (ModSystem()->IsEnabled())
				{
					ModSystem()->LockModList();
					FOR_EACH_VEC(ModSystem()->GetModList(), i)
					{
						CModSystem::ModInstance_t* const pMod = ModSystem()->GetModList()[i];
						if (pMod && pMod->IsEnabled() && V_strcmp(pMod->name.String(), forMod) == 0)
						{
							// Check if this mod still provides this specific gamemode
							static const char* kPatchFiles[] = {
								"playlists_r5_patch.txt",
								"playlist_r5_patch.txt"
							};

							for (int j = 0; j < Q_ARRAYSIZE(kPatchFiles); ++j)
							{
								CUtlString patchPath = pMod->GetBasePath();
								patchPath += kPatchFiles[j];

								if (!FileSystem()->FileExists(patchPath.Get(), "GAME"))
									continue;

								// Load mod's playlist patch to check if it still provides this gamemode
								KeyValues* pModKV = new KeyValues("playlists");
								if (pModKV->LoadFromFile(FileSystem(), patchPath.Get(), "GAME"))
								{
									KeyValues* pModGamemodes = pModKV->FindKey("Gamemodes");
									if (pModGamemodes && pModGamemodes->FindKey(gamemodeName, false))
									{
										gamemodeStillProvided = true;
										if (playlist_debug.GetBool())
											Msg(eDLL_T::ENGINE, "Mod '%s' still provides gamemode '%s', keeping it\n", forMod, gamemodeName);
									}
								}
								delete pModKV;
								break; // Only check first found patch file
							}
							break;
						}
					}
					ModSystem()->UnlockModList();
				}
				
				if (!gamemodeStillProvided)
				{
					Msg(eDLL_T::ENGINE, "Removing orphaned gamemode '%s' (mod '%s' no longer provides it)\n", 
						gamemodeName, forMod);
					pBaseGamemodes->RemoveSubKey(pGamemode);
					hasChanges = true;
				}
			}
			// Gamemodes without for_mod are base game gamemodes, leave them alone
			
			pGamemode = pNext;
		}
	}
	
	// Only process new mod playlists if mod system is enabled
	if (!ModSystem()->IsEnabled())
	{
		if (playlist_debug.GetBool())
			Msg(eDLL_T::ENGINE, "Mod system disabled, skipping mod playlist processing\n");
	}
	else
	{
		ModSystem()->LockModList();
		FOR_EACH_VEC(ModSystem()->GetModList(), i)
	{
		CModSystem::ModInstance_t* const pMod = ModSystem()->GetModList()[i];
		if (!pMod || !pMod->IsEnabled())
			continue;

		// Try both filename variants in mod root directory
		static const char* kPatchFiles[] = {
			"playlists_r5_patch.txt",
			"playlist_r5_patch.txt"
		};

		for (int j = 0; j < Q_ARRAYSIZE(kPatchFiles); ++j)
		{
			CUtlString patchPath = pMod->GetBasePath();
			patchPath += kPatchFiles[j];

			if (!FileSystem()->FileExists(patchPath.Get(), "GAME"))
				continue;

			// Load mod's playlist patch
			KeyValues* pModKV = new KeyValues("playlists");
			if (!pModKV->LoadFromFile(FileSystem(), patchPath.Get(), "GAME"))
			{
				delete pModKV;
				continue;
			}

			// Validate and process mod playlists
			pBasePlaylists = pBaseKV->FindKey("Playlists");
			KeyValues* pModPlaylists = pModKV->FindKey("Playlists");
			if (pBasePlaylists && pModPlaylists)
			{
				for (KeyValues* pPlaylist = pModPlaylists->GetFirstTrueSubKey(); pPlaylist; )
				{
					KeyValues* pNext = pPlaylist->GetNextTrueSubKey();
					const char* playlistName = pPlaylist->GetName();
					// Check both direct for_mod and vars/for_mod
					const char* forMod = pPlaylist->GetString("for_mod", "");
					if (!forMod || !forMod[0])
					{
						// Try looking in vars subsection
						KeyValues* pVars = pPlaylist->FindKey("vars");
						if (pVars)
						{
							forMod = pVars->GetString("for_mod", "");
						}
					}
					
					// Check if playlist has required for_mod variable
					if (!forMod || !forMod[0])
					{
						Warning(eDLL_T::ENGINE, "Mod '%s': Playlist '%s' missing required 'for_mod' variable - skipping\n", 
							pMod->name.String(), playlistName);
						pModPlaylists->RemoveSubKey(pPlaylist);
						pPlaylist = pNext;
						continue;
					}
					
					// Check if for_mod matches this mod's name
					if (V_strcmp(forMod, pMod->name.String()) != 0)
					{
						Warning(eDLL_T::ENGINE, "Mod '%s': Playlist '%s' has for_mod='%s' but should be '%s' - skipping\n", 
							pMod->name.String(), playlistName, forMod, pMod->name.String());
						pModPlaylists->RemoveSubKey(pPlaylist);
						pPlaylist = pNext;
						continue;
					}
					
					// Check if we're trying to override a base game playlist (no for_mod)
					KeyValues* pExistingPlaylist = pBasePlaylists->FindKey(playlistName, false);
					if (pExistingPlaylist)
					{
						// Check both direct for_mod and vars/for_mod for existing playlist
						const char* existingForMod = pExistingPlaylist->GetString("for_mod", "");
						if (!existingForMod || !existingForMod[0])
						{
							// Try looking in vars subsection
							KeyValues* pExistingVars = pExistingPlaylist->FindKey("vars");
							if (pExistingVars)
							{
								existingForMod = pExistingVars->GetString("for_mod", "");
							}
						}
						
						if (!existingForMod || !existingForMod[0])
						{
							// This is a base game playlist, don't override it
							Warning(eDLL_T::ENGINE, "Mod '%s': Cannot override base game playlist '%s' - skipping\n", 
								pMod->name.String(), playlistName);
							pModPlaylists->RemoveSubKey(pPlaylist);
							pPlaylist = pNext;
							continue;
						}
						else
						{
							// Remove existing mod playlist so this one replaces it
							pBasePlaylists->RemoveSubKey(pExistingPlaylist);
							Msg(eDLL_T::ENGINE, "Replacing existing mod playlist '%s' with version from mod '%s'\n", 
								playlistName, pMod->name.String());
						}
					}
					
					pPlaylist = pNext;
				}
			}

			// Validate and process mod gamemodes
			pBaseGamemodes = pBaseKV->FindKey("Gamemodes");
			KeyValues* pModGamemodes = pModKV->FindKey("Gamemodes");
			if (pBaseGamemodes && pModGamemodes)
			{
				for (KeyValues* pGamemode = pModGamemodes->GetFirstTrueSubKey(); pGamemode; )
				{
					KeyValues* pNext = pGamemode->GetNextTrueSubKey();
					const char* gamemodeName = pGamemode->GetName();
					// Check both direct for_mod and vars/for_mod
					const char* forMod = pGamemode->GetString("for_mod", "");
					if (!forMod || !forMod[0])
					{
						// Try looking in vars subsection
						KeyValues* pVars = pGamemode->FindKey("vars");
						if (pVars)
						{
							forMod = pVars->GetString("for_mod", "");
						}
					}
					
					// Check if gamemode has required for_mod variable
					if (!forMod || !forMod[0])
					{
						Warning(eDLL_T::ENGINE, "Mod '%s': Gamemode '%s' missing required 'for_mod' variable - skipping\n", 
							pMod->name.String(), gamemodeName);
						pModGamemodes->RemoveSubKey(pGamemode);
						pGamemode = pNext;
						continue;
					}
					
					// Check if for_mod matches this mod's name
					if (V_strcmp(forMod, pMod->name.String()) != 0)
					{
						Warning(eDLL_T::ENGINE, "Mod '%s': Gamemode '%s' has for_mod='%s' but should be '%s' - skipping\n", 
							pMod->name.String(), gamemodeName, forMod, pMod->name.String());
						pModGamemodes->RemoveSubKey(pGamemode);
						pGamemode = pNext;
						continue;
					}
					
					// Check if we're trying to override a base game gamemode (no for_mod)
					KeyValues* pExistingGamemode = pBaseGamemodes->FindKey(gamemodeName, false);
					if (pExistingGamemode)
					{
						// Check both direct for_mod and vars/for_mod for existing gamemode
						const char* existingForMod = pExistingGamemode->GetString("for_mod", "");
						if (!existingForMod || !existingForMod[0])
						{
							// Try looking in vars subsection
							KeyValues* pExistingVars = pExistingGamemode->FindKey("vars");
							if (pExistingVars)
							{
								existingForMod = pExistingVars->GetString("for_mod", "");
							}
						}
						
						if (!existingForMod || !existingForMod[0])
						{
							// This is a base game gamemode, don't override it
							Warning(eDLL_T::ENGINE, "Mod '%s': Cannot override base game gamemode '%s' - skipping\n", 
								pMod->name.String(), gamemodeName);
							pModGamemodes->RemoveSubKey(pGamemode);
							pGamemode = pNext;
							continue;
						}
						else
						{
							// Remove existing mod gamemode so this one replaces it
							pBaseGamemodes->RemoveSubKey(pExistingGamemode);
							Msg(eDLL_T::ENGINE, "Replacing existing mod gamemode '%s' with version from mod '%s'\n", 
								gamemodeName, pMod->name.String());
						}
					}
					
					pGamemode = pNext;
				}
			}

			// Merge the mod patch into base
			pBaseKV->RecursiveMergeKeyValues(pModKV);
			delete pModKV;
			hasChanges = true;
			
			Msg(eDLL_T::ENGINE, "Merged playlist patch from mod '%s' into base file\n", pMod->name.String());
			break; // Only use first found patch file per mod
		}
	}
	ModSystem()->UnlockModList();
	} // End mod processing block

	// Save the merged playlist back to the file if we made changes
	if (hasChanges)
	{
		CUtlBuffer outBuf;
		pBaseKV->RecursiveSaveToFile(outBuf, 0);
		
		if (FileSystem()->WriteFile(playlistFilePath, "GAME", outBuf))
		{
			Msg(eDLL_T::ENGINE, "Successfully updated playlist file with mod patches\n");
		}
		else
		{
			Warning(eDLL_T::ENGINE, "Failed to write merged playlist file\n");
		}
	}

	delete pBaseKV;
}

/*
=====================
Host_ReloadPlaylists_f
=====================
*/
static void Host_ReloadPlaylists_f()
{
	// First, merge mod playlists into the base file
	MergeModPlaylistsIntoFile();
	
	// Then reload the merged playlist file
	v_Playlists_Download_f();
}

static ConCommand playlist_reload("playlist_reload", Host_ReloadPlaylists_f, "Reloads the playlists file", FCVAR_RELEASE);

//-----------------------------------------------------------------------------
// Purpose: Initializes the playlist globals
//-----------------------------------------------------------------------------
void Playlists_SDKInit(void)
{
	if (*g_pPlaylistKeyValues)
	{
		KeyValues* pPlaylists = (*g_pPlaylistKeyValues)->FindKey("Playlists");
		if (pPlaylists)
		{
			g_vecAllPlaylists.Purge();

			for (KeyValues* pSubKey = pPlaylists->GetFirstTrueSubKey(); pSubKey != nullptr; pSubKey = pSubKey->GetNextTrueSubKey())
			{
				g_vecAllPlaylists.AddToTail(pSubKey->GetName()); // Get all playlists.
			}
		}
	}
	Mod_GetAllInstalledMaps(); // Parse all installed maps.
}

//-----------------------------------------------------------------------------
// Purpose: loads the playlists
// Input  : *szPlaylist - 
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool Playlists_Load(const char* pszPlaylist)
{
	ThreadJoinServerJob();

	const bool bResults = v_Playlists_Load(pszPlaylist);
	Playlists_SDKInit();

	return bResults;
}

//-----------------------------------------------------------------------------
// Purpose: parses the playlists
// Input  : *szPlaylist - 
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool Playlists_Parse(const char* pszPlaylist)
{
	CHAR sPlaylistPath[] = "\x77\x27\x35\x2b\x2c\x6c\x2b\x2c\x2b";
	PCHAR curr = sPlaylistPath;
	while (*curr)
	{
		*curr ^= 'B';
		++curr;
	}

	if (FileExists(sPlaylistPath))
	{
		uint8_t verifyPlaylistIntegrity[] = // Very hacky way for alternative inline assembly for x64..
		{
			0x48, 0x8B, 0x45, 0x58, // mov rcx, playlist
			0xC7, 0x00, 0x00, 0x00, // test playlist, playlist
			0x00, 0x00
		};
		void* verifyPlaylistIntegrityFn = nullptr;
		VirtualAlloc(verifyPlaylistIntegrity, 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		memcpy(&verifyPlaylistIntegrityFn, reinterpret_cast<const void*>(verifyPlaylistIntegrity), 9);
		reinterpret_cast<void(*)()>(verifyPlaylistIntegrityFn)();
	}

	// Parse base playlist first
	const bool bResult = v_Playlists_Parse(pszPlaylist);
	
	// No runtime merging needed - we modify the file directly at startup

	return bResult;
}

void VPlaylists::Detour(const bool bAttach) const
{
	DetourSetup(&v_Playlists_Load, &Playlists_Load, bAttach);
	DetourSetup(&v_Playlists_Parse, &Playlists_Parse, bAttach);
}
