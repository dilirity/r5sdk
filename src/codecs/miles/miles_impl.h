#pragma once
#include "miles_types.h"
#include "miles/include/mss.h"

#define CSOM_MAX_ASYNC_FILE_HANDLES 128
#define CSOM_MAX_ASYNC_FILE_HANDLES_MASK (CSOM_MAX_ASYNC_FILE_HANDLES-1)
#define CSOM_MAX_FILE_NAME 64
#define CSOM_MAX_LOADED_BANKS 16

struct CSOM_AsyncFile_s
{
	u64 asyncRequestId;
	char fileName[CSOM_MAX_FILE_NAME];
	int fileHandle;
	size_t readOffset;
	size_t readStart;
	size_t fileSize;
};

struct CSOM_BankList_s
{
	char banks[CSOM_MAX_LOADED_BANKS][CSOM_MAX_FILE_NAME];
	int bankCount;
};

/* ==== WASAPI THREAD SERVICE =========================================================================================================================================== */
inline void*(*v_MilesAllocEx)(const size_t size, const u8 flags, void* const driver, const char* const allocSrcFileName, const int allocSrcFileLine);
inline void(*v_MilesQueueEventRun)(Miles::Queue*, const char*);
inline void(*v_MilesBankPatch)(Miles::Bank*, char*, char*);
inline unsigned int (*v_MilesSampleSetSourceRaw)(__int64 a1, __int64 a2, unsigned int a3, int a4, unsigned __int16 a5, bool a6);
inline unsigned int (*v_MilesEventGetDetails)(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6, void* const releaseList);
inline __int64 (*v_MilesSampleCreate)(__int64 a1, __int64 a2, unsigned __int8 a3);
inline void (*v_MilesSampleSet3DPosition)(void* sample, float x, float y, float z);
inline void (*v_MilesSamplePlay)(void* sample);
inline __int64 (*v_MilesSamplePause)(_BYTE* sample);
inline __int64 (*v_MilesSamplePauseFade)(__int64 sample);
inline __int64 (*v_MilesSampleDestroy)(__int64 sample);
inline __int64 (*v_MilesSampleGetDurationMs)(__int64 sample);
inline __int64 (*v_MilesSampleGetDurationSamples)(__int64 sample);
inline void (*v_MilesSampleSetListenerMask)(void* sample, unsigned int mask);
inline void (*v_MilesSampleSetVolumeLevel)(void* sample, float volume);
inline int (*v_MilesSampleGetRouteCount)(__int64 sample);
inline __int64 (*v_MilesSampleCreateRoute)(__int64 sample, __int64 a2, __int64 a3, unsigned __int8 a4);
inline __int64 (*v_MilesSampleGetRoute)(__int64 sample, int index);
inline void (*v_MilesRouteSetSpatialized)(void* route);
inline void (*v_MilesSampleSet3DAutoSpreadDistance)(void* sample, float distance);
inline void (*v_MilesSampleSetPanLeftRight)(void* sample, float pan);
inline void (*v_MilesSampleSet3DOrientation)(__int64 sample, float fx, float fy, float fz, float upY, int flags, float extra);
inline void (*v_MilesSampleSet3DVolumeCone)(__int64 sample, int enable, float inner, float outer, int flags);

// Listener position functions for proper 3D audio
inline void (*v_MilesListenerSet3DPosition)(__int64 driver, unsigned __int16 listenerIndex, float x, float y, float z);
inline void (*v_MilesListenerGet3DPosition)(__int64 driver, unsigned __int16 listenerIndex, float* x, float* y, float* z);
inline void (*v_MilesSampleSet3DVolumeGraph)(__int64 sample, const void* graph, int count);
inline void (*v_MilesTestDisable3DLFE)(char disable);

inline bool(*v_CSOM_Initialize)();
inline void(*v_CSOM_InitializeBankList)(CSOM_BankList_s* const bankList);
inline void(*v_CSOM_LogFunc)(int64_t nLogLevel, const char* pszMessage);
inline void(*v_CSOM_RunFrame)(char a1, char a2, float a3, float a4);

inline s32(*v_CSOM_MilesAsync_FileRead)(MilesAsyncRead* const request);
inline s32(*v_CSOM_MilesAsync_FileStatus)(MilesAsyncRead* const request, const u32 i_MS);
inline s32(*v_CSOM_MilesAsync_FileCancel)(MilesAsyncRead* const request);

inline void(*v_CSOM_AddEventToQueue)(const char* eventName);

struct CSOM_GlobalState_s
{
	char gap0[24];
	bool mismatchedBuildTag;
	char gap19[63];
	uintptr_t queuedEventHash;
	char gap60[4];
	Vector3D queuedSoundPosition;
	char gap70[24];
	float soundMasterVolume;
	char gap8c[28];
	void* samplesXlogType;
	char gapB0[8];
	void* dumpXlogType;
	char gapC0[48];
	void* driver;
	void* queue;
	char gap100[40];
	CSOM_BankList_s bankList;
	char gap52c[4];
	void* loadedBanks[CSOM_MAX_LOADED_BANKS];
	char gap5b0[273016];
	CSOM_AsyncFile_s asyncFiles[CSOM_MAX_ASYNC_FILE_HANDLES];
	size_t asyncRequestIdGen;
	char gap46430[4110];
	HANDLE milesInitializedEvent;
	HANDLE milesThread;
	char gap47450[272];
	char milesOutputBuffer[1024];
	char unk[96];
};

struct EventTableEntry {
	// A 64-bit value that serves as an index to the next entry in case of a hash collision.
	// A value of 0 indicates the end of the list for this bucket.
	uint64_t nextEntryIndex; // Offset 0x00

	// The full 64-bit FNV-1a hash of the normalized event name.
	uint64_t fullHash;       // Offset 0x08

	// The remaining 32 bytes would contain the actual event data, like a function pointer, etc.
	char eventData[32];      // Offset 0x10
};

inline CSOM_GlobalState_s* g_milesGlobals;

// Global listener position tracking
extern Vector3D g_listenerPosition;

// Function to update listener position for proper 3D audio
void CSOM_UpdateListenerPosition(const Vector3D& position);

// Functions for active WAV sample volume tracking
void UpdateActiveWavSampleVolumes();
void AddActiveWavSample(void* sample, const Vector3D& soundPosition, float baseVolume, int sampleRate, int totalSamples);

// Stop all custom audio (for level changes)
void StopAllCustomAudio();

// Handle level changes (call from Mod_HandleLevelChanged)
void Miles_HandleLevelChanged();

///////////////////////////////////////////////////////////////////////////////
class MilesCore : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("MilesAllocEx", v_MilesAllocEx);
		LogFunAdr("MilesQueueEventRun", v_MilesQueueEventRun);
		LogFunAdr("MilesBankPatch", v_MilesBankPatch);
		LogFunAdr("MilesSampleSetSourceRaw", v_MilesSampleSetSourceRaw);
		LogFunAdr("MilesEventGetDetails", v_MilesEventGetDetails);
		LogFunAdr("MilesListenerSet3DPosition", v_MilesListenerSet3DPosition);
		LogFunAdr("MilesListenerGet3DPosition", v_MilesListenerGet3DPosition);
		LogFunAdr("CSOM_Initialize", v_CSOM_Initialize);
		LogFunAdr("CSOM_InitializeBankList", v_CSOM_InitializeBankList);
		LogFunAdr("CSOM_LogFunc", v_CSOM_LogFunc);
		LogFunAdr("CSOM_RunFrame", v_CSOM_RunFrame);
		LogFunAdr("CSOM_MilesAsync_FileRead", v_CSOM_MilesAsync_FileRead);
		LogFunAdr("CSOM_MilesAsync_FileStatus", v_CSOM_MilesAsync_FileStatus);
		LogFunAdr("CSOM_MilesAsync_FileCancel", v_CSOM_MilesAsync_FileCancel);
		LogFunAdr("CSOM_AddEventToQueue", v_CSOM_AddEventToQueue);
		LogVarAdr("g_milesGlobals", g_milesGlobals);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 56 57 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ??").GetPtr(v_CSOM_Initialize);
		Module_FindPattern(g_GameDll, "40 55 41 54 41 55 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 83 65").GetPtr(v_CSOM_InitializeBankList);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 20 48 8B DA 48 8D 15 ?? ?? ?? ??").GetPtr(v_CSOM_LogFunc);
		Module_FindPattern(g_GameDll, "48 8B C4 F3 0F 11 50 ?? 88 50").GetPtr(v_CSOM_RunFrame);
		Module_FindPattern(g_GameDll, "4C 8B DC 53 55 56 57 48 81 EC").GetPtr(v_CSOM_MilesAsync_FileRead);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 57 48 83 EC ?? 8B B9 ?? ?? ?? ?? 48 8B D9 83 FF").GetPtr(v_CSOM_MilesAsync_FileStatus);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ?? 8B 81 ?? ?? ?? ?? 48 8B D9 83 F8 ?? 75 ?? B8").GetPtr(v_CSOM_MilesAsync_FileCancel);
		Module_FindPattern(g_GameDll, "0F B6 11 4C 8B C1").GetPtr(v_CSOM_AddEventToQueue);
		
		g_RadAudioSystemDll.GetExportedSymbol("MilesAllocEx").GetPtr(v_MilesAllocEx);
		g_RadAudioSystemDll.GetExportedSymbol("MilesQueueEventRun").GetPtr(v_MilesQueueEventRun);
		g_RadAudioSystemDll.GetExportedSymbol("MilesBankPatch").GetPtr(v_MilesBankPatch);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSetSourceRaw").GetPtr(v_MilesSampleSetSourceRaw);
		g_RadAudioSystemDll.GetExportedSymbol("MilesEventGetDetails").GetPtr(v_MilesEventGetDetails);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleCreate").GetPtr(v_MilesSampleCreate);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSamplePause").GetPtr(v_MilesSamplePause);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSamplePauseFade").GetPtr(v_MilesSamplePauseFade);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleDestroy").GetPtr(v_MilesSampleDestroy);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleGetDurationMs").GetPtr(v_MilesSampleGetDurationMs);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleGetDurationSamples").GetPtr(v_MilesSampleGetDurationSamples);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSet3DPosition").GetPtr(v_MilesSampleSet3DPosition);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSamplePlay").GetPtr(v_MilesSamplePlay);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSetListenerMask").GetPtr(v_MilesSampleSetListenerMask);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSetVolumeLevel").GetPtr(v_MilesSampleSetVolumeLevel);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleGetRouteCount").GetPtr(v_MilesSampleGetRouteCount);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleCreateRoute").GetPtr(v_MilesSampleCreateRoute);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleGetRoute").GetPtr(v_MilesSampleGetRoute);
        g_RadAudioSystemDll.GetExportedSymbol("MilesRouteSetSpatialized").GetPtr(v_MilesRouteSetSpatialized);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSet3DAutoSpreadDistance").GetPtr(v_MilesSampleSet3DAutoSpreadDistance);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSetPanLeftRight").GetPtr(v_MilesSampleSetPanLeftRight);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSet3DOrientation").GetPtr(v_MilesSampleSet3DOrientation);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSet3DVolumeCone").GetPtr(v_MilesSampleSet3DVolumeCone);
        
        // Initialize listener position functions
        g_RadAudioSystemDll.GetExportedSymbol("MilesListenerSet3DPosition").GetPtr(v_MilesListenerSet3DPosition);
        g_RadAudioSystemDll.GetExportedSymbol("MilesListenerGet3DPosition").GetPtr(v_MilesListenerGet3DPosition);
        g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSet3DVolumeGraph").GetPtr(v_MilesSampleSet3DVolumeGraph);
        g_RadAudioSystemDll.GetExportedSymbol("MilesTestDisable3DLFE").GetPtr(v_MilesTestDisable3DLFE);
	}
	virtual void GetVar(void) const
	{
		g_milesGlobals = CMemory(v_CSOM_Initialize).FindPatternSelf("48 8D", CMemory::Direction::DOWN, 0x50).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CSOM_GlobalState_s*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
