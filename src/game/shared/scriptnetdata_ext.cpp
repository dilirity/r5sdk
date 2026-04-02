//=============================================================================//
//
// Purpose: ScriptNetData Extension — SNDC_GLOBAL_NON_REWIND (category 5)
//
// Engine has 5 ScriptNetData categories (0-4). This module adds a 6th
// (GLOBAL_NON_REWIND) with its own entity:
//
//   1. Expands scriptNetCategories arrays from 5→6 entries (server + client)
//   2. Creates a second CScriptNetDataGlobal entity for category 5
//   3. Hooks the variable lookup functions to route category 5 to our entity
//
// The engine's bounds check is separately patched (cmp r15d, 4 → 5) via
// patch_scriptnetvars.py.
//
//=============================================================================//

#include "core/stdafx.h"
#include "scriptnetdata_ext.h"
#include "game/client/scriptnetdata_client.h"

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------
static constexpr int SERVER_CATEGORY_SIZE = 0x20C;  // 524 bytes per server category
static constexpr int CLIENT_CATEGORY_SIZE = 0x108;  // 264 bytes per client category
static constexpr int ORIG_CATEGORY_COUNT = 5;
static constexpr int NEW_CATEGORY_COUNT = 6;
static constexpr int INTERNAL_TYPE_COUNT = 5;
static constexpr int SERVER_NETVAR_ENTRY_SIZE = 0x38;  // 56 bytes per server scriptNetVars entry
static constexpr int CLIENT_NETVAR_ENTRY_SIZE = 0x20;  // 32 bytes per client scriptNetVars entry
static constexpr int NETVAR_CATEGORY_OFFSET = 0x08;    // category field at +8 in both

// Default slot limits for SNDC_GLOBAL_NON_REWIND (matches SNDC_GLOBAL)
static constexpr int NON_REWIND_LIMITS[INTERNAL_TYPE_COUNT] = { 16, 32, 8, 24, 16 };

//------------------------------------------------------------------------------
// State
//------------------------------------------------------------------------------
static uint8_t* s_pExtServerCategories = nullptr;
static uint8_t* s_pExtClientCategories = nullptr;
static uint8_t* s_pOrigServerCategories = nullptr;
static uint8_t* s_pOrigClientCategories = nullptr;

// Entity pointers for SNDC_GLOBAL_NON_REWIND
// g_pScriptNetDataNonRewindEnt is the externally-visible pointer (declared in header)
static void* s_pNonRewindClientEnt = nullptr;

// Engine global entity pointers (we read/swap these)
static void** g_ppServerGlobalEnt = nullptr;   // qword_16859E480
static void** g_ppClientGlobalEnt = nullptr;   // qword_1695075E8

// Engine scriptNetVars hash tables (for category lookup)
static uint8_t* g_pServerNetVars = nullptr;    // unk_1685A01A0
static uint8_t* g_pClientNetVars = nullptr;    // unk_169509820

// Hook targets
static char (*v_ServerFindGlobalNetVar)(__int64 a1, int* a2) = nullptr;
static char (*v_ClientFindGlobalNetVar)(__int64 a1, int* a2) = nullptr;

// Entity constructor (server — creates CScriptNetDataGlobal)
static void* (*v_ServerCreateScriptNetDataGlobal)(__int64 a1, __int64 a2) = nullptr;

//------------------------------------------------------------------------------
// Category xref tables
//------------------------------------------------------------------------------
struct CategoryXref
{
	uint32_t rva;
	int32_t offset;
};

static constexpr CategoryXref SERVER_CATEGORY_XREFS[] = {
	{ 0x781E9D, 0 },   // ScriptNetDataInit       lea rcx, categories (memset)
	{ 0x781EAF, 8 },   // ScriptNetDataInit       lea rdx, categories+8 (limit copy)
	{ 0x7DCF57, 0 },   // GetScriptNetTimeDefault
	{ 0x86EDE8, 0 },   // GetScriptNetTime
	{ 0x888DEB, 0 },   // AllocateInternalVar(srv)
	{ 0x889003, 0 },   // RegisterNetworkedVar(srv)
	{ 0x889B16, 0 },   // GetScriptNetInt(srv)
	{ 0x889BC6, 0 },   // GetScriptNetBool(srv)
	{ 0x88A645, 0 },   // UpdateCallbacks(srv)
	{ 0x88AA32, 0 },   // TriggerChangeCallbacks
	{ 0x8EBE2C, 0 },   // ChangeCallback<BOOL>
	{ 0x8EC0E3, 0 },   // ChangeCallback<INT>
	{ 0x8EC3A0, 0 },   // ChangeCallback<FLOAT>
	{ 0x8EC660, 0 },   // ChangeCallback<TIME>
	{ 0x8EC920, 0 },   // ChangeCallback<ENT>
};
static constexpr int SERVER_XREF_COUNT = _countof(SERVER_CATEGORY_XREFS);

static constexpr CategoryXref CLIENT_CATEGORY_XREFS[] = {
	{ 0xB62C5A, 0 },
	{ 0xBFA87E, 0 },
	{ 0xC1820C, 0 },
	{ 0xC1837B, 0 },   // AllocateInternalVar(cl)
	{ 0xC18592, 0 },   // RegisterNetworkedVar(cl)
	{ 0xC19096, 0 },
	{ 0xC19136, 0 },
	{ 0xE517E1, 0 },   // ClientInit (memset)
	{ 0xE517F3, 8 },   // ClientInit (limit copy)
	{ 0xE5204C, 0 },
	{ 0xE524B2, 0 },
};
static constexpr int CLIENT_XREF_COUNT = _countof(CLIENT_CATEGORY_XREFS);

//------------------------------------------------------------------------------
// Patch RIP-relative LEA displacements
//------------------------------------------------------------------------------
static bool PatchCategoryXrefs(uintptr_t moduleBase, const CategoryXref* xrefs, int count,
	uint8_t* pOrigArray, uint8_t* pNewArray)
{
	int patched = 0;
	for (int i = 0; i < count; i++)
	{
		uint8_t* pInstr = reinterpret_cast<uint8_t*>(moduleBase + xrefs[i].rva);
		uint8_t* pDispField = pInstr + 3;
		int32_t oldDisp = *reinterpret_cast<int32_t*>(pDispField);
		uintptr_t nextRIP = reinterpret_cast<uintptr_t>(pInstr) + 7;
		uintptr_t expectedAddr = reinterpret_cast<uintptr_t>(pOrigArray) + xrefs[i].offset;
		uintptr_t resolvedAddr = nextRIP + oldDisp;

		if (resolvedAddr != expectedAddr)
		{
			Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] xref[%d] RVA=0x%X resolved=0x%p expected=0x%p\n",
				i, xrefs[i].rva, (void*)resolvedAddr, (void*)expectedAddr);
			continue;
		}

		uintptr_t newTarget = reinterpret_cast<uintptr_t>(pNewArray) + xrefs[i].offset;
		int64_t newDisp64 = static_cast<int64_t>(newTarget) - static_cast<int64_t>(nextRIP);
		if (newDisp64 > INT32_MAX || newDisp64 < INT32_MIN)
		{
			Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] xref[%d] displacement overflow\n", i);
			continue;
		}

		DWORD oldProtect;
		VirtualProtect(pDispField, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
		*reinterpret_cast<int32_t*>(pDispField) = static_cast<int32_t>(newDisp64);
		VirtualProtect(pDispField, 4, oldProtect, &oldProtect);
		patched++;
	}

	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] Patched %d/%d category array references\n", patched, count);
	return patched == count;
}

//------------------------------------------------------------------------------
// Allocate extended category array near module (within RIP-relative reach)
//------------------------------------------------------------------------------
static uint8_t* CreateExtendedCategoryArray(int categorySize, const char* side)
{
	const size_t newSize = NEW_CATEGORY_COUNT * categorySize;
	uint8_t* pNew = nullptr;
	uintptr_t moduleBase = g_GameDll.GetModuleBase();

	for (int dir = 1; dir >= -1 && !pNew; dir -= 2)
	{
		uintptr_t scanAddr = (moduleBase + 0x10000000ULL) & ~0xFFFFULL;
		if (dir < 0) scanAddr = (moduleBase - 0x10000000ULL) & ~0xFFFFULL;

		for (int attempt = 0; attempt < 4096 && !pNew; attempt++)
		{
			uintptr_t tryAddr = scanAddr + dir * attempt * 0x10000ULL;
			int64_t dist = static_cast<int64_t>(tryAddr) - static_cast<int64_t>(moduleBase);
			if (dist > 0x70000000LL || dist < -0x70000000LL) break;

			MEMORY_BASIC_INFORMATION mbi;
			if (VirtualQuery(reinterpret_cast<void*>(tryAddr), &mbi, sizeof(mbi)) == sizeof(mbi))
			{
				if (mbi.State == MEM_FREE && mbi.RegionSize >= newSize)
				{
					pNew = reinterpret_cast<uint8_t*>(
						VirtualAlloc(reinterpret_cast<void*>(tryAddr), newSize,
							MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
				}
			}
		}
	}

	if (!pNew) return nullptr;

	// Init category 5 slot limits
	uint8_t* pCat5 = pNew + SNDC_GLOBAL_NON_REWIND * categorySize;
	int* pLimits = reinterpret_cast<int*>(pCat5);
	for (int i = 0; i < INTERNAL_TYPE_COUNT; i++)
		pLimits[i] = NON_REWIND_LIMITS[i];

	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] Created extended %s category array at 0x%p\n", side, pNew);
	return pNew;
}

//------------------------------------------------------------------------------
// Entity creation hook — when the engine creates CScriptNetDataGlobal on
// server, we create a second instance for SNDC_GLOBAL_NON_REWIND.
//------------------------------------------------------------------------------
static void* Hook_ServerCreateScriptNetDataGlobal(__int64 a1, __int64 a2)
{
	// Let engine create the original GLOBAL entity
	void* result = v_ServerCreateScriptNetDataGlobal(a1, a2);

	if (result && g_ppServerGlobalEnt && *g_ppServerGlobalEnt && !g_pScriptNetDataNonRewindEnt)
	{
		// Save the GLOBAL entity pointer
		void* originalEnt = *g_ppServerGlobalEnt;

		// Create a second entity — constructor will overwrite g_ppServerGlobalEnt
		void* nonRewindEnt = v_ServerCreateScriptNetDataGlobal(a1, a2);

		if (nonRewindEnt)
		{
			// Save our entity
			g_pScriptNetDataNonRewindEnt = *g_ppServerGlobalEnt;

			// Restore original GLOBAL entity pointer
			*g_ppServerGlobalEnt = originalEnt;

			Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] Created NON_REWIND server entity at 0x%p (GLOBAL at 0x%p)\n",
				g_pScriptNetDataNonRewindEnt, originalEnt);
		}
	}

	return result;
}

//------------------------------------------------------------------------------
// Lookup hooks — route category 5 variables to our entity
//
// The lookup function is called before every Get/Set operation:
//   if (FindGlobalNetVar(vm, &varIdx))
//       SetOnEntity(*g_ppGlobalEnt, varIdx, value)
//
// For category 5 vars, we swap the global entity pointer to our entity.
// We restore it at the start of the NEXT lookup call.
//------------------------------------------------------------------------------
static thread_local void** s_tls_swappedPtr = nullptr;
static thread_local void*  s_tls_savedValue = nullptr;

static void RestoreEntityPointer()
{
	if (s_tls_swappedPtr)
	{
		*s_tls_swappedPtr = s_tls_savedValue;
		s_tls_swappedPtr = nullptr;
	}
}

static char Hook_ServerFindGlobalNetVar(__int64 a1, int* a2)
{
	RestoreEntityPointer();

	char result = v_ServerFindGlobalNetVar(a1, a2);

	if (result && a2 && *a2 >= 0 && g_pServerNetVars && g_pScriptNetDataNonRewindEnt && g_ppServerGlobalEnt)
	{
		int category = *reinterpret_cast<int*>(g_pServerNetVars + (*a2) * SERVER_NETVAR_ENTRY_SIZE + NETVAR_CATEGORY_OFFSET);
		if (category == SNDC_GLOBAL_NON_REWIND)
		{
			s_tls_savedValue = *g_ppServerGlobalEnt;
			s_tls_swappedPtr = g_ppServerGlobalEnt;
			*g_ppServerGlobalEnt = g_pScriptNetDataNonRewindEnt;
		}
	}

	return result;
}

static char Hook_ClientFindGlobalNetVar(__int64 a1, int* a2)
{
	RestoreEntityPointer();

	char result = v_ClientFindGlobalNetVar(a1, a2);

	if (result && a2 && *a2 >= 0 && g_pClientNetVars && s_pNonRewindClientEnt && g_ppClientGlobalEnt)
	{
		int category = *reinterpret_cast<int*>(g_pClientNetVars + (*a2) * CLIENT_NETVAR_ENTRY_SIZE + NETVAR_CATEGORY_OFFSET);
		if (category == SNDC_GLOBAL_NON_REWIND)
		{
			s_tls_savedValue = *g_ppClientGlobalEnt;
			s_tls_swappedPtr = g_ppClientGlobalEnt;
			*g_ppClientGlobalEnt = s_pNonRewindClientEnt;
		}
	}

	return result;
}

//------------------------------------------------------------------------------
// Var slot lookup for NonRewind natives
//
// Searches the engine's scriptNetVars hash table for a variable by name,
// verifying it belongs to the specified category.
// Returns the slot index, or -1 on failure.
//------------------------------------------------------------------------------
static uint64_t HashVarName64(const char* name)
{
	// Squirrel _hashstr — must match SQString._hash stored in scriptNetVars
	size_t l = strlen(name);
	uint64_t h = static_cast<uint64_t>(l);
	size_t step = (l >> 5) + 1;
	for (size_t l1 = l; l1 >= step; l1 -= step)
		h = h ^ ((h << 5) + (h >> 2) + static_cast<uint8_t>(name[l1 - 1]));
	return h;
}

int ScriptNetData_FindVarSlot(const char* name, int expectedCategory)
{
	if (!g_pServerNetVars)
		return -1;

	static constexpr int BUCKET_COUNT = 250;

	uint64_t hash = HashVarName64(name);
	uint64_t startBucket = hash % BUCKET_COUNT;

	for (int probe = 0; probe < BUCKET_COUNT; probe++)
	{
		uint64_t idx = (startBucket + probe) % BUCKET_COUNT;
		uintptr_t entry = reinterpret_cast<uintptr_t>(g_pServerNetVars)
			+ SERVER_NETVAR_ENTRY_SIZE * idx;

		uint64_t storedHash = *reinterpret_cast<uint64_t*>(entry);
		if (storedHash == 0)
			return -1; // Empty slot — var not registered

		if (storedHash == hash)
		{
			int category = *reinterpret_cast<int*>(entry + NETVAR_CATEGORY_OFFSET);
			if (category == expectedCategory)
				return *reinterpret_cast<int*>(entry + 0x10); // slot index at +0x10
		}
	}

	return -1;
}

//------------------------------------------------------------------------------
// Lifecycle: clear NonRewind entity on map shutdown
//------------------------------------------------------------------------------
void ScriptNetDataExt_LevelShutdown()
{
	if (g_pScriptNetDataNonRewindEnt)
	{
		Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] NonRewind entity cleared on level shutdown\n");
		g_pScriptNetDataNonRewindEnt = nullptr;
	}
	s_pNonRewindClientEnt = nullptr;
}

//------------------------------------------------------------------------------
// IDetour implementation
//------------------------------------------------------------------------------
void VScriptNetDataExt::GetFun(void) const
{
	uintptr_t base = g_GameDll.GetModuleBase();
	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] GetFun() running, module base=0x%p\n", (void*)base);

	// Server FindGlobalNetVar: sub_14088ADF0
	v_ServerFindGlobalNetVar = reinterpret_cast<decltype(v_ServerFindGlobalNetVar)>(base + 0x88ADF0);

	// Client FindGlobalNetVar: sub_140C197B0
	v_ClientFindGlobalNetVar = reinterpret_cast<decltype(v_ClientFindGlobalNetVar)>(base + 0xC197B0);

	// Server CScriptNetDataGlobal constructor: sub_14088AD00
	v_ServerCreateScriptNetDataGlobal = reinterpret_cast<decltype(v_ServerCreateScriptNetDataGlobal)>(base + 0x88AD00);

	// Validate by checking first bytes
	auto checkByte = [](void* fn, uint8_t expected) -> bool {
		return fn && *reinterpret_cast<uint8_t*>(fn) == expected;
	};

	if (!checkByte(v_ServerFindGlobalNetVar, 0x48))
	{
		Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] ServerFindGlobalNetVar validation FAILED\n");
		v_ServerFindGlobalNetVar = nullptr;
	}
	if (!checkByte(v_ClientFindGlobalNetVar, 0x48))
	{
		Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] ClientFindGlobalNetVar validation FAILED\n");
		v_ClientFindGlobalNetVar = nullptr;
	}
	if (!checkByte(v_ServerCreateScriptNetDataGlobal, 0x48))
	{
		Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] ServerCreateGlobalEnt validation FAILED\n");
		v_ServerCreateScriptNetDataGlobal = nullptr;
	}
}

void VScriptNetDataExt::GetVar(void) const
{
	// Server scriptNetCategories
	CMemory srvPat = Module_FindPattern(g_GameDll,
		"33 D2 48 8D 0D ?? ?? ?? ?? 41 B8 3C 0A 00 00");
	if (srvPat)
		s_pOrigServerCategories = srvPat.Offset(0x2).ResolveRelativeAddress(0x3, 0x7).RCast<uint8_t*>();

	// Client scriptNetCategories
	CMemory clPat = Module_FindPattern(g_GameDll,
		"48 69 F8 08 01 00 00 4D 33 D3 48 8D 05");
	if (clPat)
		s_pOrigClientCategories = clPat.Offset(0xA).ResolveRelativeAddress(0x3, 0x7).RCast<uint8_t*>();

	// Direct RVA resolution for entity pointers and scriptNetVars
	uintptr_t base = g_GameDll.GetModuleBase();

	// Server global entity pointer: qword_16859E480
	g_ppServerGlobalEnt = reinterpret_cast<void**>(base + 0x2859E480);

	// Client global entity pointer: qword_1695075E8
	g_ppClientGlobalEnt = reinterpret_cast<void**>(base + 0x295075E8);

	// Server scriptNetVars: unk_1685A01A0
	g_pServerNetVars = reinterpret_cast<uint8_t*>(base + 0x285A01A0);

	// Client scriptNetVars: unk_169509820
	g_pClientNetVars = reinterpret_cast<uint8_t*>(base + 0x29509820);

	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] ServerCategories=0x%p ClientCategories=0x%p\n",
		s_pOrigServerCategories, s_pOrigClientCategories);
	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] ServerGlobalEnt=0x%p ClientGlobalEnt=0x%p\n",
		g_ppServerGlobalEnt, g_ppClientGlobalEnt);
	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] ServerNetVars=0x%p ClientNetVars=0x%p\n",
		g_pServerNetVars, g_pClientNetVars);
	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] FindGlobalNetVar: server=0x%p client=0x%p\n",
		v_ServerFindGlobalNetVar, v_ClientFindGlobalNetVar);
	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] CreateGlobalEnt: server=0x%p\n",
		v_ServerCreateScriptNetDataGlobal);
}

void VScriptNetDataExt::Detour(const bool bAttach) const
{
	uintptr_t moduleBase = g_GameDll.GetModuleBase();

	// --- Expand category arrays ---
	if (s_pOrigServerCategories)
	{
		s_pExtServerCategories = CreateExtendedCategoryArray(SERVER_CATEGORY_SIZE, "server");
		if (s_pExtServerCategories)
		{
			PatchCategoryXrefs(moduleBase, SERVER_CATEGORY_XREFS, SERVER_XREF_COUNT,
				s_pOrigServerCategories, s_pExtServerCategories);
		}
	}

	if (s_pOrigClientCategories)
	{
		s_pExtClientCategories = CreateExtendedCategoryArray(CLIENT_CATEGORY_SIZE, "client");
		if (s_pExtClientCategories)
		{
			PatchCategoryXrefs(moduleBase, CLIENT_CATEGORY_XREFS, CLIENT_XREF_COUNT,
				s_pOrigClientCategories, s_pExtClientCategories);
		}
	}

	// --- Hook entity constructor to create NON_REWIND entity ---
	if (v_ServerCreateScriptNetDataGlobal)
		DetourSetup(&v_ServerCreateScriptNetDataGlobal, &Hook_ServerCreateScriptNetDataGlobal, bAttach);

	// --- Hook lookup functions to route category 5 ---
	if (v_ServerFindGlobalNetVar)
		DetourSetup(&v_ServerFindGlobalNetVar, &Hook_ServerFindGlobalNetVar, bAttach);

	if (v_ClientFindGlobalNetVar)
		DetourSetup(&v_ClientFindGlobalNetVar, &Hook_ClientFindGlobalNetVar, bAttach);

	Warning(eDLL_T::ENGINE, "[ScriptNetDataExt] SNDC_GLOBAL_NON_REWIND (category %d) — limits: bool=%d range=%d bigint=%d time=%d entity=%d\n",
		SNDC_GLOBAL_NON_REWIND,
		NON_REWIND_LIMITS[0], NON_REWIND_LIMITS[1], NON_REWIND_LIMITS[2],
		NON_REWIND_LIMITS[3], NON_REWIND_LIMITS[4]);
}
