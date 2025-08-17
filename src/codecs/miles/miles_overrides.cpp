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

static ConVar miles_overrides_debug("miles_overrides_debug", "0", FCVAR_RELEASE, "Debug audio override resolution");

// Keep a single global map of overrides: original -> replacement
static CUtlMap<CUtlString, CUtlString>* s_pOverrideMap = nullptr;

// Local helpers
static void EnsureMap()
{
	if (!s_pOverrideMap)
	{
		s_pOverrideMap = new CUtlMap<CUtlString, CUtlString>(UtlStringLessFunc);
	}
}

static void ClearMap()
{
	if (!s_pOverrideMap)
		return;

	for (CUtlMap<CUtlString, CUtlString>::IndexType_t i = s_pOverrideMap->FirstInorder(); i != s_pOverrideMap->InvalidIndex(); i = s_pOverrideMap->NextInorder(i))
	{
		// free nodes by resetting strings
	}

	s_pOverrideMap->Purge();
}

static void LoadOverridesFromFile(const char* filePath)
{
	CUtlBuffer buf;
	if (!FileSystem()->ReadFile(filePath, nullptr, buf))
		return;

	const RSON::eFieldType rootType = (RSON::eFieldType)(RSON::eFieldType::RSON_OBJECT | RSON::eFieldType::RSON_VALUE);
	RSON::Node_t* root = RSON::LoadFromBuffer(filePath, (char*)buf.Base(), rootType);
	if (!root || root->type != RSON::eFieldType::RSON_OBJECT)
	{
		Warning(eDLL_T::AUDIO, "Miles overrides: '%s' must be an object of original->replacement pairs\n", filePath);
		if (root)
		{
			RSON_Free(root, AlignedMemAlloc());
			AlignedMemAlloc()->Free(root);
		}
		return;
	}

	for (RSON::Field_t* kv = root->GetFirstSubKey(); kv != nullptr; kv = kv->GetNextKey())
	{
		if (kv->node.type != RSON::eFieldType::RSON_STRING)
			continue;

		const char* original = kv->name;
		const char* replacement = kv->GetString();

		// Insert/replace
		const CUtlString key(original);
		const CUtlString val(replacement);

		const CUtlMap<CUtlString, CUtlString>::IndexType_t idx = s_pOverrideMap->Find(key);
		if (idx != s_pOverrideMap->InvalidIndex())
		{
			(*s_pOverrideMap)[idx] = val;
		}
		else
		{
			s_pOverrideMap->Insert(key, val);
		}

		if (miles_overrides_debug.GetBool())
		{
			Msg(eDLL_T::AUDIO, "Miles overrides: %s -> %s (from %s)\n", key.String(), val.String(), filePath);
		}
	}

	RSON_Free(root, AlignedMemAlloc());
	AlignedMemAlloc()->Free(root);
}

namespace MilesOverrides
{
	void LoadAll()
	{
		EnsureMap();
		ClearMap();

		// Base path file
		static const char* kBaseOverridePath = "scripts/audio/overrides.rson";
		LoadOverridesFromFile(kBaseOverridePath);

		// Mods
		if (ModSystem()->IsEnabled())
		{
			ModSystem()->LockModList();
			FOR_EACH_VEC(ModSystem()->GetModList(), i)
			{
				const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];
				if (!mod->IsEnabled())
					continue;

				const CUtlString modPath = mod->GetBasePath() + kBaseOverridePath;
				LoadOverridesFromFile(modPath.String());
			}
			ModSystem()->UnlockModList();
		}
	}

	const char* Resolve(const char* originalEventName)
	{
		if (!originalEventName || !*originalEventName)
			return nullptr;
		EnsureMap();

		const CUtlMap<CUtlString, CUtlString>::IndexType_t idx = s_pOverrideMap->Find(CUtlString(originalEventName));
		if (idx == s_pOverrideMap->InvalidIndex())
			return nullptr;

		return (*s_pOverrideMap)[idx].String();
	}

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


