#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <regex>

// Northstar-like custom audio override system for Miles.
// Supports per-mod JSON manifests (in `audio/*.json`) with a sibling samples folder
// named the same as the JSON (without extension). Each JSON can register exact
// event ids, wildcard "*", and regex patterns.

enum class AudioSelectionStrategy
{
	SEQUENTIAL = 0,
	RANDOM = 1,
};

struct AudioSample
{
	size_t size = 0;
	std::unique_ptr<uint8_t[]> data;
	int sampleRate = 0;
	unsigned short channels = 0;
	unsigned short bitsPerSample = 0;
};

struct EventOverrideData
{
	std::vector<AudioSample> samples;
	AudioSelectionStrategy strategy = AudioSelectionStrategy::SEQUENTIAL;
	size_t currentIndex = 0;
	std::vector<std::string> eventIds;
	std::vector<std::pair<std::string, std::regex>> eventIdsRegex;

	// Optional JSON-configurable audio params (use when wav_force_convars == 0)
	bool hasVolumeBase = false; float volumeBase = 1.0f;
	bool hasVolumeMin = false; float volumeMin = 0.0f;
	bool hasDistanceStart = false; float distanceStart = 100.0f;
	bool hasFalloffPower = false; float falloffPower = 1.0f;
	bool hasVolumeUpdateRate = false; float volumeUpdateRate = 0.1f;
	bool hasAllowSilence = false; bool allowSilence = true;
	bool hasSilenceCutoff = false; float silenceCutoff = 0.001f;
	bool hasCancelOnReplay = false; bool cancelOnReplay = false;
	bool hasFadeOnDestroy = false; bool fadeOnDestroy = false;
	
	// Custom Doppler effect settings
	bool hasDopplerEnabled = false; bool dopplerEnabled = false;
	bool hasDopplerFactor = false; float dopplerFactor = 1.0f;
	bool hasSpeedOfSound = false; float speedOfSound = 343.0f; // meters per second
	bool hasListenerVelocity = false; float listenerVelocityX = 0.0f, listenerVelocityY = 0.0f, listenerVelocityZ = 0.0f;
	bool hasSourceVelocity = false; float sourceVelocityX = 0.0f, sourceVelocityY = 0.0f, sourceVelocityZ = 0.0f;
	
	// Custom 3D volume cone settings
	bool hasVolumeCone = false; bool volumeConeEnabled = false;
	bool hasInsideAngle = false; int insideAngleDeg = 360;
	bool hasInsideVolume = false; float insideVolume = 1.0f;
	bool hasOutsideAngle = false; int outsideAngleDeg = 360;
	bool hasOutsideVolume = false; float outsideVolume = 0.5f;
	
	// 3D audio positioning and spatialization settings
	bool has3DAudioEnabled = false; bool audio3DEnabled = false;
	bool hasUseRoutes = false; bool useRoutes = true;
	bool hasAutoSpreadDistance = false; float autoSpreadDistance = 100.0f;
	bool hasPosition3D = false; float position3DX = 0.0f, position3DY = 0.0f, position3DZ = 0.0f;
	bool hasOrientation3D = false; float orientationFX = 0.0f, orientationFY = 0.0f, orientationFZ = 1.0f;
	bool hasUpVector3D = false; float upVectorY = 1.0f, upVectorZ = 0.0f;
	bool hasMultiChannelPan = false; float panLeftRight = 0.0f, panFrontBack = 0.0f;
	
	// Fake reverb effect settings
	bool hasReverbEnabled = false; bool reverbEnabled = false;
	bool hasReverbDecayTime = false; float reverbDecayTime = 2.0f; // seconds
	bool hasReverbRoomSize = false; float reverbRoomSize = 0.5f; // 0.0 to 1.0 (small to large room)
	bool hasReverbDamping = false; float reverbDamping = 0.3f; // 0.0 to 1.0 (low to high frequency absorption)
	bool hasReverbWetLevel = false; float reverbWetLevel = 0.3f; // 0.0 to 1.0 (dry to wet mix)
	bool hasReverbDryLevel = false; float reverbDryLevel = 0.8f; // 0.0 to 1.0 (original signal level)
	bool hasReverbDelay = false; float reverbDelay = 0.05f; // seconds (initial reflection delay)
	bool hasReverbDiffusion = false; float reverbDiffusion = 0.7f; // 0.0 to 1.0 (echo clarity)
};

class CustomAudioManager
{
public:
	void LoadFromMods();
	// Load overrides from a specific subdirectory under each mod (e.g. "audio" or "audio_override").
	void LoadFromModsInDir(const char* subdirName);
	void Clear();

	// Returns a buffer for the given event if overridden, along with format metadata.
	bool TryGetOverrideForEventDetailed(
		const char* eventName,
		const void*& outPtr,
		unsigned& outLen,
		int& outSampleRate,
		unsigned short& outChannels,
		unsigned short& outBitsPerSample);

	// Back-compat wrapper (no metadata).
	bool TryGetOverrideForEvent(const char* eventName, const void*& outPtr, unsigned& outLen);

	bool HasOverride(const char* eventName);
	int GetOverrideCount();

	// Fetch settings for an event (resolves regex if enabled). Returns false if no override.
	bool GetOverrideSettingsForEvent(
		const char* eventName,
		bool& hasVolumeBase, float& volumeBase,
		bool& hasVolumeMin, float& volumeMin,
		bool& hasDistanceStart, float& distanceStart,
		bool& hasFalloffPower, float& falloffPower,
		bool& hasVolumeUpdateRate, float& volumeUpdateRate,
		bool& hasAllowSilence, bool& allowSilence,
		bool& hasSilenceCutoff, float& silenceCutoff);

	bool GetCancelOnReplayForEvent(const char* eventName, bool& cancelOnReplay);

private:
	std::unordered_map<std::string, std::shared_ptr<EventOverrideData>> m_overrides;
	std::vector<std::pair<std::string, std::shared_ptr<EventOverrideData>>> m_overridesRegex;
	mutable std::shared_mutex m_mutex;
};

// Global accessors used by Miles glue code
CustomAudioManager* GetAudioOverrideManager();
CustomAudioManager* GetAudioOverrideManagerNorthstar(); // For audio_overrides directory

// Helpers used by the Miles event hook path
void AudioOverride_OnEventRun(const char* eventName);
bool AudioOverride_TryGetBufferForCurrentEvent(const void*& outPtr, unsigned& outLen);
void AudioOverride_ClearCurrentEvent();
const char* AudioOverride_GetCurrentEventName();


