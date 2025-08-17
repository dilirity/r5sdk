//===============================================================================//
// PCM override manager for Miles raw sample source
//===============================================================================//
#include "core/stdafx.h"
#include "filesystem/filesystem.h"
#include "tier1/cvar.h"

#include "miles_pcm.h"

static ConVar miles_pcm_debug("miles_pcm_debug", "0", FCVAR_RELEASE, "Debug Miles PCM overrides");

static CUtlMap<CUtlString, MilesPcmData*>* s_pPcmCache = nullptr;
static CUtlVector<CUtlString>* s_pPendingList = nullptr;
static CThreadMutex s_pendingMutex;
static thread_local CUtlString s_tlsPending;

static void EnsureMap()
{
	if (!s_pPcmCache)
		s_pPcmCache = new CUtlMap<CUtlString, MilesPcmData*>(UtlStringLessFunc);
    if (!s_pPendingList)
        s_pPendingList = new CUtlVector<CUtlString>();
}

// Simple WAV reader (PCM only)
static bool LoadWavPcm(const char* path, MilesPcmData& out)
{
	FileHandle_t f = FileSystem()->Open(path, "rb");
	if (!f) return false;

	char riff[4]; FileSystem()->Read(riff, 4, f);
	if (memcmp(riff, "RIFF", 4) != 0) { FileSystem()->Close(f); return false; }
	FileSystem()->Seek(f, 4, FILESYSTEM_SEEK_CURRENT); // skip size
	char wave[4]; FileSystem()->Read(wave, 4, f);
	if (memcmp(wave, "WAVE", 4) != 0) { FileSystem()->Close(f); return false; }

	unsigned short audioFormat = 0;
	unsigned short numChannels = 0;
	unsigned int sampleRate = 0;
	unsigned short bitsPerSample = 0;
	size_t dataSize = 0;
	size_t dataOffset = 0;
	bool hasDataChunk = false;

	// Parse chunks
	while (!FileSystem()->EndOfFile(f))
	{
		char chunkId[4];
		if (FileSystem()->Read(chunkId, 4, f) != 4) break;
		unsigned int chunkSize = 0; FileSystem()->Read(&chunkSize, sizeof(chunkSize), f);
		if (memcmp(chunkId, "fmt ", 4) == 0)
		{
			unsigned short fmtTag = 0; FileSystem()->Read(&fmtTag, sizeof(fmtTag), f);
			unsigned short ch = 0; FileSystem()->Read(&ch, sizeof(ch), f);
			unsigned int sr = 0; FileSystem()->Read(&sr, sizeof(sr), f);
			FileSystem()->Seek(f, 6, FILESYSTEM_SEEK_CURRENT); // byte rate + block align
			unsigned short bps = 0; FileSystem()->Read(&bps, sizeof(bps), f);
			audioFormat = fmtTag; numChannels = ch; sampleRate = sr; bitsPerSample = bps;
			if (chunkSize > 16) FileSystem()->Seek(f, chunkSize - 16, FILESYSTEM_SEEK_CURRENT);
		}
		else if (memcmp(chunkId, "data", 4) == 0)
		{
			dataOffset = (size_t)FileSystem()->Tell(f);
			dataSize = (size_t)chunkSize;
			hasDataChunk = true;
			FileSystem()->Seek(f, chunkSize, FILESYSTEM_SEEK_CURRENT);
		}
		else
		{
			FileSystem()->Seek(f, chunkSize, FILESYSTEM_SEEK_CURRENT);
		}
	}

	if (audioFormat != 1 || !hasDataChunk || dataSize == 0 || (bitsPerSample != 8 && bitsPerSample != 16))
	{
		FileSystem()->Close(f);
		return false;
	}

	const int intDataSize = (int)dataSize;
	out.samples.EnsureCapacity(intDataSize);
	out.samples.SetSize(intDataSize);
	FileSystem()->Seek(f, (int)dataOffset, FILESYSTEM_SEEK_HEAD);
	FileSystem()->Read(out.samples.Base(), dataSize, f);
	FileSystem()->Close(f);

	out.sampleRate = (int)sampleRate;
	out.channels = numChannels;
	out.bitsPerSample = bitsPerSample;
	return true;
}

namespace MilesPcmOverrides
{
	bool EnsureLoaded(const char* eventId, const char* wavPath)
	{
		EnsureMap();
		if (!eventId || !*eventId) return false;
		const CUtlString key(eventId);
		if (s_pPcmCache->Find(key) != s_pPcmCache->InvalidIndex())
			return true;

		MilesPcmData* pcm = new MilesPcmData();
		if (!LoadWavPcm(wavPath, *pcm))
			return false;

		s_pPcmCache->Insert(key, pcm);
		if (miles_pcm_debug.GetBool())
			Msg(eDLL_T::AUDIO, "PCM cached for '%s': %d Hz, %u ch, %u bps, %d bytes\n",
				key.String(), pcm->sampleRate, pcm->channels, pcm->bitsPerSample, pcm->samples.Count());
		return true;
	}

	const MilesPcmData* Get(const char* eventId)
	{
		if (!eventId || !*eventId || !s_pPcmCache) return nullptr;
		const auto idx = s_pPcmCache->Find(CUtlString(eventId));
		if (idx == s_pPcmCache->InvalidIndex()) return nullptr;
		return (*s_pPcmCache)[idx];
	}

	void EnqueuePending(const char* eventId)
	{
		if (!eventId || !*eventId) return;
		AUTO_LOCK(s_pendingMutex);
		s_pPendingList->AddToTail(CUtlString(eventId));
	}

	void MarkPendingTLS(const char* eventId)
	{
		s_tlsPending = eventId ? eventId : "";
	}

	bool HasPending()
	{
		if (s_tlsPending.Length() > 0) return true;
		AUTO_LOCK(s_pendingMutex);
		return s_pPendingList && s_pPendingList->Count() > 0;
	}

	const MilesPcmData* ConsumePending()
	{
		if (s_tlsPending.Length() > 0)
		{
			const MilesPcmData* pcm = Get(s_tlsPending.String());
			s_tlsPending = CUtlString();
			if (pcm) return pcm;
		}
		AUTO_LOCK(s_pendingMutex);
		if (!s_pPendingList || s_pPendingList->Count() == 0) return nullptr;
		const CUtlString eventId = s_pPendingList->Element(0);
		s_pPendingList->Remove(0);
		return Get(eventId.String());
	}
}


