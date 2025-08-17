//===============================================================================//
//
// Purpose: Audio override subsystem for Miles events
//
//===============================================================================//
#include "core/stdafx.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include "filesystem/filesystem.h"
#include "rtech/rson.h"
#include "pluginsystem/modsystem.h"

#include "miles_overrides.h"

namespace MilesOverrides
{
	static bool FileExistsRelative(const CUtlString& path)
	{
		return FileExists(path.String());
	}

	bool FindWavForEvent(const char* eventId, CUtlString& outPath)
	{
		if (!eventId || !*eventId)
			return false;

		// Iterate enabled mods, newest first.
		if (!ModSystem()->IsEnabled())
			return false;

		auto MakeFlattened = [](const char* s) -> CUtlString {
			char buf[1024];
			int len = 0;
			for (const char* p = s; *p && len < (int)(sizeof(buf) - 1); ++p)
			{
				char ch = *p;
				if (ch == '/' || ch == '\\') ch = '_';
				buf[len++] = ch;
			}
			buf[len] = '\0';
			return CUtlString(buf);
		};

		auto LastSegment = [](const char* s) -> const char* {
			const char* a = strrchr(s, '/');
			const char* b = strrchr(s, '\\');
			const char* lastSlash = (a > b) ? a : b;
			return lastSlash ? lastSlash + 1 : s;
		};

		const CUtlString flattened = MakeFlattened(eventId);
		const char* lastSeg = LastSegment(eventId);

		bool found = false;
		ModSystem()->LockModList();
		FOR_EACH_VEC(ModSystem()->GetModList(), i)
		{
			const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];
			if (!mod->IsEnabled())
				continue;

			// Candidates:
			// 1) Raw nested folder using full event id as directory path; filename is last segment
			CUtlString wavPath1 = mod->GetBasePath() + CUtlString("audio/") + eventId + "/" + lastSeg + ".wav";
			// 2) Flattened folder name; filename flattened
			CUtlString wavPath2 = mod->GetBasePath() + CUtlString("audio/") + flattened + "/" + flattened + ".wav";

			if (FileExistsRelative(wavPath1))
			{
				outPath = wavPath1;
				found = true;
				break;
			}

			if (FileExistsRelative(wavPath2))
			{
				outPath = wavPath2;
				found = true;
				break;
			}
		}
		ModSystem()->UnlockModList();

		return found;
	}
}


