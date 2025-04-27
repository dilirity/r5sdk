//===============================================================================//
//
// Purpose: Client Sound Miles implementation
//
//===============================================================================//
#include "core/stdafx.h"
#include "tier0/fasttimer.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include "rtech/core/strutils.h"
#include "rtech/async/asyncio.h"
#include "rtech/pak/pakstate.h"
#include "filesystem/filesystem.h"
#include "pluginsystem/modsystem.h"
#include "ebisusdk/EbisuSDK.h"
#include "miles_impl.h"
#include "miles/src/sdk/shared/rrthreads2.h"

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------
static ConVar miles_debug("miles_debug", "0", FCVAR_DEVELOPMENTONLY, "Enables debug prints for the Miles Sound System", "1 = print; 0 (zero) = no print");
static ConVar miles_warnings("miles_warnings", "0", FCVAR_RELEASE, "Enables warning prints for the Miles Sound System", "1 = print; 0 (zero) = no print");

//-----------------------------------------------------------------------------
// Purpose: initializes the miles sound system
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
static bool CSOM_Initialize()
{
	const char* pszLanguage = HEbisuSDK_GetLanguage();
	const bool isDefaultLanguage = _stricmp(pszLanguage, MILES_DEFAULT_LANGUAGE) == 0;

	if (!isDefaultLanguage)
	{
		const bool useShipSound = !CommandLine()->FindParm("-devsound") || CommandLine()->FindParm("-shipsound");
		char baseStreamFilePath[MAX_OSPATH];

		V_snprintf(baseStreamFilePath, sizeof(baseStreamFilePath), "%s\\general_%s.mstr", useShipSound ? "audio\\ship" : "audio\\dev", pszLanguage);
		bool found = FileExists(baseStreamFilePath);

		if (!found && ModSystem()->IsEnabled())
		{
			ModSystem()->LockModList();

			// Check for it in our mods.
			FOR_EACH_VEC(ModSystem()->GetModList(), i)
			{
				const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

				if (!mod->IsEnabled())
					continue;

				const CUtlString modLookupPath = mod->GetBasePath() + baseStreamFilePath;
				const char* const pModLookupPath = modLookupPath.String();

				found = FileExists(pModLookupPath);

				if (found)
					break;
			}

			ModSystem()->UnlockModList();
		}

		if (!found)
		{
			// if the requested language for miles does not have a MSTR file present,
			// throw a non-fatal error and force MILES_DEFAULT_LANGUAGE as a fallback if
			// we are loading MILES_DEFAULT_LANGUAGE and the file is still not found, we
			// can let it hit the regular engine error, since that is not recoverable.
			Error(eDLL_T::AUDIO, NO_ERROR, "%s: attempted to load language '%s' but the required streaming source file (%s) was not found, falling back to '%s'...\n",
				__FUNCTION__, pszLanguage, baseStreamFilePath, MILES_DEFAULT_LANGUAGE);

			pszLanguage = MILES_DEFAULT_LANGUAGE;
		}

		miles_language->SetValue(pszLanguage);
	}

	Msg(eDLL_T::AUDIO, "%s: initializing MSS with language: '%s'\n", __FUNCTION__, pszLanguage);
	CFastTimer initTimer;

	initTimer.Start();
	const bool bResult = v_CSOM_Initialize();
	initTimer.End();

	Msg(eDLL_T::AUDIO, "%s: %s ('%f' seconds)\n", __FUNCTION__, bResult ? "success" : "failure", initTimer.GetDuration().GetSeconds());
	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: logs debug output emitted from the Miles Sound System
// Input  : nLogLevel - 
//          pszMessage - 
//-----------------------------------------------------------------------------
static void CSOM_LogFunc(int64_t nLogLevel, const char* pszMessage)
{
	Msg(eDLL_T::AUDIO, "%s\n", pszMessage);
	v_CSOM_LogFunc(nLogLevel, pszMessage);
}

//-----------------------------------------------------------------------------
// Purpose: runs the event queue
//-----------------------------------------------------------------------------
void MilesQueueEventRun(Miles::Queue* queue, const char* eventName)
{
	if(miles_debug.GetBool())
		Msg(eDLL_T::AUDIO, "%s: running event: '%s'\n", __FUNCTION__, eventName);

	v_MilesQueueEventRun(queue, eventName);
}

//-----------------------------------------------------------------------------
// Purpose: patches miles banks
//-----------------------------------------------------------------------------
void MilesBankPatch(Miles::Bank* bank, char* streamPatch, char* localizedStreamPatch)
{
	if (miles_debug.GetBool())
	{
		Msg(eDLL_T::AUDIO,
			"%s: patching bank \"%s\". stream patches: \"%s\", \"%s\"\n",
			__FUNCTION__,
			bank->GetBankName(),
			V_UnqualifiedFileName(streamPatch), V_UnqualifiedFileName(localizedStreamPatch)
		);
	}

	const Miles::BankHeader_t* header = bank->GetHeader();

	if (header->bankIndex >= header->project->bankCount)
		Error(eDLL_T::AUDIO, EXIT_FAILURE,
			"%s: attempted to patch bank \"%s\" that identified itself as bank #%i, project expects a highest index of #%i\n",
			__FUNCTION__,
			bank->GetBankName(),
			header->bankIndex,
			header->project->bankCount - 1
		);

	v_MilesBankPatch(bank, streamPatch, localizedStreamPatch);
}

//-----------------------------------------------------------------------------
// Purpose: adds an audio event to the queue
//-----------------------------------------------------------------------------
static void CSOM_AddEventToQueue(const char* eventName)
{
	if (miles_debug.GetBool())
		Msg(eDLL_T::AUDIO, "%s: queuing audio event '%s'\n", __FUNCTION__, eventName);

	v_CSOM_AddEventToQueue(eventName);

	if (miles_warnings.GetBool())
	{
		if (g_milesGlobals->queuedEventHash == 1)
			Warning(eDLL_T::AUDIO, "%s: failed to add event to queue; invalid event name '%s'\n", __FUNCTION__, eventName);

		if (g_milesGlobals->queuedEventHash == 2)
			Warning(eDLL_T::AUDIO, "%s: failed to add event to queue; event '%s' not found.\n", __FUNCTION__, eventName);
	}
};

//-----------------------------------------------------------------------------
// Purpose: close and reset the CSOM async file instance
//-----------------------------------------------------------------------------
static void CSOM_CloseAsyncFile(CSOM_AsyncFile_s* const asyncFile)
{
	asyncFile->asyncRequestId = 0;
	asyncFile->fileName[0] = '\0';
	FS_CloseAsyncFile(asyncFile->fileHandle);
	asyncFile->fileHandle = FS_ASYNC_FILE_INVALID;
	asyncFile->readOffset = 0;
	asyncFile->fileSize = 0;
}

//-----------------------------------------------------------------------------
// Structure for each live file instance
//-----------------------------------------------------------------------------
struct CSOM_FileInfo_s
{
	char fileName[256];
	size_t fileSize;
	int fileHandle;
};

#define CSOM_MAX_OPENED_FILES 32

static CSOM_FileInfo_s s_milesFileInfos[CSOM_MAX_OPENED_FILES];
static size_t s_numMilesFilesOpened = 0;

//-----------------------------------------------------------------------------
// Purpose: finds the file handle for given name, opens it if not found
//-----------------------------------------------------------------------------
static int CSOM_MilesAsync_OpenOrFindFile(const char* const fileName, size_t& outFileSize)
{
	if (s_numMilesFilesOpened)
	{
		// Find the file.
		CSOM_FileInfo_s* infoIt = s_milesFileInfos;
		size_t currIdx = 0;
		bool notFound = false; // If true, will try and open the file.

		while (V_strcmp(fileName, infoIt->fileName))
		{
			++currIdx;
			++infoIt;

			if (currIdx == s_numMilesFilesOpened)
			{
				notFound = true;
				break;
			}
		}

		if (!notFound)
		{
			outFileSize = infoIt->fileSize;
			g_pakLoadApi->IncrementAsyncFileRefCount(infoIt->fileHandle);

			return infoIt->fileHandle;
		}
	}

	if (s_numMilesFilesOpened == CSOM_MAX_OPENED_FILES)
		return FS_ASYNC_FILE_INVALID; // Max opened files reached.

	// Open the file.
	CSOM_FileInfo_s* const info = &s_milesFileInfos[s_numMilesFilesOpened++];
	V_strncpy(info->fileName, fileName, sizeof(info->fileName));

	info->fileHandle = FS_OpenAsyncFile(fileName, 4, &info->fileSize);

	if (info->fileHandle == FS_ASYNC_FILE_INVALID)
		Error(eDLL_T::AUDIO, EXIT_FAILURE, "%s( \"%s\" ) failed to open file; try resyncing\n", __FUNCTION__, fileName);

	outFileSize = info->fileSize;
	g_pakLoadApi->IncrementAsyncFileRefCount(info->fileHandle);

	return info->fileHandle;
}

//-----------------------------------------------------------------------------
// Purpose: returns the first free file slot index
//-----------------------------------------------------------------------------
static inline size_t CSOM_MilesAsync_GetFirstFreeFileSlot()
{
	size_t index = 0;

	// Scan the list.
	while (g_milesGlobals->asyncFiles[index].asyncRequestId)
		index++;

	return index;
}

//-----------------------------------------------------------------------------
// User structure for MilesAsyncRead
//-----------------------------------------------------------------------------
struct CSOM_AsyncRead_s
{
	int asyncFileHandle;
	bool shouldCloseFile;
	bool readFinished;
};

//-----------------------------------------------------------------------------
// Purpose: Miles async file read request handler; maps to internal callback of
//          MilesAsyncFileRead, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileRead(MilesAsyncRead* const request)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;
	CSOM_AsyncFile_s* asyncFile;

	if (request->RequestId)
	{
		asyncFile = &g_milesGlobals->asyncFiles[request->RequestId & CSOM_MAX_ASYNC_FILE_HANDLES_MASK];
	}
	else // New request, open the file.
	{
		const size_t asyncFileIdx = CSOM_MilesAsync_GetFirstFreeFileSlot();
		asyncFile = &g_milesGlobals->asyncFiles[asyncFileIdx];

		MilesSubFileInfo_s sfi; char fileNameStack[512];
		MilesGetSubFileInfo(fileNameStack, request->FileName, &sfi);

		R_UTF8_strncpy(asyncFile->fileName, sfi.filename, sizeof(asyncFile->fileName));

		asyncFile->fileSize = 0;
		asyncFile->fileHandle = CSOM_MilesAsync_OpenOrFindFile(sfi.filename, asyncFile->fileSize);

		asyncFile->readOffset = 0;
		asyncFile->readStart = sfi.start;

		if (sfi.size)
		{
			const size_t subFileSize = sfi.size + sfi.start;

			if (subFileSize < asyncFile->fileSize)
				asyncFile->fileSize = subFileSize;
		}

		// Give the request an unique ID with its slot index packed into it.
		const u64 asyncRequestId = asyncFileIdx + (++g_milesGlobals->asyncRequestIdGen * CSOM_MAX_ASYNC_FILE_HANDLES);

		asyncFile->asyncRequestId = asyncRequestId;
		request->RequestId = asyncRequestId;
	}

	user->shouldCloseFile = (request->Flags & MSSIO_FLAGS_DONT_CLOSE_HANDLE) == 0;

	if ((request->Flags & (MSSIO_FLAGS_QUERY_START_ONLY|MSSIO_FLAGS_QUERY_SIZE_ONLY)) != 0)
		request->Start = asyncFile->fileSize - asyncFile->readStart;

	size_t readCount = request->Count;

	if ((request->Flags & MSSIO_FLAGS_QUERY_SIZE_ONLY) != 0 || readCount == 0)
	{
		user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
		request->Status = MSSIO_STATUS_COMPLETE;

		if (user->shouldCloseFile)
			CSOM_CloseAsyncFile(asyncFile);

		return 1;
	}

	size_t readOffset = 0;

	if ((request->Flags & MSSIO_FLAGS_DONT_USE_OFFSET) == 0)
	{
		readOffset = request->Offset;
		asyncFile->readOffset = readOffset;
	}

	if (readCount < 0)
	{
		readCount = asyncFile->fileSize - asyncFile->readStart - readOffset;
		request->Count = readCount;
	}

	size_t numBytesLeft = asyncFile->fileSize - asyncFile->readStart - readOffset;

	if (readCount < numBytesLeft)
		numBytesLeft = readCount;

	request->Count = numBytesLeft;

	if (!request->Buffer)
	{
		// Allocate a read buffer.
		const size_t readBufSize = numBytesLeft + request->ReadAmt;
		void* const readBuffer = v_MilesAllocEx(readBufSize, 0, g_milesGlobals->driver, request->LastAllocSrcFileName, request->LastAllocSrcFileLine);

		if (!readBuffer)
		{
			Error(eDLL_T::AUDIO, EXIT_FAILURE, "Miles async failed malloc for '%s' size %zu\n", asyncFile->fileName, readBufSize);

			user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
			request->Status = MSSIO_STATUS_ERROR_MEMORY_ALLOC_FAIL;

			if (user->shouldCloseFile)
				CSOM_CloseAsyncFile(asyncFile);

			return 0;
		}

		request->Buffer = readBuffer;
	}

	if (request->Count)
	{
		// Read data into the buffer.
		user->readFinished = false;
		user->asyncFileHandle = g_pakLoadApi->ReadAsyncFile(asyncFile->fileHandle, asyncFile->readStart + asyncFile->readOffset, request->Count, request->Buffer, 1);

		if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		{
			Error(eDLL_T::AUDIO, EXIT_FAILURE, "Miles async failed read for '%s' offset %zu count %zu\n", asyncFile->fileName, request->Offset, request->Count);
			request->Status = MSSIO_STATUS_ERROR_FAILED_OPEN;

			if (user->shouldCloseFile)
				CSOM_CloseAsyncFile(asyncFile);

			return 0;
		}

		request->Status = MSSIO_STATUS_COMPLETE_NOP;
	}
	else
	{
		request->Status = MSSIO_STATUS_COMPLETE_NOP;
		user->readFinished = true;
		user->asyncFileHandle = FS_ASYNC_REQ_INVALID;
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: Miles async file status request handler; maps to internal callback
//          of MilesAsyncFileStatus, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileStatus(MilesAsyncRead* const request, const u32 i_MS)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;

	if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		return request->Status;

	AsyncHandleStatus_s::Status_e currentStatus;

	if (user->readFinished)
	{
		currentStatus = AsyncHandleStatus_s::Status_e::FS_ASYNC_READY;
	}
	else
	{
		if (i_MS)
			g_pakLoadApi->WaitForAsyncRequest(user->asyncFileHandle);

		currentStatus = g_pakLoadApi->CheckAsyncRequest(user->asyncFileHandle, nullptr, nullptr);

		if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_PENDING)
			return 0;
	}

	user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
	CSOM_AsyncFile_s* const asyncFile = &g_milesGlobals->asyncFiles[request->RequestId & CSOM_MAX_ASYNC_FILE_HANDLES_MASK];

	if (user->shouldCloseFile)
		CSOM_CloseAsyncFile(asyncFile);

	if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_READY)
	{
		request->LastCount = request->Count;
		asyncFile->readOffset += request->Count;
		request->Status = MSSIO_STATUS_COMPLETE;
	}
	else // Failure or canceled.
	{
		request->LastCount = 0;

		if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_CANCELLED)
			request->Status = MSSIO_STATUS_ERROR_CANCELLED;
		else
			request->Status = MSSIO_STATUS_ERROR_FAILED_READ;
	}

	return request->Status;
}

//-----------------------------------------------------------------------------
// Purpose: Miles async file cancel request handler; maps to internal callback
//          of MilesAsyncFileCancel, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileCancel(MilesAsyncRead* const request)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;

	if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		return 1; // Nothing to cancel.

	if (!user->readFinished)
		g_pakLoadApi->CancelAsyncRequest(user->asyncFileHandle);

	return CSOM_MilesAsync_FileStatus(request, RR_WAIT_INFINITE);
}

///////////////////////////////////////////////////////////////////////////////
void MilesCore::Detour(const bool bAttach) const
{
	DetourSetup(&v_MilesQueueEventRun, &MilesQueueEventRun, bAttach);
	//DetourSetup(&v_MilesBankPatch, &MilesBankPatch, bAttach);
	DetourSetup(&v_CSOM_Initialize, &CSOM_Initialize, bAttach);
	DetourSetup(&v_CSOM_LogFunc, &CSOM_LogFunc, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileRead, &CSOM_MilesAsync_FileRead, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileStatus, &CSOM_MilesAsync_FileStatus, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileCancel, &CSOM_MilesAsync_FileCancel, bAttach);
	DetourSetup(&v_CSOM_AddEventToQueue, &CSOM_AddEventToQueue, bAttach);

	if (bAttach)
	{
		CMemory mem(v_CSOM_RunFrame);

		// Between Miles version 10.0.48 and 10.0.50, they swapped locations of
		// 2 members in a struct returned by MilesEventInfoQueueEnum on type 4.
		// This change breaks closed captions (sub-titles). The fix is to apply
		// the swap in the assembly code as well so the engine retrieves the
		// values correctly from the new locations again. The structure layout
		// on all other enums are still identical and do not need to be fixes.
		mem.Offset(0x762).Patch({ 0x4 });
		mem.Offset(0x78B).Patch({ 0xC });
	}
}
