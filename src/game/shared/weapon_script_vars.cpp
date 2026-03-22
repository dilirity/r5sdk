//=============================================================================//
//
// Purpose: Extended weapon script variables (ScriptFloat0, ScriptInt1, ScriptTime1)
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "weapon_script_vars.h"
#ifndef CLIENT_DLL
#include "game/server/r1/weapon_x.h"
#else
#include "game/client/r1/c_weapon_x.h"
#include "game/client/cliententitylist.h"
#endif

#include <unordered_map>

struct WeaponExtScriptVars
{
	float scriptFloat0 = 0.0f;
	int scriptInt1 = 0;
	float scriptTime1 = 0.0f;
};

static std::unordered_map<uintptr_t, WeaponExtScriptVars> s_weaponScriptVars;

static WeaponExtScriptVars& GetWeaponVars(void* pWeapon)
{
	const uintptr_t key = reinterpret_cast<uintptr_t>(pWeapon);
	return s_weaponScriptVars[key];
}

static void WeaponScriptVars_ClearHighlightMaps();

void WeaponScriptVars_LevelShutdown()
{
	s_weaponScriptVars.clear();
	WeaponScriptVars_ClearHighlightMaps();
}

//-----------------------------------------------------------------------------
// ScriptFloat0
//-----------------------------------------------------------------------------
static SQRESULT Script_SetScriptFloat0(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	SQFloat value;
	sq_getfloat(v, 2, &value);

	GetWeaponVars(pWeapon).scriptFloat0 = static_cast<float>(value);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_GetScriptFloat0(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	sq_pushfloat(v, GetWeaponVars(pWeapon).scriptFloat0);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// ScriptInt1
//-----------------------------------------------------------------------------
static constexpr int SCRIPT_INT_MIN = -65536;
static constexpr int SCRIPT_INT_MAX = 65535;

static SQRESULT Script_SetScriptInt1(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	SQInteger value;
	sq_getinteger(v, 2, &value);

	const int intVal = static_cast<int>(value);

	if (intVal < SCRIPT_INT_MIN || intVal > SCRIPT_INT_MAX)
	{
		v_SQVM_ScriptError("ScriptInt1 value %i out of range (%i to %i)",
			intVal, SCRIPT_INT_MIN, SCRIPT_INT_MAX);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	GetWeaponVars(pWeapon).scriptInt1 = intVal;
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_GetScriptInt1(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	sq_pushinteger(v, GetWeaponVars(pWeapon).scriptInt1);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// ScriptTime1
//-----------------------------------------------------------------------------
static SQRESULT Script_SetScriptTime1(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	SQFloat value;
	sq_getfloat(v, 2, &value);

	GetWeaponVars(pWeapon).scriptTime1 = static_cast<float>(value);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_GetScriptTime1(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

	sq_pushfloat(v, GetWeaponVars(pWeapon).scriptTime1);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// IsWeaponX
// Entity-level: returns false (not a weapon)
// Weapon-level: returns true (is a weapon)
//-----------------------------------------------------------------------------
static SQRESULT Script_IsWeaponX_False(HSQUIRRELVM v)
{
	sq_pushbool(v, false);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_IsWeaponX_True(HSQUIRRELVM v)
{
	sq_pushbool(v, true);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// GetItemFlavorGUID
// Returns the item flavor GUID associated with this weapon (stub: returns 0)
//-----------------------------------------------------------------------------
static SQRESULT Script_GetItemFlavorGUID(HSQUIRRELVM v)
{
	sq_pushinteger(v, 0);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// SetItemFlavorGUID
// Sets the item flavor GUID on this weapon (stub: no-op)
//-----------------------------------------------------------------------------
static SQRESULT Script_SetItemFlavorGUID(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "SetItemFlavorGUID called - stub\n");
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// SetupArtifact
// Sets up artifact visuals on the weapon (stub: no-op)
//-----------------------------------------------------------------------------
static SQRESULT Script_SetupArtifact(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "SetupArtifact called - stub\n");
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// SetWeaponCharmOrArtifactBladeGUID
// Sets the weapon charm or artifact blade GUID (stub: no-op)
//-----------------------------------------------------------------------------
static SQRESULT Script_SetWeaponCharmOrArtifactBladeGUID(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "SetWeaponCharmOrArtifactBladeGUID called - stub\n");
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// StartCustomActivityDetailed
// Starts a custom activity with detailed parameters (stub: no-op)
//-----------------------------------------------------------------------------
static SQRESULT Script_StartCustomActivityDetailed(HSQUIRRELVM v)
{
	DevMsg(eDLL_T::CLIENT, "StartCustomActivityDetailed called - stub\n");
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// IsWeaponOffhandMelee
// Returns true if fireMode == eWeaponFireMode.offhandMelee (4)
//-----------------------------------------------------------------------------
static SQRESULT Script_IsWeaponOffhandMelee(HSQUIRRELVM v)
{
	void* pWeapon = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pWeapon)))
		return SQ_ERROR;

#ifndef CLIENT_DLL
	const bool result = reinterpret_cast<CWeaponX*>(pWeapon)->IsWeaponOffhandMelee();
#else
	const bool result = reinterpret_cast<C_WeaponX*>(pWeapon)->IsWeaponOffhandMelee();
#endif

	sq_pushbool(v, result);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

float WeaponScriptVars_GetScriptFloat0(void* pWeapon)
{
	if (!pWeapon) return 0.0f;

	const uintptr_t key = reinterpret_cast<uintptr_t>(pWeapon);
	auto it = s_weaponScriptVars.find(key);
	if (it == s_weaponScriptVars.end()) return 0.0f;

	return it->second.scriptFloat0;
}

void WeaponScriptVars_SetScriptFloat0(void* pWeapon, float value)
{
	if (!pWeapon) return;
	GetWeaponVars(pWeapon).scriptFloat0 = value;
}

void WeaponScriptVars_RegisterWeaponFuncs(ScriptClassDescriptor_t* weaponStruct)
{
	DevMsg(eDLL_T::CLIENT, "[WeaponScriptVars] Registering weapon script variable functions\n");

	weaponStruct->AddFunction(
		"SetScriptFloat0",
		"Script_SetScriptFloat0",
		"Sets extended script float 0 on this weapon",
		"void",
		"float value",
		false,
		Script_SetScriptFloat0);

	weaponStruct->AddFunction(
		"GetScriptFloat0",
		"Script_GetScriptFloat0",
		"Gets extended script float 0 from this weapon",
		"float",
		"",
		false,
		Script_GetScriptFloat0);

	weaponStruct->AddFunction(
		"SetScriptInt1",
		"Script_SetScriptInt1",
		"Sets extended script int 1 on this weapon (range: -65536 to 65535)",
		"void",
		"int value",
		false,
		Script_SetScriptInt1);

	weaponStruct->AddFunction(
		"GetScriptInt1",
		"Script_GetScriptInt1",
		"Gets extended script int 1 from this weapon",
		"int",
		"",
		false,
		Script_GetScriptInt1);

	weaponStruct->AddFunction(
		"SetScriptTime1",
		"Script_SetScriptTime1",
		"Sets extended script time 1 on this weapon",
		"void",
		"float value",
		false,
		Script_SetScriptTime1);

	weaponStruct->AddFunction(
		"GetScriptTime1",
		"Script_GetScriptTime1",
		"Gets extended script time 1 from this weapon",
		"float",
		"",
		false,
		Script_GetScriptTime1);

	weaponStruct->AddFunction(
		"IsWeaponOffhandMelee",
		"Script_IsWeaponOffhandMelee",
		"Returns true if this weapon's fire mode is offhandMelee",
		"bool",
		"",
		false,
		Script_IsWeaponOffhandMelee);

	weaponStruct->AddFunction(
		"IsWeaponX",
		"Script_IsWeaponX_True",
		"Returns true if this entity is a CWeaponX",
		"bool",
		"",
		false,
		Script_IsWeaponX_True);

	weaponStruct->AddFunction(
		"GetItemFlavorGUID",
		"Script_GetItemFlavorGUID",
		"Returns the item flavor GUID associated with this weapon",
		"int",
		"",
		false,
		Script_GetItemFlavorGUID);

	weaponStruct->AddFunction(
		"SetItemFlavorGUID",
		"Script_SetItemFlavorGUID",
		"Sets the item flavor GUID on this weapon",
		"void",
		"int guid",
		false,
		Script_SetItemFlavorGUID);

	weaponStruct->AddFunction(
		"SetupArtifact",
		"Script_SetupArtifact",
		"Sets up artifact visuals on the weapon",
		"void",
		"int tier, string bladeSkinName, string powerSourceModifier, int componentChanged",
		false,
		Script_SetupArtifact);

	weaponStruct->AddFunction(
		"SetWeaponCharmOrArtifactBladeGUID",
		"Script_SetWeaponCharmOrArtifactBladeGUID",
		"Sets the weapon charm or artifact blade GUID on this weapon",
		"void",
		"int guid",
		false,
		Script_SetWeaponCharmOrArtifactBladeGUID);

	weaponStruct->AddFunction(
		"StartCustomActivityDetailed",
		"Script_StartCustomActivityDetailed",
		"Starts a custom activity with detailed parameters",
		"void",
		"string activity, int flags, float duration, string thirdPersonActivity",
		false,
		Script_StartCustomActivityDetailed);
}

//-----------------------------------------------------------------------------
// HighlightEnableForTeam / HighlightDisableForTeam
// (int contextId, int team) with per-entity team/context slot tracking
//-----------------------------------------------------------------------------
static constexpr int HIGHLIGHT_TEAM_MAX_SLOTS = 8;

struct EntityHighlightTeamData_t
{
	uint8_t teamIndex[HIGHLIGHT_TEAM_MAX_SLOTS];
	uint32_t teamBits[HIGHLIGHT_TEAM_MAX_SLOTS];

	EntityHighlightTeamData_t()
	{
		memset(teamIndex, 0xFF, sizeof(teamIndex));
		memset(teamBits, 0, sizeof(teamBits));
	}
};

static std::unordered_map<uintptr_t, EntityHighlightTeamData_t> s_entityHighlightTeams;

static SQRESULT Script_HighlightEnableForTeam(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

	SQInteger contextId, team;
	sq_getinteger(v, 2, &contextId);
	sq_getinteger(v, 3, &team);

	if (contextId < 0 || contextId > 254 || team < 0 || team > 31)
		return SQ_OK;

	auto& data = s_entityHighlightTeams[reinterpret_cast<uintptr_t>(pEntity)];

	// Find existing slot for this context, or allocate free slot
	int slot = -1;
	for (int i = 0; i < HIGHLIGHT_TEAM_MAX_SLOTS; i++)
	{
		if (data.teamIndex[i] == static_cast<uint8_t>(contextId))
		{
			slot = i;
			break;
		}
	}

	if (slot < 0)
	{
		for (int i = 0; i < HIGHLIGHT_TEAM_MAX_SLOTS; i++)
		{
			if (data.teamIndex[i] == 0xFF)
			{
				slot = i;
				break;
			}
		}
	}

	if (slot < 0)
		return SQ_OK;

	data.teamBits[slot] |= (1u << static_cast<int>(team));
	data.teamIndex[slot] = static_cast<uint8_t>(contextId);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_HighlightDisableForTeam(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

	SQInteger contextId, team;
	sq_getinteger(v, 2, &contextId);
	sq_getinteger(v, 3, &team);

	if (contextId < 0 || contextId > 254 || team < 0 || team > 31)
		return SQ_OK;

	auto it = s_entityHighlightTeams.find(reinterpret_cast<uintptr_t>(pEntity));
	if (it == s_entityHighlightTeams.end())
		return SQ_OK;

	auto& data = it->second;
	for (int i = 0; i < HIGHLIGHT_TEAM_MAX_SLOTS; i++)
	{
		if (data.teamIndex[i] == static_cast<uint8_t>(contextId))
		{
			data.teamBits[i] &= ~(1u << static_cast<int>(team));
			if (data.teamBits[i] == 0)
				data.teamIndex[i] = 0xFF;
			break;
		}
	}

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Highlight_OverrideParam / Highlight_ClearOverrideParam
//-----------------------------------------------------------------------------
static constexpr int MAX_HIGHLIGHT_OVERRIDE_PARAMS = 4;

struct EntityHighlightOverride_t
{
	bool isOverriden[MAX_HIGHLIGHT_OVERRIDE_PARAMS] = {};
	float overrideParams[MAX_HIGHLIGHT_OVERRIDE_PARAMS][3] = {};
};

static std::unordered_map<uintptr_t, EntityHighlightOverride_t> s_entityHighlightOverrides;

static SQRESULT Script_Highlight_OverrideParam(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

	SQInteger paramId;
	sq_getinteger(v, 2, &paramId);

	const SQVector3D* value = nullptr;
	sq_getvector(v, 3, &value);

	if (paramId >= 0 && paramId < MAX_HIGHLIGHT_OVERRIDE_PARAMS && value)
	{
		auto& data = s_entityHighlightOverrides[reinterpret_cast<uintptr_t>(pEntity)];
		data.isOverriden[paramId] = true;
		data.overrideParams[paramId][0] = value->x;
		data.overrideParams[paramId][1] = value->y;
		data.overrideParams[paramId][2] = value->z;
	}

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_Highlight_ClearOverrideParam(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

	SQInteger paramId;
	sq_getinteger(v, 2, &paramId);

	if (paramId >= 0 && paramId < MAX_HIGHLIGHT_OVERRIDE_PARAMS)
	{
		auto it = s_entityHighlightOverrides.find(reinterpret_cast<uintptr_t>(pEntity));
		if (it != s_entityHighlightOverrides.end())
			it->second.isOverriden[paramId] = false;
	}

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Generic highlight context per-entity (4 slots)
//-----------------------------------------------------------------------------
static constexpr int MAX_GENERIC_HIGHLIGHT_TYPES = 4;

struct EntityGenericHighlight_t
{
	uint8_t contexts[MAX_GENERIC_HIGHLIGHT_TYPES] = { 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t focusedBits = 0;
};

static std::unordered_map<uintptr_t, EntityGenericHighlight_t> s_entityGenericHighlights;

static SQRESULT Script_Highlight_SetGenericHighlightContext(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

	SQInteger genericType, contextId;
	SQBool focused;
	sq_getinteger(v, 2, &genericType);
	sq_getinteger(v, 3, &contextId);
	sq_getbool(v, 4, &focused);

	if (genericType < 0 || genericType >= MAX_GENERIC_HIGHLIGHT_TYPES)
		return SQ_OK;

	auto& data = s_entityGenericHighlights[reinterpret_cast<uintptr_t>(pEntity)];

	if (contextId > 0xFF)
		data.contexts[genericType] = 0xFF;
	else
		data.contexts[genericType] = static_cast<uint8_t>(contextId);

	if (focused)
		data.focusedBits |= (1 << genericType);
	else
		data.focusedBits &= ~(1 << genericType);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT Script_Highlight_GetGenericHighlightContext(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

	SQInteger genericType;
	sq_getinteger(v, 2, &genericType);

	int result = 0xFF;
	if (genericType >= 0 && genericType < MAX_GENERIC_HIGHLIGHT_TYPES)
	{
		auto it = s_entityGenericHighlights.find(reinterpret_cast<uintptr_t>(pEntity));
		if (it != s_entityGenericHighlights.end())
			result = it->second.contexts[genericType];
	}

	sq_pushinteger(v, result);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// GetUsableValue - reads usable flags from the entity
// SERVER: entity + 0x624, CLIENT: entity + 0x44 (m_usableType, networked)
//-----------------------------------------------------------------------------
static constexpr int ENTITY_USABLETYPE_CLIENT_OFFSET = 0x44;
static constexpr int ENTITY_USABLETYPE_SERVER_OFFSET = 0x624;

static SQRESULT Script_GetUsableValue(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

#ifndef CLIENT_DLL
	int val = *(int*)((uintptr_t)pEntity + ENTITY_USABLETYPE_SERVER_OFFSET);
#else
	int val = *(int*)((uintptr_t)pEntity + ENTITY_USABLETYPE_CLIENT_OFFSET);
#endif

	sq_pushinteger(v, val);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// SetCylinderRadius - alias for engine's SetRadius
//-----------------------------------------------------------------------------
static SQRESULT Script_SetCylinderRadius(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

	SQFloat radius;
	sq_getfloat(v, 2, &radius);

	if (v_CBaseEntity_SetRadius && pEntity)
		v_CBaseEntity_SetRadius(pEntity, static_cast<float>(radius));

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// EnableTouchAutoUse - stub (auto-pickup system, server-only)
//-----------------------------------------------------------------------------
static SQRESULT Script_EnableTouchAutoUse(HSQUIRRELVM v)
{
	Warning(eDLL_T::SERVER, "EnableTouchAutoUse called - stub\n");
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// GetChildren - returns array of entities parented to this entity
// Walks the m_pMoveChild -> m_pMovePeer linked list
//-----------------------------------------------------------------------------
static constexpr int ENTITY_MOVECHILD_OFFSET = 0x11C;
static constexpr int ENTITY_MOVEPEER_OFFSET  = 0x120;
static constexpr int MAX_CHILDREN_SAFETY     = 512;

static SQRESULT Script_GetChildren(HSQUIRRELVM v)
{
	void* pEntity = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pEntity)))
		return SQ_ERROR;

	sq_newarray(v, 0);

#ifdef CLIENT_DLL
	if (!pEntity || !g_pClientEntityList)
		return 1;

	uintptr_t entBase = reinterpret_cast<uintptr_t>(pEntity);
	uint32_t childHandle = *(uint32_t*)(entBase + ENTITY_MOVECHILD_OFFSET);

	int safety = 0;
	while (childHandle != INVALID_EHANDLE_INDEX && safety < MAX_CHILDREN_SAFETY)
	{
		int entIndex = childHandle & ENT_ENTRY_MASK;
		IClientEntity* pChild = v_ClientEntityList_GetClientEntity(g_pClientEntityList, entIndex);

		if (!pChild)
			break;

		C_BaseEntity* pChildEnt = reinterpret_cast<C_BaseEntity*>(pChild);
		const HSCRIPT scriptHandle = v_C_BaseEntity__GetScriptInstance(pChildEnt);
		if (scriptHandle)
		{
			SQObject entObj;
			entObj._type = OT_ENTITY;
			entObj._pad = 0;
			entObj._unVal.pInstance = reinterpret_cast<SQInstance*>(scriptHandle);
			sq_pushobject(v, entObj);
			sq_arrayappend(v, -2);
		}

		uintptr_t childBase = reinterpret_cast<uintptr_t>(pChild);
		childHandle = *(uint32_t*)(childBase + ENTITY_MOVEPEER_OFFSET);
		safety++;
	}
#endif

	return 1;
}

void WeaponScriptVars_RegisterEntityFuncs(ScriptClassDescriptor_t* entityStruct)
{
	DevMsg(eDLL_T::CLIENT, "[WeaponScriptVars] Registering entity highlight functions\n");

	entityStruct->AddFunction(
		"IsWeaponX",
		"Script_IsWeaponX_False",
		"Returns true if this entity is a CWeaponX",
		"bool",
		"",
		false,
		Script_IsWeaponX_False);

	entityStruct->AddFunction(
		"EnableTouchAutoUse",
		"Script_EnableTouchAutoUse",
		"Enables touch-based auto-use within a distance",
		"void",
		"float distance",
		false,
		Script_EnableTouchAutoUse);

	entityStruct->AddFunction(
		"GetChildren",
		"Script_GetChildren",
		"Returns an array of all child entities parented to this entity",
		"array< entity >",
		"",
		false,
		Script_GetChildren);

	entityStruct->AddFunction(
		"SetCylinderRadius",
		"Script_SetCylinderRadius",
		"Sets the entity's collision cylinder radius",
		"void",
		"float radius",
		false,
		Script_SetCylinderRadius);

	entityStruct->AddFunction(
		"GetUsableValue",
		"Script_GetUsableValue",
		"Returns the usable flags bitmask for this entity",
		"int",
		"",
		false,
		Script_GetUsableValue);

	entityStruct->AddFunction(
		"Highlight_OverrideParam",
		"Script_Highlight_OverrideParam",
		"Overrides a highlight param with a vector value",
		"void",
		"int paramId, vector value",
		false,
		Script_Highlight_OverrideParam);

	entityStruct->AddFunction(
		"Highlight_ClearOverrideParam",
		"Script_Highlight_ClearOverrideParam",
		"Clears an overridden highlight param",
		"void",
		"int paramId",
		false,
		Script_Highlight_ClearOverrideParam);

	entityStruct->AddFunction(
		"HighlightEnableForTeam",
		"Script_HighlightEnableForTeam",
		"Enables highlight for a team by context ID",
		"void",
		"int contextId, int team",
		false,
		Script_HighlightEnableForTeam);

	entityStruct->AddFunction(
		"HighlightDisableForTeam",
		"Script_HighlightDisableForTeam",
		"Disables highlight for a team by context ID",
		"void",
		"int contextId, int team",
		false,
		Script_HighlightDisableForTeam);

	entityStruct->AddFunction(
		"Highlight_SetGenericHighlightContext",
		"Script_Highlight_SetGenericHighlightContext",
		"Sets a generic highlight context by type",
		"void",
		"int genericType, int contextId, bool focused",
		false,
		Script_Highlight_SetGenericHighlightContext);

	entityStruct->AddFunction(
		"Highlight_GetGenericHighlightContext",
		"Script_Highlight_GetGenericHighlightContext",
		"Gets a generic highlight context by type",
		"int",
		"int genericType",
		false,
		Script_Highlight_GetGenericHighlightContext);
}

static void WeaponScriptVars_ClearHighlightMaps()
{
	s_entityHighlightTeams.clear();
	s_entityHighlightOverrides.clear();
	s_entityGenericHighlights.clear();
}
