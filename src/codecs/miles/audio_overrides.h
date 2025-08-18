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
};

class CustomAudioManager
{
public:
	void LoadFromMods();
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

private:
	std::unordered_map<std::string, std::shared_ptr<EventOverrideData>> m_overrides;
	std::vector<std::pair<std::string, std::shared_ptr<EventOverrideData>>> m_overridesRegex;
	mutable std::shared_mutex m_mutex;
};

// Global accessors used by Miles glue code
CustomAudioManager* GetAudioOverrideManager();

// Helpers used by the Miles event hook path
void AudioOverride_OnEventRun(const char* eventName);
bool AudioOverride_TryGetBufferForCurrentEvent(const void*& outPtr, unsigned& outLen);
void AudioOverride_ClearCurrentEvent();
const char* AudioOverride_GetCurrentEventName();


