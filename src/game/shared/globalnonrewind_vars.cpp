//=============================================================================//
//
// Purpose: GlobalNonRewind networked variable system
//
// SDK-side key-value store for global non-rewind network variables.
// Both VMs share the same storage (listen server = same process).
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "globalnonrewind_vars.h"
#include "game/shared/scriptnetdata_ext.h"
#include "game/shared/dt_injection.h"
#include "game/client/scriptnetdata_client.h"

#ifdef CLIENT_DLL
#include "game/client/cliententitylist.h"
#include "game/client/c_baseentity.h"
#endif

#include <unordered_map>
#include <string>

//-----------------------------------------------------------------------------
// Entity data array offsets within CScriptNetDataGlobal (from DT RE)
//-----------------------------------------------------------------------------
static constexpr int ENT_OFFSET_BOOLS    = 0xC40;  // m_bools[16], 1 byte each
static constexpr int ENT_OFFSET_RANGES   = 0xC50;  // m_ranges[32], 2 bytes each (uint16)
static constexpr int ENT_OFFSET_INT32S   = 0xC90;  // m_int32s[8], 4 bytes each
static constexpr int ENT_OFFSET_TIMES    = 0xCB0;  // m_times[24], 4 bytes each (float)
static constexpr int ENT_OFFSET_ENTITIES = 0xD10;  // m_entities[16], 4 bytes each (ehandle)

//-----------------------------------------------------------------------------
// Entity-backed read/write helpers
//
// When the NonRewind entity exists, these operate directly on its memory.
// The DT system replicates changes automatically when edict is marked dirty.
//-----------------------------------------------------------------------------
static bool EntitySetBool(const char* name, bool value)
{
	if (!g_pScriptNetDataNonRewindEnt) return false;
	int slot = ScriptNetData_FindVarSlot(name, SNDC_GLOBAL_NON_REWIND);
	if (slot < 0) return false;

	*reinterpret_cast<uint8_t*>(
		reinterpret_cast<uintptr_t>(g_pScriptNetDataNonRewindEnt) + ENT_OFFSET_BOOLS + slot) = value ? 1 : 0;
	DTInject_MarkEntityDirty(g_pScriptNetDataNonRewindEnt);
	return true;
}

static bool EntityGetBool(const char* name, bool& out)
{
	if (!g_pScriptNetDataNonRewindEnt) return false;
	int slot = ScriptNetData_FindVarSlot(name, SNDC_GLOBAL_NON_REWIND);
	if (slot < 0) return false;

	out = *reinterpret_cast<uint8_t*>(
		reinterpret_cast<uintptr_t>(g_pScriptNetDataNonRewindEnt) + ENT_OFFSET_BOOLS + slot) != 0;
	return true;
}

static bool EntitySetInt(const char* name, int value)
{
	if (!g_pScriptNetDataNonRewindEnt) return false;
	int slot = ScriptNetData_FindVarSlot(name, SNDC_GLOBAL_NON_REWIND);
	if (slot < 0) return false;

	// SNVT_INT: 10-bit signed in m_ranges (uint16), range -512..511
	*reinterpret_cast<uint16_t*>(
		reinterpret_cast<uintptr_t>(g_pScriptNetDataNonRewindEnt) + ENT_OFFSET_RANGES + slot * 2)
		= static_cast<uint16_t>(value & 0x3FF);
	DTInject_MarkEntityDirty(g_pScriptNetDataNonRewindEnt);
	return true;
}

static bool EntityGetInt(const char* name, int& out)
{
	if (!g_pScriptNetDataNonRewindEnt) return false;
	int slot = ScriptNetData_FindVarSlot(name, SNDC_GLOBAL_NON_REWIND);
	if (slot < 0) return false;

	uint16_t raw = *reinterpret_cast<uint16_t*>(
		reinterpret_cast<uintptr_t>(g_pScriptNetDataNonRewindEnt) + ENT_OFFSET_RANGES + slot * 2);
	out = (raw < 0x200) ? static_cast<int>(raw) : static_cast<int>(raw) - 1024;
	return true;
}

static bool EntitySetFloat(const char* name, float value)
{
	if (!g_pScriptNetDataNonRewindEnt) return false;
	int slot = ScriptNetData_FindVarSlot(name, SNDC_GLOBAL_NON_REWIND);
	if (slot < 0) return false;

	*reinterpret_cast<float*>(
		reinterpret_cast<uintptr_t>(g_pScriptNetDataNonRewindEnt) + ENT_OFFSET_TIMES + slot * 4) = value;
	DTInject_MarkEntityDirty(g_pScriptNetDataNonRewindEnt);
	return true;
}

static bool EntityGetFloat(const char* name, float& out)
{
	if (!g_pScriptNetDataNonRewindEnt) return false;
	int slot = ScriptNetData_FindVarSlot(name, SNDC_GLOBAL_NON_REWIND);
	if (slot < 0) return false;

	out = *reinterpret_cast<float*>(
		reinterpret_cast<uintptr_t>(g_pScriptNetDataNonRewindEnt) + ENT_OFFSET_TIMES + slot * 4);
	return true;
}

//-----------------------------------------------------------------------------
// Fallback key-value storage (used when entity isn't available)
//-----------------------------------------------------------------------------
struct NonRewindVar_t
{
	enum Type { BOOL, INT, FLOAT, TIME, ENT };

	Type type = BOOL;
	union
	{
		bool bVal;
		int iVal;
		float fVal;
	};

	NonRewindVar_t() : type(BOOL), iVal(0) {}
};

static std::unordered_map<std::string, NonRewindVar_t> s_nonRewindVars;
static constexpr size_t MAX_NONREWIND_VARS = 1024;

//-----------------------------------------------------------------------------
// Setters
//-----------------------------------------------------------------------------
static constexpr size_t MAX_NONREWIND_NAME_LEN = 256;

static bool EnsureVarSlot(const char* name)
{
	if (s_nonRewindVars.count(name))
		return true;
	if (strlen(name) > MAX_NONREWIND_NAME_LEN)
	{
		Warning(eDLL_T::SERVER, "GlobalNonRewind: Variable name too long (%zu > %zu), rejecting\n",
			strlen(name), MAX_NONREWIND_NAME_LEN);
		return false;
	}
	if (s_nonRewindVars.size() >= MAX_NONREWIND_VARS)
	{
		Warning(eDLL_T::SERVER, "GlobalNonRewind: Variable limit reached (%zu), rejecting '%s'\n",
			MAX_NONREWIND_VARS, name);
		return false;
	}
	return true;
}
SQRESULT Script_SetGlobalNonRewindNetBool(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	SQBool value;
	sq_getbool(v, 3, &value);

	if (EntitySetBool(name, value != 0))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	if (!EnsureVarSlot(name))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::BOOL;
	var.bVal = (value != 0);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_SetGlobalNonRewindNetInt(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	SQInteger value;
	sq_getinteger(v, 3, &value);

	if (EntitySetInt(name, static_cast<int>(value)))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	if (!EnsureVarSlot(name))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::INT;
	var.iVal = static_cast<int>(value);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_SetGlobalNonRewindNetFloat(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	SQFloat value;
	sq_getfloat(v, 3, &value);

	if (EntitySetFloat(name, static_cast<float>(value)))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	if (!EnsureVarSlot(name))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::FLOAT;
	var.fVal = static_cast<float>(value);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_SetGlobalNonRewindNetTime(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	SQFloat value;
	sq_getfloat(v, 3, &value);

	if (EntitySetFloat(name, static_cast<float>(value)))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	if (!EnsureVarSlot(name))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::TIME;
	var.fVal = static_cast<float>(value);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Getters
//-----------------------------------------------------------------------------
SQRESULT Script_GetGlobalNonRewindNetBool(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	bool bVal = false;
	if (EntityGetBool(name, bVal))
	{
		sq_pushbool(v, bVal);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	auto it = s_nonRewindVars.find(name);
	if (it != s_nonRewindVars.end() && it->second.type == NonRewindVar_t::BOOL)
		sq_pushbool(v, it->second.bVal);
	else
		sq_pushbool(v, false);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_GetGlobalNonRewindNetInt(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	int iVal = 0;
	if (EntityGetInt(name, iVal))
	{
		sq_pushinteger(v, iVal);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	auto it = s_nonRewindVars.find(name);
	if (it != s_nonRewindVars.end() && it->second.type == NonRewindVar_t::INT)
		sq_pushinteger(v, it->second.iVal);
	else
		sq_pushinteger(v, 0);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_GetGlobalNonRewindNetFloat(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	float fVal = 0.0f;
	if (EntityGetFloat(name, fVal))
	{
		sq_pushfloat(v, fVal);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	auto it = s_nonRewindVars.find(name);
	if (it != s_nonRewindVars.end() && it->second.type == NonRewindVar_t::FLOAT)
		sq_pushfloat(v, it->second.fVal);
	else
		sq_pushfloat(v, 0.0f);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_GetGlobalNonRewindNetTime(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	float fVal = 0.0f;
	if (EntityGetFloat(name, fVal))
	{
		sq_pushfloat(v, fVal);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	auto it = s_nonRewindVars.find(name);
	if (it != s_nonRewindVars.end() && it->second.type == NonRewindVar_t::TIME)
		sq_pushfloat(v, it->second.fVal);
	else
		sq_pushfloat(v, 0.0f);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Entity net var (Set/Get by name, stores entity EHANDLE)
//-----------------------------------------------------------------------------
// m_RefEHandle offset in C_BaseEntity
static constexpr int ENTITY_REFEHANDLE_OFFSET = 0x08;
static constexpr uint32_t NONREWIND_INVALID_EHANDLE = 0xFFFFFFFF;
static constexpr uint32_t NONREWIND_ENT_ENTRY_MASK  = 0xFFFF;

SQRESULT Script_SetGlobalNonRewindNetEnt(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	if (!EnsureVarSlot(name))
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	void* pEnt = nullptr;
	const bool gotEntity = v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEnt));

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::ENT;
	// Store the entity's EHANDLE (m_RefEHandle at +0x08); INVALID if null or extraction failed
	var.iVal = (gotEntity && pEnt)
		? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + ENTITY_REFEHANDLE_OFFSET)
		: NONREWIND_INVALID_EHANDLE;
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_GetGlobalNonRewindNetEnt(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	auto it = s_nonRewindVars.find(name);
	if (it == s_nonRewindVars.end() || it->second.type != NonRewindVar_t::ENT)
	{
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

#ifdef CLIENT_DLL
	const uint32_t ehandle = static_cast<uint32_t>(it->second.iVal);
	if (ehandle != NONREWIND_INVALID_EHANDLE && g_pClientEntityList)
	{
		// Extract entity index from low 16 bits of EHANDLE (NONREWIND_ENT_ENTRY_MASK)
		const int entIndex = ehandle & NONREWIND_ENT_ENTRY_MASK;
		IClientEntity* pClient = v_ClientEntityList_GetClientEntity(g_pClientEntityList, entIndex);

		if (pClient)
		{
			C_BaseEntity* pEnt = reinterpret_cast<C_BaseEntity*>(pClient);

			// Validate serial number: stored EHANDLE must match entity's current m_RefEHandle
			const uint32_t currentHandle = *reinterpret_cast<uint32_t*>(
				reinterpret_cast<uintptr_t>(pEnt) + ENTITY_REFEHANDLE_OFFSET);
			if (currentHandle == ehandle)
			{
				const HSCRIPT scriptHandle = v_C_BaseEntity__GetScriptInstance(pEnt);
				if (scriptHandle)
				{
					SQObject entObj;
					entObj._type = OT_ENTITY;
					entObj._pad = 0;
					entObj._unVal.pInstance = reinterpret_cast<SQInstance*>(scriptHandle);
					sq_pushobject(v, entObj);
					SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
				}
			}
		}
	}
#endif

	// Entity not found, deleted, or running on server — return null
	sq_pushnull(v);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Per-player NonRewind data (respawn time, music pack)
//-----------------------------------------------------------------------------
struct PlayerNonRewindData_t
{
	float respawnTime = 0.0f;
	int musicPack = 0;
};

// Keyed by EHANDLE (m_RefEHandle) instead of raw pointer to prevent ABA reuse
static std::unordered_map<uint32_t, PlayerNonRewindData_t> s_playerNonRewindData;

// Extract EHANDLE from entity pointer, returns NONREWIND_INVALID_EHANDLE on failure
static uint32_t GetPlayerEHandle(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)) || !pPlayer)
		return NONREWIND_INVALID_EHANDLE;

	return static_cast<uint32_t>(
		*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pPlayer) + ENTITY_REFEHANDLE_OFFSET));
}

SQRESULT Script_GetNonRewindRespawnTime(HSQUIRRELVM v)
{
	const uint32_t ehandle = GetPlayerEHandle(v);
	if (ehandle == NONREWIND_INVALID_EHANDLE)
		return SQ_ERROR;

	auto it = s_playerNonRewindData.find(ehandle);
	float val = (it != s_playerNonRewindData.end()) ? it->second.respawnTime : 0.0f;

	sq_pushfloat(v, val);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_SetNonRewindRespawnTime(HSQUIRRELVM v)
{
	const uint32_t ehandle = GetPlayerEHandle(v);
	if (ehandle == NONREWIND_INVALID_EHANDLE)
		return SQ_ERROR;

	SQFloat time;
	sq_getfloat(v, 2, &time);

	s_playerNonRewindData[ehandle].respawnTime = static_cast<float>(time);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_GetNonRewindMusicPack(HSQUIRRELVM v)
{
	const uint32_t ehandle = GetPlayerEHandle(v);
	if (ehandle == NONREWIND_INVALID_EHANDLE)
		return SQ_ERROR;

	auto it = s_playerNonRewindData.find(ehandle);
	int val = (it != s_playerNonRewindData.end()) ? it->second.musicPack : 0;

	sq_pushinteger(v, val);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

SQRESULT Script_SetNonRewindMusicPack(HSQUIRRELVM v)
{
	const uint32_t ehandle = GetPlayerEHandle(v);
	if (ehandle == NONREWIND_INVALID_EHANDLE)
		return SQ_ERROR;

	SQInteger pack;
	sq_getinteger(v, 2, &pack);

	s_playerNonRewindData[ehandle].musicPack = static_cast<int>(pack);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Level shutdown
//-----------------------------------------------------------------------------
void GlobalNonRewind_LevelShutdown()
{
	s_nonRewindVars.clear();
	s_playerNonRewindData.clear();
}
