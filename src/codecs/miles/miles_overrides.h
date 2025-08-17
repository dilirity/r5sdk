#pragma once

#include "tier1/utlmap.h"
#include "tier1/utlstring.h"

// Simple subsystem to load and resolve audio event overrides.
// Mods can provide mappings from original Miles event names to replacement
// event names using RSON at scripts/audio/overrides.rson.

namespace MilesOverrides
{
	// Load overrides from base game and enabled mods. Safe to call multiple times.
	void LoadAll();

	// Resolve an override for the given event name.
	// Returns replacement string if found, otherwise nullptr.
	// The returned pointer remains valid for the lifetime of the process.
	const char* Resolve(const char* originalEventName);

	// Try to locate a mod-provided WAV replacement using the scheme:
	//   <modBase>/audio/<eventId>.json
	//   <modBase>/audio/<eventId>/<eventId>.wav
	// Returns true and fills outPath if found.
	bool FindWavForEvent(const char* eventId, CUtlString& outPath);
}


