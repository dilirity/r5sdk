//=============================================================================//
//
// Purpose: async file loading, unloading and the management thereof
//
//=============================================================================//
#include "rtech/ipakfile.h"
#include "rtech/pak/pakstate.h"
#include "rtech/pak/paktools.h"
#include "pluginsystem/modsystem.h"
#include "asyncio.h"

static ConVar async_debugchannel("async_debugchannel", "0", FCVAR_DEVELOPMENTONLY | FCVAR_ACCESSIBLE_FROM_THREADS, "Log async read handles created or destroyed with this channel ID", false, 0.f, false, 0.f, "0 = disabled, -1 = all");
static int s_fileHandleLogChannelIDs[ASYNC_MAX_FILE_HANDLES];

//----------------------------------------------------------------------------------
// helper for opening files
//----------------------------------------------------------------------------------
static HANDLE FS_Internal_OpenFile(const char* const fileToOpen)
{
    return CreateFileA(fileToOpen, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_SUPPORTS_GHOSTING, 0);
}

//----------------------------------------------------------------------------------
// open a file and add it to the async file handle array
//----------------------------------------------------------------------------------
int FS_OpenAsyncFile(const char* const filePath, const int logChannel, size_t* const fileSizeOut)
{
    const CHAR* fileToLoad = filePath;
    char overridePath[1024];

    // function can be called with null strings, for example if optional
    // streaming sets are missing; check for it
    if (fileToLoad && *fileToLoad)
    {
        // is this a pak file and do we have an override
        if (strstr(fileToLoad, Pak_GetBaseLoadPath()) &&
            Pak_FileOverrideExists(fileToLoad, overridePath, sizeof(overridePath)))
        {
            fileToLoad = overridePath;
        }
    }

    HANDLE hFile = FS_Internal_OpenFile(fileToLoad);
    CUtlString modLookupPath;

    if (hFile == INVALID_HANDLE_VALUE)
    {
        if (!ModSystem()->IsEnabled())
            return FS_ASYNC_FILE_INVALID;

        if (!fileToLoad || !*fileToLoad)
            return FS_ASYNC_FILE_INVALID;

        ModSystem()->LockModList();
        bool found = false;

        // Look for the file in our mods and obtain the first one we find.
        FOR_EACH_VEC(ModSystem()->GetModList(), i)
        {
            const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

            if (!mod->IsEnabled())
                continue;

            modLookupPath = mod->GetBasePath() + fileToLoad;
            const char* const pModLookupPath = modLookupPath.String();

            hFile = FS_Internal_OpenFile(pModLookupPath);

            if (hFile != INVALID_HANDLE_VALUE)
            {
                fileToLoad = pModLookupPath;
                found = true;

                break;
            }
        }

        ModSystem()->UnlockModList();

        if (!found)
            return FS_ASYNC_FILE_INVALID;
    }

    if (fileSizeOut)
    {
        // get the size of the file we just opened
        LARGE_INTEGER fileSize;

        if (GetFileSizeEx(hFile, &fileSize))
            *fileSizeOut = fileSize.QuadPart;
    }

    const int fileIdx = g_pAsyncFileSlotMgr->FindSlot();
    const int slotNum = (fileIdx & ASYNC_MAX_FILE_HANDLES_MASK);

    AsyncHandleTracker_s& tracker = g_pAsyncFileSlots[slotNum];

    tracker.slot = fileIdx;
    tracker.handle = hFile;
    tracker.refCount = 1;

    s_fileHandleLogChannelIDs[slotNum] = logChannel;
    const int selectedLogChannel = async_debugchannel.GetInt();

    if (selectedLogChannel && (selectedLogChannel == -1 || logChannel == selectedLogChannel))
        Msg(eDLL_T::RTECH, "%s: Opened file: '%s' to slot #%d\n", __FUNCTION__, fileToLoad, slotNum);

    return fileIdx;
}

//----------------------------------------------------------------------------------
// close a file and remove it from the async file handle array
//----------------------------------------------------------------------------------
void FS_CloseAsyncFile(const int fileHandle)
{
    const int slotNum = fileHandle & ASYNC_MAX_FILE_HANDLES_MASK;
    AsyncHandleTracker_s& tracker = g_pAsyncFileSlots[slotNum];

    if (ThreadInterlockedExchangeAdd(&tracker.refCount, -1) <= 1)
    {
        CloseHandle(tracker.handle);
        tracker.handle = INVALID_HANDLE_VALUE;

        g_pAsyncFileSlotMgr->FreeSlot(slotNum);
        const int selectedLogChannel = async_debugchannel.GetInt();

        if (selectedLogChannel && (selectedLogChannel == -1 || s_fileHandleLogChannelIDs[slotNum] == selectedLogChannel))
            Msg(eDLL_T::RTECH, "%s: Closed file from slot #%d\n", __FUNCTION__, slotNum);

        // TODO: StreamDB_Init has an inline version of FS_OpenAsyncFile, therefore
        // anything loaded there will never have the channel id's set. In order to
        // fix this, StreamDB_Init has to be rebuilt.
        //assert(s_fileHandleLogChannelIDs[slotNum] != 0);
        s_fileHandleLogChannelIDs[slotNum] = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////
void V_AsyncIO::Detour(const bool bAttach) const
{
    DetourSetup(&v_FS_OpenAsyncFile, &FS_OpenAsyncFile, bAttach);
    DetourSetup(&v_FS_CloseAsyncFile, &FS_CloseAsyncFile, bAttach);
}
