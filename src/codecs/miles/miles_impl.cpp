//===============================================================================//
//
// Purpose: Client Sound Miles implementation
//
//===============================================================================//
#include "core/stdafx.h"
#include "tier0/fasttimer.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include <cmath>
#include <algorithm>
#include "rtech/core/strutils.h"
#include "rtech/async/asyncio.h"
#include "rtech/pak/pakstate.h"
#include "filesystem/filesystem.h"
#include "pluginsystem/modsystem.h"
#include "ebisusdk/EbisuSDK.h"
#include "miles_impl.h"
#include "miles_overrides.h"
#include "game/client/viewrender.h"
#include "miles/src/sdk/shared/rrthreads2.h"
#include "audio_overrides.h"
#include <unordered_map>
#include <mutex>

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------
static ConVar miles_debug("miles_debug", "0", FCVAR_DEVELOPMENTONLY, "Enables debug prints for the Miles Sound System", "1 = print; 0 (zero) = no print");
static ConVar miles_warnings("miles_warnings", "0", FCVAR_RELEASE, "Enables warning prints for the Miles Sound System", "1 = print; 0 (zero) = no print");

// Custom WAV override volume controls
static ConVar wav_debug("wav_debug", "0", FCVAR_DEVELOPMENTONLY, "Enables warning prints for the Miles Sound System", "1 = print; 0 (zero) = no print");

static ConVar enable_audio_mods("enable_audio_mods", "1", FCVAR_RELEASE, "Enables custom audio mods");
static ConVar wav_volume_base("wav_volume_base", "1", FCVAR_RELEASE, "Base volume multiplier for custom WAV overrides (0.0 to 1.0)");
static ConVar wav_volume_min("wav_volume_min", "0.0", FCVAR_RELEASE, "Minimum volume for custom WAV overrides (0.0 to 1.0, 0.0 = complete silence)");
static ConVar wav_distance_start("wav_distance_start", "100.0", FCVAR_RELEASE, "Distance at which volume attenuation starts for custom WAV overrides");
static ConVar wav_falloff_power("wav_falloff_power", "1.0", FCVAR_RELEASE, "Power for distance falloff curve (0.1 = gentle, 2.0 = aggressive)");
static ConVar wav_volume_update_rate("wav_volume_update_rate", "0.1", FCVAR_RELEASE, "How often to update WAV sample volumes in seconds (0.1 = 10 times per second)");
static ConVar wav_allow_silence("wav_allow_silence", "1", FCVAR_RELEASE, "Allow sounds to fade to complete silence (1 = yes, 0 = no - always keep minimum volume)");
static ConVar wav_silence_cutoff("wav_silence_cutoff", "0.001", FCVAR_RELEASE, "Volume threshold below which sounds become completely silent");
static ConVar wav_force_convars("wav_force_convars", "0", FCVAR_RELEASE, "Force using convar audio params; ignore JSON per-override settings");

// Level change detection
static ConVar miles_wav_auto_stop_on_level_change("miles_wav_auto_stop_on_level_change", "1", FCVAR_RELEASE, "Automatically stop custom audio when level changes (1 = enabled, 0 = disabled)");

static void CC_reload_audio_mods(const CCommand& args)
{
	if (ModSystem()->IsEnabled())
	{
		GetAudioOverrideManager()->LoadFromMods();
		Msg(eDLL_T::AUDIO, "Loaded %d audio overrides\n", GetAudioOverrideManager()->GetOverrideCount());
	}
}
static ConCommand reload_audio_mods("reload_audio_mods", CC_reload_audio_mods, "", FCVAR_CLIENTDLL | FCVAR_RELEASE);


//-----------------------------------------------------------------------------
// Global listener position tracking
//-----------------------------------------------------------------------------
Vector3D g_listenerPosition = {0.0f, 0.0f, 0.0f};

// Structure to track active custom WAV samples for volume updates
struct ActiveWavSample
{
	void* sample;
	Vector3D soundPosition;
	float baseVolume;
	float startTime;
	float durationMs;  // Actual audio duration in milliseconds
	int sampleRate;    // PCM sample rate for duration calculation
	int totalSamples;  // Total PCM samples for duration calculation
	bool isActive;
};

#define MAX_ACTIVE_WAV_SAMPLES 32
static ActiveWavSample s_activeWavSamples[MAX_ACTIVE_WAV_SAMPLES];
static int s_numActiveWavSamples = 0;

// Timer for periodic volume updates
static float s_lastVolumeUpdateTime = 0.0f;

// A global pointer that will be set to the address of the found event's data.
// It's used as a return value for the function.
void* g_pFoundEventData; // Corresponds to qword_1655B9658

// Base address of the main table containing the EventTableEntry structs.
EventTableEntry* g_EventTableBase; // Corresponds to qword_14D4E4AE8

// An array of 4096 (0x1000) indices that maps a partial hash to the first entry in a bucket.
uint32_t g_EventTableBuckets[4096]; // Corresponds to dword_14D4E0AE8

// Track active custom samples per event for CancelOnReplay behavior
static std::unordered_map<std::string, std::vector<void*>> s_eventActiveSamples;
static std::mutex s_eventSamplesMutex;

//-----------------------------------------------------------------------------
// Purpose: Stops and cleans up all active custom WAV samples
//-----------------------------------------------------------------------------
void StopAllCustomAudio()
{
	if (wav_debug.GetBool())
	{
		Msg(eDLL_T::AUDIO, "Stopping all custom audio samples (%d active)\n", s_numActiveWavSamples);
	}
	
	for (int i = 0; i < s_numActiveWavSamples; i++)
	{
		ActiveWavSample& activeSample = s_activeWavSamples[i];
		if (activeSample.isActive)
		{
			// Force volume to 0 first to stop audio immediately
			if (v_MilesSampleSetVolumeLevel)
			{
				v_MilesSampleSetVolumeLevel(activeSample.sample, 0.0f);
			}
			
			// Pause the sample to stop playback
			if (v_MilesSamplePause)
			{
				v_MilesSamplePause(reinterpret_cast<_BYTE*>(activeSample.sample));
			}
			
			// Finally destroy the sample
			if (v_MilesSampleDestroy)
			{
				v_MilesSampleDestroy(reinterpret_cast<__int64>(activeSample.sample));
			}
			
			// Mark as inactive
			activeSample.isActive = false;
		}
	}
	
	s_numActiveWavSamples = 0;
	
	if (wav_debug.GetBool())
	{
		Msg(eDLL_T::AUDIO, "All custom audio samples stopped and cleaned up\n");
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called when a level change is detected - stops all custom audio
//-----------------------------------------------------------------------------
void Miles_HandleLevelChanged()
{
	if (miles_wav_auto_stop_on_level_change.GetBool())
	{
		if (wav_debug.GetBool())
		{
			Msg(eDLL_T::AUDIO, "Level change detected via Mod_HandleLevelChanged, stopping all custom audio\n");
		}
		StopAllCustomAudio();
	}
}



//-----------------------------------------------------------------------------
// Purpose: Updates the listener position for proper 3D audio
//-----------------------------------------------------------------------------
void CSOM_UpdateListenerPosition(const Vector3D& position)
{
	g_listenerPosition = position;
	
	/*if (g_milesGlobals && g_milesGlobals->driver && v_MilesListenerSet3DPosition)
	{
		v_MilesListenerSet3DPosition(
			reinterpret_cast<__int64>(g_milesGlobals->driver),
			0, // listener index (usually 0 for primary listener)
			position.x,
			position.y, 
			position.z
		);
		
		if (wav_debug.GetBool())
		{
			Msg(eDLL_T::AUDIO, "%s: Updated listener position to (%.2f, %.2f, %.2f)\n", 
				__FUNCTION__, position.x, position.y, position.z);
		}
	}*/
	
	// Update volume for all active custom WAV samples based on new listener position
	// Only update if enough time has passed to avoid excessive updates
	float currentTime = (float)Plat_FloatTime();
	if (currentTime - s_lastVolumeUpdateTime >= wav_volume_update_rate.GetFloat())
	{
		UpdateActiveWavSampleVolumes();
		s_lastVolumeUpdateTime = currentTime;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Updates volume for all active custom WAV samples based on current distance
//-----------------------------------------------------------------------------
void UpdateActiveWavSampleVolumes()
{
	if (!v_MilesSampleSetVolumeLevel)
		return;
		
	float distanceStart = wav_distance_start.GetFloat();
	float falloffPower = wav_falloff_power.GetFloat();
	float minVolume = wav_volume_min.GetFloat();
	
	// Clean up old samples based on actual audio duration
	float currentTime = (float)Plat_FloatTime();
	for (int i = 0; i < s_numActiveWavSamples; i++)
	{
		ActiveWavSample& activeSample = s_activeWavSamples[i];
		if (activeSample.isActive)
		{
			// Calculate how long the sample should have been playing
			float elapsedTime = (currentTime - activeSample.startTime) * 1000.0f; // Convert to milliseconds
			float expectedDuration = activeSample.durationMs + 1000.0f; // Add 1 second buffer for safety
			
			if (elapsedTime > expectedDuration)
			{
				activeSample.isActive = false;
				if (wav_debug.GetBool())
					Msg(eDLL_T::AUDIO, "Cleaned up sample after %.1f seconds (duration: %.1f ms)\n", 
						elapsedTime / 1000.0f, activeSample.durationMs);
			}
		}
	} 
	for (int i = 0; i < s_numActiveWavSamples; i++)
	{
		ActiveWavSample& activeSample = s_activeWavSamples[i];
		if (!activeSample.isActive)
			continue;
			
		// Check if this sound is attached to the player (position 0,0,0)
		bool isAttachedToPlayer = (activeSample.soundPosition.x == 0.0f && 
								  activeSample.soundPosition.y == 0.0f && 
								  activeSample.soundPosition.z == 0.0f);
		
		// Calculate current distance from listener to sound
		Vector3D deltaPos;
		float distance;
		
		if (isAttachedToPlayer)
		{
			// Sound follows the player - always at listener position
			deltaPos = {0.0f, 0.0f, 0.0f};
			distance = 0.0f;
		}
		else
		{
			// Normal 3D positioned sound
			deltaPos = {
				activeSample.soundPosition.x - g_listenerPosition.x,
				activeSample.soundPosition.y - g_listenerPosition.y,
				activeSample.soundPosition.z - g_listenerPosition.z
			};
			distance = sqrt(deltaPos.x * deltaPos.x + deltaPos.y * deltaPos.y + deltaPos.z * deltaPos.z);
		}
		
		// Apply distance-based volume attenuation
		float attenuatedVolume;
		if (distance > distanceStart)
		{
			float falloffFactor = distanceStart / distance;
			attenuatedVolume = activeSample.baseVolume * pow(falloffFactor, falloffPower);
			
			// Handle silence option
			if (wav_allow_silence.GetBool())
			{
				// Allow complete silence if falloff makes volume very low
				if (attenuatedVolume < 0.001f) // Very low threshold
				{
					attenuatedVolume = 0.0f; // Complete silence
				}
				else
				{
					attenuatedVolume = max(attenuatedVolume, minVolume);
				}
			}
			else
			{
				// Always keep minimum volume
				attenuatedVolume = max(attenuatedVolume, minVolume);
			}
		}
		else
		{
			attenuatedVolume = activeSample.baseVolume;
		}
		
		// Update the sample volume
		v_MilesSampleSetVolumeLevel(activeSample.sample, attenuatedVolume);
		

		
		if (wav_debug.GetBool())
		{
			const char* soundType = isAttachedToPlayer ? "PLAYER-ATTACHED" : "3D-POSITIONED";
			Msg(eDLL_T::AUDIO, "Updated sample: %s distance=%.2f volume=%.3f (silence=%s)\n", 
				soundType, distance, attenuatedVolume,
				wav_allow_silence.GetBool() ? "enabled" : "disabled");
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Adds a custom WAV sample to the active tracking system
//-----------------------------------------------------------------------------
void AddActiveWavSample(void* sample, const Vector3D& soundPosition, float baseVolume, int sampleRate, int totalSamples)
{
	// Find a free slot
	int freeSlot = -1;
	for (int i = 0; i < MAX_ACTIVE_WAV_SAMPLES; i++)
	{
		if (!s_activeWavSamples[i].isActive)
		{
			freeSlot = i;
			break;
		}
	}
	
	if (freeSlot == -1)
	{
		// No free slots, remove the oldest one
		freeSlot = 0;
		for (int i = 1; i < MAX_ACTIVE_WAV_SAMPLES; i++)
		{
			if (s_activeWavSamples[i].startTime < s_activeWavSamples[freeSlot].startTime)
				freeSlot = i;
		}
	}
	
	// Add the new sample
	ActiveWavSample& newSample = s_activeWavSamples[freeSlot];
	newSample.sample = sample;
	newSample.soundPosition = soundPosition;
	newSample.baseVolume = baseVolume;
	newSample.startTime = (float)Plat_FloatTime();
	
	// Store PCM data for accurate duration calculation
	newSample.sampleRate = sampleRate;
	newSample.totalSamples = totalSamples;
	
	// Calculate duration from the actual WAV file PCM data
	// Duration = total_samples / sample_rate (in seconds)
	// Convert to milliseconds
	if (sampleRate > 0)
	{
		newSample.durationMs = (float)(totalSamples) / (float)sampleRate * 1000.0f;
	}
	else
	{
		newSample.durationMs = 5000.0f; // Fallback if sample rate is invalid
	}
	
	newSample.isActive = true;
	
	if (freeSlot >= s_numActiveWavSamples)
		s_numActiveWavSamples = freeSlot + 1;
		
	if (wav_debug.GetBool())
	{
		Msg(eDLL_T::AUDIO, "Added active WAV sample: pos=(%.2f,%.2f,%.2f) volume=%.3f duration=%.1f ms (rate=%d samples=%d)\n", 
			soundPosition.x, soundPosition.y, soundPosition.z, baseVolume, newSample.durationMs, 
			newSample.sampleRate, newSample.totalSamples);
	}
}

//-----------------------------------------------------------------------------
// Purpose: initializes the miles sound system
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
static bool CSOM_Initialize()
{
	const char* pszLanguage = HEbisuSDK_GetLanguage();
	const bool isDefaultLanguage = V_stricmp(pszLanguage, MILES_DEFAULT_LANGUAGE) == 0;

	if (!isDefaultLanguage)
	{
		if ((V_stricmp(pszLanguage, "schinese") == 0) || (V_stricmp(pszLanguage, "tchinese") == 0))
			pszLanguage = "mandarin"; // schinese and tchinese use the mandarin bank.

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

	Msg(eDLL_T::AUDIO, "%s: %s (%f seconds)\n", __FUNCTION__, bResult ? "success" : "failure", initTimer.GetDuration().GetSeconds());

	// Load custom audio overrides at init time
	if (bResult && ModSystem()->IsEnabled())
	{
		GetAudioOverrideManager()->LoadFromMods();
		if (wav_debug.GetBool())
			Msg(eDLL_T::AUDIO, "Loaded %d audio overrides\n", GetAudioOverrideManager()->GetOverrideCount());
	}

	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: appends banks from list to be loaded
//-----------------------------------------------------------------------------
static void CSOM_AppendBanksFromList(CSOM_BankList_s* const bankList, const char* const filePath, const bool mandatory)
{
	const int errorCode = mandatory ? EXIT_FAILURE : 0;
	RSON::Node_t* root = nullptr;

#define ERROR_AND_RETURN(fmt, ...) \
		do {\
			Error(eDLL_T::AUDIO, errorCode, "Error loading Miles Bank list from '%s': "##fmt, filePath, ##__VA_ARGS__); \
			if (root) {\
				RSON_Free(root, AlignedMemAlloc()); \
				AlignedMemAlloc()->Free(root); \
			}\
			return; \
		} while(0)\

	if (bankList->bankCount == CSOM_MAX_LOADED_BANKS)
	{
		ERROR_AND_RETURN("Out of room -- already reached code limit of %d.\n", CSOM_MAX_LOADED_BANKS);
		return;
	}

	CUtlBuffer buf;

	if (!FileSystem()->ReadFile(filePath, nullptr, buf))
	{
		if (mandatory) // Only exit if the main file doesn't exist.
			ERROR_AND_RETURN("Could not load file.\n");

		return;
	}

	const RSON::eFieldType rootType = (RSON::eFieldType)(RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_VALUE);
	root = RSON::LoadFromBuffer(filePath, (char*)buf.Base(), rootType);

	const RSON::eFieldType expectType = (RSON::eFieldType)(RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_OBJECT);

	if (!root || root->type != expectType)
		ERROR_AND_RETURN("Data should be an array of objects.\n");

	const int numSlotsLeft = (CSOM_MAX_LOADED_BANKS - bankList->bankCount);

	if (root->valueCount > numSlotsLeft)
		ERROR_AND_RETURN("Too many banks -- code limit is %d.\n", CSOM_MAX_LOADED_BANKS);

	bool nameSetForBank = false;

	for (int i = 0; i < root->valueCount; i++)
	{
		const RSON::Field_t* const key = root->GetArrayValue(i)->GetSubKey();

		if (!key)
			continue;

		if (V_strcmp(key->name, "name") != 0)
			ERROR_AND_RETURN("Only valid key is 'name', not '%s'.\n", key->name);

		if (nameSetForBank)
			ERROR_AND_RETURN("Each bank must have exactly one name.\n");

		nameSetForBank = true;

		if (key->node.type != RSON::eFieldType::RSON_STRING)
			ERROR_AND_RETURN("'name' must be a single string.\n");

		const char* const bankToAdd = key->GetString();

		// Make sure this bank wasn't already added.
		for (int j = 0; j < bankList->bankCount; j++)
		{
			if (V_stricmp(bankList->banks[j], bankToAdd) == 0)
				ERROR_AND_RETURN("Each bank must be unique; '%s' was already listed.\n", bankToAdd);
		}

		V_strncpy(bankList->banks[bankList->bankCount++], bankToAdd, CSOM_MAX_FILE_NAME);
	}

	RSON_Free(root, AlignedMemAlloc());
	AlignedMemAlloc()->Free(root);

#undef ERROR_AND_RETURN
}

#define CSOM_BANK_LIST_FILE "scripts/audio/banks.rson"

//-----------------------------------------------------------------------------
// Purpose: initializes the bank list object dictating which banks to load
//-----------------------------------------------------------------------------
static void CSOM_InitializeBankList(CSOM_BankList_s* const bankList)
{
	bankList->bankCount = 0;
	CSOM_AppendBanksFromList(bankList, CSOM_BANK_LIST_FILE, true);

	if (ModSystem()->IsEnabled())
	{
		ModSystem()->LockModList();

		// Add banks from our mods.
		FOR_EACH_VEC(ModSystem()->GetModList(), i)
		{
			const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

			if (!mod->IsEnabled())
				continue;

			const CUtlString lookupPath = mod->GetBasePath() + CSOM_BANK_LIST_FILE;
			CSOM_AppendBanksFromList(bankList, lookupPath.String(), false);
		}

		ModSystem()->UnlockModList();
	}
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

	if(enable_audio_mods.GetBool())
	{
		// New: Northstar-style override manager path (JSON or folder-based)
		{
			const void* buf = nullptr; unsigned len = 0; int rate = 0; unsigned short ch = 0; unsigned short bps = 0;
			if (GetAudioOverrideManager()->TryGetOverrideForEventDetailed(eventName, buf, len, rate, ch, bps))
			{
				// Honor CancelOnReplay: destroy any currently running samples for this event
				bool cancelOnReplay = false;
				if (GetAudioOverrideManager()->GetCancelOnReplayForEvent(eventName, cancelOnReplay) && cancelOnReplay)
				{
					std::lock_guard<std::mutex> lg(s_eventSamplesMutex);
					auto itSamples = s_eventActiveSamples.find(eventName);
					if (itSamples != s_eventActiveSamples.end())
					{
						for (void* samplePtr : itSamples->second)
						{
							// Mark in active tracking as inactive
							for (int i = 0; i < s_numActiveWavSamples; ++i)
							{
								if (s_activeWavSamples[i].isActive && s_activeWavSamples[i].sample == samplePtr)
									s_activeWavSamples[i].isActive = false;
							}
							// Fade or mute and pause then destroy
							bool fadeOnDestroy = false;
							GetAudioOverrideManager()->GetFadeOnDestroyForEvent(eventName, fadeOnDestroy);
							if (fadeOnDestroy && v_MilesSamplePauseFade)
							{
								v_MilesSamplePauseFade(reinterpret_cast<__int64>(samplePtr));
							}
							else
							{
								if (v_MilesSampleSetVolumeLevel)
									v_MilesSampleSetVolumeLevel(samplePtr, 0.0f);
								if (v_MilesSamplePause)
									v_MilesSamplePause(reinterpret_cast<_BYTE*>(samplePtr));
							}
							if (v_MilesSampleDestroy)
								v_MilesSampleDestroy(reinterpret_cast<__int64>(samplePtr));
						}
						itSamples->second.clear();
					}
				}

				void* sample = reinterpret_cast<void*>(v_MilesSampleCreate(reinterpret_cast<__int64>(g_milesGlobals->driver), 0, 0));
				if (sample)
				{
					Vector3D soundPos = g_milesGlobals->queuedSoundPosition;
					Vector3D playerPos = {0.0f, 0.0f, 0.0f};
					if (g_vecRenderOrigin)
					{
						playerPos = *g_vecRenderOrigin;
						CSOM_UpdateListenerPosition(playerPos);
					}
					else
					{
						CSOM_UpdateListenerPosition({0.0f, 0.0f, 0.0f});
					}

					const unsigned __int16 fmt = (unsigned __int16)((bps << 8) | (ch & 0xFF));
					v_MilesSampleSetSourceRaw((__int64)sample, reinterpret_cast<const __int64>(buf), len, rate, fmt, false);

					Vector3D deltaPos = {
						soundPos.x - g_listenerPosition.x,
						soundPos.y - g_listenerPosition.y,
						soundPos.z - g_listenerPosition.z
					};
					float distance = sqrt(deltaPos.x * deltaPos.x + deltaPos.y * deltaPos.y + deltaPos.z * deltaPos.z);

					bool hasVB=false; float jVB=1.0f;
					bool hasVM=false; float jVM=0.0f;
					bool hasDS=false; float jDS=100.0f;
					bool hasFP=false; float jFP=1.0f;
					bool hasUR=false; float jUR=0.1f;
					bool hasAS=false; bool jAS=true;
					bool hasSC=false; float jSC=0.001f;
					if (!wav_force_convars.GetBool())
					{
						GetAudioOverrideManager()->GetOverrideSettingsForEvent(
							eventName,
							hasVB, jVB,
							hasVM, jVM,
							hasDS, jDS,
							hasFP, jFP,
							hasUR, jUR,
							hasAS, jAS,
							hasSC, jSC);
					}

					float baseVolume = g_milesGlobals->soundMasterVolume * (hasVB ? jVB : wav_volume_base.GetFloat());
					float distanceStart = hasDS ? jDS : wav_distance_start.GetFloat();
					float falloffPower = hasFP ? jFP : wav_falloff_power.GetFloat();
					float minVolume = hasVM ? jVM : wav_volume_min.GetFloat();
					bool allowSilence = hasAS ? jAS : wav_allow_silence.GetBool();
					float silenceCutoff = hasSC ? jSC : wav_silence_cutoff.GetFloat();

					// Apply override to global update cadence if specified
					if (hasUR)
					{
						// update cadence: just set convar so system-wide volume updates follow
						wav_volume_update_rate.SetValue(jUR);
					}

					float initialVolume;
					if (distance > distanceStart)
					{
						float falloffFactor = distanceStart / distance;
						initialVolume = baseVolume * pow(falloffFactor, falloffPower);
						if (allowSilence)
						{
							if (initialVolume < silenceCutoff) initialVolume = 0.0f;
							else initialVolume = max(initialVolume, minVolume);
						}
						else
						{
							initialVolume = max(initialVolume, minVolume);
						}
					}
					else
					{
						initialVolume = baseVolume;
					}

					if (v_MilesSampleSetVolumeLevel) v_MilesSampleSetVolumeLevel(sample, initialVolume);

					int bytesPerSample = (bps / 8) * (int)ch;
					int totalSamples = bytesPerSample > 0 ? (int)len / bytesPerSample : 0;
					AddActiveWavSample(sample, soundPos, baseVolume, rate, totalSamples);

					// Register this sample under the event for potential cancellation on replay
					{
						std::lock_guard<std::mutex> lg(s_eventSamplesMutex);
						s_eventActiveSamples[eventName].push_back(sample);
					}

					v_MilesSamplePlay(sample);
				}

				v_CSOM_AddEventToQueue("");
				return;
			}
		}
	}

	///////////////////////// OG FUNCTION //////////////////////////////////
	// If the input string is null or empty, set the result to 1 and exit.
	/*if (eventName == nullptr || *eventName == '\0') {
		g_pFoundEventData = (void*)1;
		return;
	}

	// Constants for 64-bit FNV-1a hashing algorithm
	const uint64_t FNV_OFFSET_BASIS = 0xCBF29CE484222325;
	const uint64_t FNV_PRIME = 0x100000001B3;

	uint64_t hash = FNV_OFFSET_BASIS;
	const char* currentChar = eventName;

	// Loop through each character of the string to calculate the hash.
	while (*currentChar != '\0') {
		char processedChar = *currentChar;

		// Normalize the character before hashing.
		// 1. Convert uppercase letters to lowercase.
		if (processedChar >= 'A' && processedChar <= 'Z') {
			processedChar += 32; // ASCII difference between upper and lower
		}
		// 2. Replace periods with underscores.
		else if (processedChar == '.') {
			processedChar = '_';
		}

		// FNV-1a hash algorithm step: XOR with the character, then multiply by the prime.
		hash ^= static_cast<uint8_t>(processedChar);
		hash *= FNV_PRIME;

		currentChar++;
	}

	// Use the lower 12 bits of the hash to find the initial bucket index.
	uint32_t bucketIndex = g_EventTableBuckets[hash & 0xFFF];
	if (bucketIndex == 0) { // Assuming 0 is an invalid index, meaning bucket is empty.
		g_pFoundEventData = (void*)2; // Event not found
		return;
	}

	// Get the first potential entry from the main table using the bucket index.
	EventTableEntry* entry = &g_EventTableBase[bucketIndex];

	// Traverse the linked list for this bucket to find the correct entry.
	while (true) {
		// Check if the full hash in the table entry matches our calculated hash.
		if (entry->fullHash == hash) {
			// Match found! Set the global pointer to the event's data portion.
			g_pFoundEventData = &entry->eventData;
			return;
		}

		// If hashes don't match and there's no next entry, the event is not in the table.
		if (entry->nextEntryIndex == 0) {
			break; // End of chain
		}

		// Move to the next entry in the collision chain.
		entry = &g_EventTableBase[entry->nextEntryIndex];
	}

	// If the loop finishes without a match, the event was not found.
	g_pFoundEventData = (void*)2;*/

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
	DetourSetup(&v_CSOM_InitializeBankList, &CSOM_InitializeBankList, bAttach);
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
