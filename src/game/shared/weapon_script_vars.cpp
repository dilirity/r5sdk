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

void WeaponScriptVars_LevelShutdown()
{
	s_weaponScriptVars.clear();
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
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// SetupArtifact
// Sets up artifact visuals on the weapon (stub: no-op)
//-----------------------------------------------------------------------------
static SQRESULT Script_SetupArtifact(HSQUIRRELVM v)
{
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// SetWeaponCharmOrArtifactBladeGUID
// Sets the weapon charm or artifact blade GUID (stub: no-op)
//-----------------------------------------------------------------------------
static SQRESULT Script_SetWeaponCharmOrArtifactBladeGUID(HSQUIRRELVM v)
{
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// StartCustomActivityDetailed
// Starts a custom activity with detailed parameters (stub: no-op)
//-----------------------------------------------------------------------------
static SQRESULT Script_StartCustomActivityDetailed(HSQUIRRELVM v)
{
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

void WeaponScriptVars_RegisterWeaponFuncs(ScriptClassDescriptor_t* weaponStruct)
{
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

void WeaponScriptVars_RegisterEntityFuncs(ScriptClassDescriptor_t* entityStruct)
{
	entityStruct->AddFunction(
		"IsWeaponX",
		"Script_IsWeaponX_False",
		"Returns true if this entity is a CWeaponX",
		"bool",
		"",
		false,
		Script_IsWeaponX_False);
}
