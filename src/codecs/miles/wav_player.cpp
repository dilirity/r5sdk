//===============================================================================//
//
// Purpose: Minimal WAV one-shot playback via SDL
//
//===============================================================================//
#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "filesystem/filesystem.h"

#include "wav_player.h"

#include <windows.h>
#include <mmsystem.h>

static ConVar miles_wav_debug("miles_wav_debug", "0", FCVAR_RELEASE, "Debug WAV playback");

namespace WavPlayer
{
	bool PlayOneShot(const char* wavPath)
	{
		if (!wavPath || !*wavPath)
			return false;

		if (!FileExists(wavPath))
		{
			if (miles_wav_debug.GetBool())
				Warning(eDLL_T::AUDIO, "WAV not found: %s\n", wavPath);
			return false;
		}

		// Use WinMM to play asynchronously
		BOOL ok = PlaySoundA(wavPath, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
		if (!ok)
		{
			if (miles_wav_debug.GetBool())
				Warning(eDLL_T::AUDIO, "PlaySound failed for '%s' (err=%lu)\n", wavPath, GetLastError());
			return false;
		}
		return true;
	}
}


