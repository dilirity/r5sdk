#pragma once

#include "tier1/utlvector.h"
#include "tier1/utlstring.h"

struct MilesPcmData
{
	CUtlVector<unsigned char> samples; // interleaved PCM
	int sampleRate = 0;
	unsigned short channels = 0;
	unsigned short bitsPerSample = 0;
};

namespace MilesPcmOverrides
{
	// Load and cache PCM for a given event from a WAV file. Returns true on success.
	bool EnsureLoaded(const char* eventId, const char* wavPath);
	// Retrieve cached PCM for event, or nullptr.
	const MilesPcmData* Get(const char* eventId);
	// Enqueue a pending override for the next Miles raw source setup (global queue).
	void EnqueuePending(const char* eventId);
	// Mark pending override for the current thread (TLS), used from MilesQueueEventRun.
	void MarkPendingTLS(const char* eventId);
	// Check/clear pending flag (any source). TLS has priority, then global queue.
	bool HasPending();
	const MilesPcmData* ConsumePending();
}


