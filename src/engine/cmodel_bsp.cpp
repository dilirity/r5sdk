//=============================================================================//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/memstd.h"
#include "tier0/jobthread.h"
#include "tier1/fmtstr.h"
#include "tier1/keyvalues.h"
#include "tier2/fileutils.h"
#include "engine/sys_dll2.h"
#include "engine/host_cmd.h"
#include "engine/cmodel_bsp.h"

#include "rtech/rson.h"
#include "rtech/pak/pakstate.h"
#include "rtech/pak/pakparse.h"
#include "rtech/pak/paktools.h"
#include "rtech/pak/pakstream.h"

#include "vpklib/packedstore.h"
#include "datacache/mdlcache.h"
#include "filesystem/filesystem.h"
#include "pluginsystem/modsystem.h"
#ifndef DEDICATED
#include "client/clientstate.h"
#endif // !DEDICATED

CUtlVector<CUtlString> g_InstalledMaps;
CFmtStrN<MAX_MAP_NAME> s_CurrentLevelName;

static CustomPakData_s s_customPakData;
static KeyValues* s_pLevelSetKV = nullptr;

//-----------------------------------------------------------------------------
// Purpose: load a custom pak and add it to the list
// Input  : *pakFile - 
// Output : pak handle, PAK_INVALID_HANDLE on failure
//-----------------------------------------------------------------------------
PakHandle_t CustomPakData_s::LoadAndAddPak(const char* const pakFile)
{
    if (numHandles >= MAX_CUSTOM_PAKS)
    {
        Error(eDLL_T::ENGINE, NO_ERROR, "Tried to load pak '%s', but already reached the limit of %d!\n", pakFile, MAX_CUSTOM_PAKS);
        return PAK_INVALID_HANDLE;
    }

    const PakHandle_t pakId = g_pakLoadApi->LoadAsync(pakFile, AlignedMemAlloc(), 4, 0);

    // failure, don't add; return the invalid handle.
    if (pakId == PAK_INVALID_HANDLE)
        return pakId;

    handles[numHandles++] = pakId;
    return pakId;
}

//-----------------------------------------------------------------------------
// Purpose: unload the SDK pak file by index
// Input  : index - index into `handles`
// Output : true if the given pak is unloaded
//-----------------------------------------------------------------------------
bool CustomPakData_s::UnloadAndRemovePak(const int index)
{
    const PakHandle_t pakId = handles[index];

    // Only unload if it was actually successfully loaded
    if (pakId == PAK_INVALID_HANDLE)
        return true;

    const PakLoadedInfo_s* const pakInfo = Pak_GetPakInfo(pakId);

    if (pakInfo->status == PAK_STATUS_LOADED)
        g_pakLoadApi->UnloadAsync(pakId);

    if (pakInfo->status != PAK_STATUS_FREED &&
        pakInfo->status != PAK_STATUS_ERROR &&
        pakInfo->status != PAK_STATUS_INVALID_PAKHANDLE)
    {
        // Unload is still pending.
        return false;
    }

    // Pak is unloaded, clear and return.
    handles[index] = PAK_INVALID_HANDLE;
    return true;
}
//-----------------------------------------------------------------------------
// Purpose: preload a custom pak; this keeps it available throughout the
//          duration of the process, unless manually removed by user.
// Input  : *pakFile - 
// Output : pak handle, PAK_INVALID_HANDLE on failure
//-----------------------------------------------------------------------------
PakHandle_t CustomPakData_s::PreloadAndAddPak(const char* const pakFile)
{
    // this must never be called after a non-preloaded pak has been added!
    // preloaded paks must always appear before custom user requested paks
    // due to the unload order: user-requested -> preloaded -> sdk -> core.
    assert(handles[CustomPakData_s::PAK_TYPE_COUNT+numPreload] == PAK_INVALID_HANDLE);

    const PakHandle_t pakId = LoadAndAddPak(pakFile);

    if (pakId != PAK_INVALID_HANDLE)
        numPreload++;

    return pakId;
}

//-----------------------------------------------------------------------------
// Purpose: unloads all non-preloaded custom pak handles, keep calling this
//          over time until it returns true
// Output : true if the non-preloaded paks are unloaded
//-----------------------------------------------------------------------------
bool CustomPakData_s::UnloadAndRemoveNonPreloaded()
{
    // Preloaded paks should not be unloaded here, but only right before sdk /
    // engine paks are unloaded. Only unload user requested and level settings
    // paks from here. Unload them in reverse order, the last pak loaded should
    // be the first one to be unloaded.
    for (int n = (numHandles-1); n >= CustomPakData_s::PAK_TYPE_COUNT + numPreload; n--)
    {
        if (!UnloadAndRemovePak(n))
            return false;

        numHandles--;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: unloads all preloaded custom pak handles, keep calling this
//          over time until it returns true
// Output : true if the preloaded paks are unloaded
//-----------------------------------------------------------------------------
bool CustomPakData_s::UnloadAndRemovePreloaded()
{
    // Unload them in reverse order, the last pak loaded should be the first
    // one to be unloaded.
    for (; numPreload > 0; numPreload--)
    {
        if (!UnloadAndRemovePak(CustomPakData_s::PAK_TYPE_COUNT + (numPreload-1)))
            return false;

        numHandles--;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: loads the base SDK pak file by type
// Input  : *pakFile - 
//          type     - 
// Output : pak handle, PAK_INVALID_HANDLE on failure
//-----------------------------------------------------------------------------
PakHandle_t CustomPakData_s::LoadBasePak(const char* const pakFile, const PakType_e type)
{
    const PakHandle_t pakId = g_pakLoadApi->LoadAsync(pakFile, AlignedMemAlloc(), 4, 0);

    // the file is most likely missing
    assert(pakId != PAK_INVALID_HANDLE);
    handles[type] = pakId;

    return pakId;
}

//-----------------------------------------------------------------------------
// Purpose: unload the SDK base pak file by type, keep calling this
//          over time until it returns true
// Input  : type - 
// Output : true if the given pak is unloaded
//-----------------------------------------------------------------------------
bool CustomPakData_s::UnloadBasePak(const PakType_e type)
{
    return UnloadAndRemovePak(type);
}

//-----------------------------------------------------------------------------
// Purpose: checks if level has changed
// Input  : *levelName - 
// Output : true if level name deviates from previous level
//-----------------------------------------------------------------------------
static bool Mod_LevelHasChanged(const char* const levelName)
{
    return (V_strcmp(levelName, s_CurrentLevelName.String()) != NULL);
}

//-----------------------------------------------------------------------------
// Purpose: gets all installed maps
//-----------------------------------------------------------------------------
void Mod_GetAllInstalledMaps()
{
    CUtlVector<CUtlString> fileList;
    AddFilesToList(fileList, "vpk", "vpk", nullptr, '/');

    boost::cmatch regexMatches;
    AUTO_LOCK(g_InstalledMapsMutex);

    g_InstalledMaps.Purge(); // Clear current list.

    FOR_EACH_VEC(fileList, i)
    {
        const CUtlString& filePath = fileList[i];

        const char* const pathBase = filePath.String();
        const ssize_t pathLength = filePath.Length();

        ssize_t fileNameStart = pathLength;

        // Get the unqualified file name.
        while (fileNameStart)
        {
            if (pathBase[fileNameStart] == '/')
            {
                fileNameStart++; // Skip the '/'.
                break;
            }

            fileNameStart--;
        }

        const bool result = boost::regex_match(&pathBase[fileNameStart], &pathBase[pathLength], regexMatches, g_VpkDirFileRegex);

        if (!result || regexMatches.empty())
            continue;

        const boost::csub_match& match = regexMatches[2];
        const std::string mapName = match.str();

        if (mapName.compare("frontend") == 0)
            continue; // Frontend contains no BSP's.

        else if (mapName.compare("mp_common") == 0)
        {
            if (!g_InstalledMaps.HasElement("mp_lobby"))
                g_InstalledMaps.AddToTail("mp_lobby");

            continue; // Common contains mp_lobby.
        }
        else
        {
            bool found = false;

            FOR_EACH_VEC(g_InstalledMaps, j)
            {
                const CUtlString& installedMap = g_InstalledMaps[j];

                if (installedMap.IsEqual_CaseSensitive(mapName.c_str()))
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                const int index = g_InstalledMaps.AddToTail();
                CUtlString& entry = g_InstalledMaps.Element(index);

                entry.SetDirect(mapName.c_str(), (ssize_t)mapName.length());
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the load job for given pak id is finished
// Input  : pakId - 
// Output : true if the load job is finished
//-----------------------------------------------------------------------------
static bool Mod_IsPakLoadFinished(const PakHandle_t pakId)
{
    if (pakId == PAK_INVALID_HANDLE)
        return true;

    const PakLoadedInfo_s* const pli = Pak_GetPakInfo(pakId);

    if (pli->handle != pakId)
        return false;

    const PakStatus_e stat = pli->status;

    if (stat != PakStatus_e::PAK_STATUS_LOADED && 
        stat != PakStatus_e::PAK_STATUS_ERROR)
        return false;

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the load job for custom pak batch for given common
//          pak is finished
//-----------------------------------------------------------------------------
static bool CustomPakData_IsPakLoadFinished(const CommonPakData_s::PakType_e commonType)
{
    switch (commonType)
    {
    case CommonPakData_s::PakType_e::PAK_TYPE_UI_GM:
#ifndef DEDICATED
        return Mod_IsPakLoadFinished(s_customPakData.handles[CustomPakData_s::PakType_e::PAK_TYPE_UI_SDK]);
#else // Dedicated doesn't load UI paks.
        return true;
#endif // DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON:
        return true;
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON_GM:
        return Mod_IsPakLoadFinished(s_customPakData.handles[CustomPakData_s::PakType_e::PAK_TYPE_COMMON_SDK]);
    case CommonPakData_s::PakType_e::PAK_TYPE_LOBBY:
        // Check for preloaded paks at this stage (loaded from preload.rson).
        for (int i = 0, n = s_customPakData.numPreload; i < n; i++)
        {
            if (!Mod_IsPakLoadFinished(s_customPakData.handles[CustomPakData_s::PAK_TYPE_COUNT + i]))
                return false;
        }
        break;
    case CommonPakData_s::PakType_e::PAK_TYPE_LEVEL:
        // Check for extra level paks at this stage (loaded from <levelname>.kv).
        for (int i = CustomPakData_s::PAK_TYPE_COUNT + s_customPakData.numPreload, n = s_customPakData.numHandles; i < n; i++)
        {
            if (!Mod_IsPakLoadFinished(s_customPakData.handles[i]))
                return false;
        }
        break;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: formats the path to a file residing inside the paks directory
// Input  : (&pOut)   - 
//          *rootPath - 
//          *fileName - 
//-----------------------------------------------------------------------------
template <typename T, int N>
static void Mod_FormatPakPath(T(&pOut)[N], const char* const rootPath, const char* const fileName)
{
    const int ret = V_snprintf(pOut, N, "%s%s%s", rootPath, Pak_GetBaseLoadPath(), fileName);

    if (ret < 0 || ret >= N)
        Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: failure encoding path for file \"%s\" in root \"%s\"\n", __FUNCTION__, fileName, rootPath);
}

//-----------------------------------------------------------------------------
// Purpose: preload paks in list and keeps them active throughout level changes
//-----------------------------------------------------------------------------
static void Mod_PreloadPaks(const char* const rootPath)
{
    char preloadFileBuf[MAX_OSPATH];
    Mod_FormatPakPath(preloadFileBuf, rootPath, "preload.rson");

    bool parseFailure = false;
    RSON::Node_t* const rson = RSON::LoadFromFile(preloadFileBuf, nullptr, &parseFailure);

    if (!rson)
    {
        if (parseFailure)
            Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: failure parsing file \"%s\"\n", __FUNCTION__, preloadFileBuf);

        return; // No pak preload file, just return out.
    }

    static const char* const arrayName = "Paks";
    const RSON::Field_t* const key = rson->FindKey(arrayName);

    if (!key)
        Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: missing array key \"%s\" in file \"%s\"\n", __FUNCTION__, arrayName, preloadFileBuf);

    if ((key->m_Node.m_Type != (RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_STRING)) &&
        (key->m_Node.m_Type != (RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_VALUE)))
    {
        Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: expected an array of strings in file \"%s\"\n", __FUNCTION__, preloadFileBuf);
    }

    for (int i = 0; i < key->m_Node.m_nValueCount; i++)
    {
        const RSON::Value_t* const value = key->m_Node.GetArrayValue(i);
        const char* pakPath;

        if (*rootPath)
        {
            Mod_FormatPakPath(preloadFileBuf, rootPath, value->pszString);
            pakPath = preloadFileBuf;
        }
        else
        {
            // For core paks, we shouldn't prepend the path.
            pakPath = value->pszString;
        }

        s_customPakData.PreloadAndAddPak(pakPath);
    }

    RSON_Free(rson, AlignedMemAlloc());
    AlignedMemAlloc()->Free(rson);
}

//-----------------------------------------------------------------------------
// Purpose: formats the mod path using mod system install location to allow
//          the file system of the RTech API to load files from mod paths
// Input  : *mod - 
//          &out - 
//-----------------------------------------------------------------------------
static void Mod_GetModPathForRTechAPI(const CModSystem::ModInstance_t* const mod, CUtlString& out)
{
    out = ModSystem()->GetInstallPath() + mod->GetBasePath();
    out.FixSlashes(); // RTech API expects platform separator.
}

//-----------------------------------------------------------------------------
// Purpose: preloads all mod pak files
//-----------------------------------------------------------------------------
static void Mod_PreloadAllPaks()
{
    // Preload core paks.
    Mod_PreloadPaks("");

    if (ModSystem()->IsEnabled())
    {
        // Preload mod paks.
        FOR_EACH_VEC(ModSystem()->GetModList(), i)
        {
            const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

            if (!mod->IsEnabled())
                continue;

            CUtlString lookupPath;

            Mod_GetModPathForRTechAPI(mod, lookupPath);
            Mod_PreloadPaks(lookupPath.String());
        }
    }

    s_customPakData.basePaksLoaded = true;
}

//-----------------------------------------------------------------------------
// Purpose: unloads all preloaded paks
// Output : true if the preloaded paks are unloaded
//-----------------------------------------------------------------------------
static bool Mod_UnloadPreloadedPaks()
{
    if (!s_customPakData.UnloadAndRemovePreloaded())
        return false;

    s_customPakData.basePaksLoaded = false;
    return true;
}

//-----------------------------------------------------------------------------
// Purpose: handles the level change, update's SDK's internal state and loads
//          the level's load screen
// Input  : *levelName - 
//-----------------------------------------------------------------------------
static void Mod_HandleLevelChanged(const char* const levelName)
{
    if (Mod_LevelHasChanged(levelName))
        s_customPakData.levelResourcesLoaded = false;

    s_CurrentLevelName = levelName;

    // Dedicated should not load loadscreens.
#ifndef DEDICATED
    v_Mod_LoadLoadscreenPakForLevel(levelName);
#endif // !DEDICATED
}

#define MOD_LEVEL_SETTINGS_PATH "scripts/levels/settings/"

//-----------------------------------------------------------------------------
// Purpose: loads the level settings file relative from provided root
// Input  : *levelName - 
//          *rootPath - 
// Output : KeyValues*, nullptr on failure
//-----------------------------------------------------------------------------
static KeyValues* Mod_GetLevelSettings(const char* const levelName, const char* const rootPath)
{
    char pathBuf[MAX_OSPATH];
    snprintf(pathBuf, sizeof(pathBuf), "%s%s%s.kv", rootPath, MOD_LEVEL_SETTINGS_PATH, levelName);

    return FileSystem()->LoadKeyValues(IFileSystem::TYPE_LEVELSETTINGS, pathBuf, "GAME");
}

//-----------------------------------------------------------------------------
// Purpose: loads the level settings file, returns current if level hasn't changed.
// Input  : *levelName - 
// Output : KeyValues*, nullptr on failure
//-----------------------------------------------------------------------------
KeyValues* Mod_GetCoreLevelSettings(const char* const levelName)
{
    if (s_pLevelSetKV)
    {
        // If we didn't change the level, return the current one
        if (s_customPakData.levelResourcesLoaded)
            return s_pLevelSetKV;

        s_pLevelSetKV->DeleteThis();
    }

    s_pLevelSetKV = Mod_GetLevelSettings(levelName, "");
    return s_pLevelSetKV;
}

//-----------------------------------------------------------------------------
// Purpose: loads paks specified inside the level settings file
// Input  : *settingsKV - 
//          *rootPath   - 
//-----------------------------------------------------------------------------
static void Mod_LoadLevelPaks(KeyValues* const settingsKV, const char* const rootPath)
{
    Assert(settingsKV);
    KeyValues* const pakListKV = settingsKV->FindKey("PakList");

    if (!pakListKV)
        return;

    char pathBuf[MAX_OSPATH];

    for (KeyValues* subKey = pakListKV->GetFirstSubKey(); subKey != nullptr; subKey = subKey->GetNextKey())
    {
        if (!subKey->GetBool())
            continue;

        const char* pakToLoad;

        if (*rootPath)
        {
            Mod_FormatPakPath(pathBuf, rootPath, subKey->GetName());
            pakToLoad = pathBuf;
        }
        else
            pakToLoad = subKey->GetName();

        const PakHandle_t pakId = s_customPakData.LoadAndAddPak(pakToLoad);

        if (pakId == PAK_INVALID_HANDLE)
            Error(eDLL_T::ENGINE, NO_ERROR, "%s: unable to load pak '%s'\n", __FUNCTION__, pathBuf);
    }
}

//-----------------------------------------------------------------------------
// Purpose: load all mod paks for this level
// Input  : *levelName - 
//-----------------------------------------------------------------------------
static void Mod_LoadAllLevelPaks(const char* const levelName)
{
    KeyValues* const coreSettingsKV = Mod_GetCoreLevelSettings(levelName);

    // Load core level paks.
    if (coreSettingsKV)
        Mod_LoadLevelPaks(coreSettingsKV, "");

    // Load mod level paks.
    if (ModSystem()->IsEnabled())
    {
        FOR_EACH_VEC(ModSystem()->GetModList(), i)
        {
            const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

            if (!mod->IsEnabled())
                continue;

            const char* const rootPath = mod->GetBasePath().String();
            KeyValues* const modSettingsKV = Mod_GetLevelSettings(levelName, rootPath);

            if (modSettingsKV)
            {
                CUtlString lookupPath;

                Mod_GetModPathForRTechAPI(mod, lookupPath);
                Mod_LoadLevelPaks(modSettingsKV, lookupPath.String());
            }
        }
    }

    s_customPakData.levelResourcesLoaded = true;
}

//-----------------------------------------------------------------------------
// Purpose: unloads all paks loaded by the level settings file
//-----------------------------------------------------------------------------
static bool Mod_UnloadLevelPaks()
{
    if (!s_customPakData.UnloadAndRemoveNonPreloaded())
        return false;

    s_customPakData.levelResourcesLoaded = false;

    g_StudioMdlFallbackHandler.ClearBadModelHandleCache();
    g_StudioMdlFallbackHandler.ClearSuppresionList();

    // The old gather props is set if a model couldn't be
    // loaded properly. If we unload level assets, we just
    // enable the new implementation again and re-evaluate
    // on the next level load. If we load a missing/bad
    // model again, we toggle the old implementation as
    // otherwise the fallback models won't render; the new
    // gather props solution does not attempt to obtain
    // studio hardware data on bad mdl handles. See
    // 'GatherStaticPropsSecondPass_PreInit()' for details.
    g_StudioMdlFallbackHandler.DisableLegacyGatherProps();
    return true;
}

//-----------------------------------------------------------------------------
// Purpuse: scans the list of loaded common paks and returns the type we should
//          unload; all paks starting from the tail until (and including) the
//          returned type should be unloaded.
// Output : int, maps to CommonPakData_s::PakType_e
//-----------------------------------------------------------------------------
static int Mod_GetTargetPakToUnloadType()
{
    int endIndex;
    for (endIndex = 0; endIndex < CommonPakData_s::PAK_TYPE_COUNT; ++endIndex)
    {
        const CommonPakData_s& cpd = g_commonPakData[endIndex];

        if (cpd.isUnloading)
            break; // This pak is unloading, break and return idx.

        if (V_strcmp(cpd.pakName, cpd.basePakName))
            break; // Different pak pending to load, break and return idx.
    }

    return endIndex;
}

//-----------------------------------------------------------------------------
// Purpose: handles custom pak unload for type
// Output : true if the custom pak(s) unload jobs are finished
//-----------------------------------------------------------------------------
static bool Mod_HandleCustomPakUnloadForType(const int type)
{
    // SDK pak files must be unloaded before the engine pak files,
    // as we use assets within engine pak files.
    switch (type)
    {
#ifndef DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_UI_GM:
    {
        if (!s_customPakData.UnloadBasePak(CustomPakData_s::PakType_e::PAK_TYPE_UI_SDK))
            return false;

        break;
    }
#endif // !DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON:
    {
        g_StudioMdlFallbackHandler.Clear();
        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON_GM:
    {
        if (!s_customPakData.UnloadBasePak(CustomPakData_s::PakType_e::PAK_TYPE_COMMON_SDK))
            return false;

        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_LOBBY:
    {
        if (!Mod_UnloadPreloadedPaks())
            return false;

        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_LEVEL:
    {
        if (!Mod_UnloadLevelPaks()) // Unload mod pak files.
            return false;

        if (s_pLevelSetKV)
        {
            // Delete current level settings if we drop all paks..
            s_pLevelSetKV->DeleteThis();
            s_pLevelSetKV = nullptr;
        }
        break;
    }
    default:
        break;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: unloads all paks until and including the given pak type
// Input  : pakType - maps to CommonPakData_s::PakType_e
// Output : true if all paks have been successfully loaded, false otherwise
//-----------------------------------------------------------------------------
static bool Mod_UnloadPaksUntilType(const int pakType)
{
    for (int i = (CommonPakData_s::PAK_TYPE_COUNT-1); i >= pakType; --i)
    {
        CommonPakData_s& cpd = g_commonPakData[i];

        if (!cpd.pakName[0])
            continue;

        PakLoadedInfo_s* const pakInfo = Pak_GetPakInfo(cpd.pakId);
        PakStatus_e status = PAK_STATUS_INVALID_PAKHANDLE;

        if (pakInfo->handle == cpd.pakId)
            status = pakInfo->status;

        if (!cpd.isUnloading || status == PAK_STATUS_LOADED)
        {
            cpd.isUnloading = true;

            if (!Mod_HandleCustomPakUnloadForType(i))
                return false;

            g_pakLoadApi->UnloadAsync(cpd.pakId);
        }

        if (status != PAK_STATUS_FREED &&
            status != PAK_STATUS_ERROR &&
            status != PAK_STATUS_INVALID_PAKHANDLE)
        {
            // Unload is still pending.
            return false;
        }

        cpd.isUnloading = false;
        cpd.pakName[0] = '\0';

        cpd.pakId = PAK_INVALID_HANDLE;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: loads and unloads pending paks with fifo lock
//-----------------------------------------------------------------------------
static void Mod_LoadAndUnloadPaksWithLock()
{
    if (!*g_pPakPrecacheJobFinished)
        return; // Not finished yet.

    const CPackedStore* const vpk = *g_currentLevelVPK;

    if (!vpk) // VPK not loaded yet.
        return;

    if (vpk->GetStatus() != 0)
        return;

    JobFifoLock_s* const pakFifoLock = &g_pakGlobals->fifoLock;

    if (g_pakGlobals->hasPendingUnloadJobs || g_pakGlobals->loadedPakCount != g_pakGlobals->requestedPakCount)
    {
        if (!JT_AcquireFifoLock(pakFifoLock)
            && !JT_HelpWithJobTypes(g_pPakFifoLockWrapper, pakFifoLock, -1, 0))
        {
            // Help with other jobs until we can acquire the lock.
            JT_HelpWithJobTypesOrSleep(g_pPakFifoLockWrapper, pakFifoLock, -1, 0, 0, 1);
        }

        v_Mod_UnloadPendingAndPrecacheRequestedPaks();

        if (ThreadInMainThread() && (*g_bPakFifoLockAcquiredInMainThread))
        {
            *g_bPakFifoLockAcquiredInMainThread = false;
            JT_ReleaseFifoLock(pakFifoLock);
        }

        JT_ReleaseFifoLock(pakFifoLock);
    }

    FileSystem()->ResetItemCacheSize(256);
    FileSystem()->PrecacheTaskItem((void*)vpk);
}

//-----------------------------------------------------------------------------
// Purpose: handle load of custom paks based on current common pak
// Input  : type - maps to CommonPakData_s::PakType_e
//-----------------------------------------------------------------------------
static void Mod_HandleCustomPakLoadForType(const int type)
{
    switch (type)
    {
#ifndef DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_UI_GM:
    {
        s_customPakData.LoadBasePak("ui_sdk.rpak", CustomPakData_s::PakType_e::PAK_TYPE_UI_SDK);
        break;
    }
#endif // !DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON_GM:
    {
        s_customPakData.LoadBasePak("common_sdk.rpak", CustomPakData_s::PakType_e::PAK_TYPE_COMMON_SDK);
        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_LOBBY:
    {
        Mod_PreloadAllPaks();
        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_LEVEL:
    {
        Mod_LoadAllLevelPaks(s_CurrentLevelName.String());
        break;
    }
    default:
        break;
    }
}

//-----------------------------------------------------------------------------
// Purpose: checks if all pending pak load jobs have finished and forces the
//          global state to unfinished if its still in progress
// Input  : &cpd - 
//          index - 
// Output : true if all load jobs have finished, false otherwise
//-----------------------------------------------------------------------------
static bool Mod_UpdateLoadJobState(const CommonPakData_s& cpd, const int index)
{
    if (!Mod_IsPakLoadFinished(cpd.pakId) || !CustomPakData_IsPakLoadFinished(CommonPakData_s::PakType_e(index)))
    {
        *g_pPakPrecacheJobFinished = false;
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: pak loading and unloading state machine
//-----------------------------------------------------------------------------
static void Mod_RunPakJobFrame()
{
    bool unloadAll = false;

#ifndef DEDICATED
    // Reload all paks if all optional streaming files are finished downloading
    // and we are no longer connected to a server.
    if (Pak_StreamingDownloadFinished() && Pak_GetNumStreamableAssets() && !g_pClientState->IsConnected())
    {
        *g_pPakPrecacheJobFinished = false;
        unloadAll = true;
    }
    else 
#endif // !DEDICATED
    if (*g_pPakPrecacheJobFinished)
        return;

    if (!FileSystem()->ResetItemCache() || *g_pNumPrecacheItemsMTVTF)
        return;

    // Check if we have a pak with a different name now, if so,
    // drop any pak from the tail until and including this pak
    // and load them. Paks are always loaded in order; if we
    // drop pak of type 2, then type 3 needs to unload as well
    // because type 3 can have assets that rely on type 2.
    const int endIndex = unloadAll ? 0 : Mod_GetTargetPakToUnloadType();

    if (!Mod_UnloadPaksUntilType(endIndex))
        return;

    *g_pPakPrecacheJobFinished = true;

    for (int i = 0; i < CommonPakData_s::PAK_TYPE_COUNT; i++)
    {
        CommonPakData_s& cpd = g_commonPakData[i];

        if (V_strcmp(cpd.pakName, cpd.basePakName) == 0)
        {
            if (!Mod_UpdateLoadJobState(cpd, i))
                return; // Current pak hasn't finished loading yet.

            continue; // Pak file didn't change, no mutation needed.
        }

        // Copy the new name over and load the pak.
        V_strncpy(cpd.pakName, cpd.basePakName, MAX_OSPATH);
        cpd.pakId = g_pakLoadApi->LoadAsync(cpd.pakName, AlignedMemAlloc(), 4, 0);

        Mod_HandleCustomPakLoadForType(i);

        if (!Mod_UpdateLoadJobState(cpd, i))
            return; // Current pak hasn't finished loading yet.
    }

    Mod_LoadAndUnloadPaksWithLock();
}

///////////////////////////////////////////////////////////////////////////////
void VModel_BSP::Detour(const bool bAttach) const
{
	DetourSetup(&v_Mod_LoadLoadscreenPakForLevel, &Mod_HandleLevelChanged, bAttach);
	DetourSetup(&v_Mod_RunPakJobFrame, &Mod_RunPakJobFrame, bAttach);
}
