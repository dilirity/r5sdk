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
}
