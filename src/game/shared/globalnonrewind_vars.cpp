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
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "globalnonrewind_vars.h"

#include <unordered_map>
#include <string>

//-----------------------------------------------------------------------------
// Storage
//-----------------------------------------------------------------------------
struct NonRewindVar_t
{
	enum Type { BOOL, INT, FLOAT, TIME };

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
static bool EnsureVarSlot(const char* name)
{
	if (s_nonRewindVars.count(name))
		return true;
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

	if (!EnsureVarSlot(name))
		return SQ_OK;

	SQBool value;
	sq_getbool(v, 3, &value);

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::BOOL;
	var.bVal = (value != 0);
	return SQ_OK;
}

SQRESULT Script_SetGlobalNonRewindNetInt(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	if (!EnsureVarSlot(name))
		return SQ_OK;

	SQInteger value;
	sq_getinteger(v, 3, &value);

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::INT;
	var.iVal = static_cast<int>(value);
	return SQ_OK;
}

SQRESULT Script_SetGlobalNonRewindNetFloat(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	if (!EnsureVarSlot(name))
		return SQ_OK;

	SQFloat value;
	sq_getfloat(v, 3, &value);

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::FLOAT;
	var.fVal = static_cast<float>(value);
	return SQ_OK;
}

SQRESULT Script_SetGlobalNonRewindNetTime(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	if (!EnsureVarSlot(name))
		return SQ_OK;

	SQFloat value;
	sq_getfloat(v, 3, &value);

	NonRewindVar_t& var = s_nonRewindVars[name];
	var.type = NonRewindVar_t::TIME;
	var.fVal = static_cast<float>(value);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// Getters
//-----------------------------------------------------------------------------
SQRESULT Script_GetGlobalNonRewindNetBool(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	auto it = s_nonRewindVars.find(name);
	if (it != s_nonRewindVars.end() && it->second.type == NonRewindVar_t::BOOL)
		sq_pushbool(v, it->second.bVal);
	else
		sq_pushbool(v, false);

	return SQ_OK;
}

SQRESULT Script_GetGlobalNonRewindNetInt(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	auto it = s_nonRewindVars.find(name);
	if (it != s_nonRewindVars.end() && it->second.type == NonRewindVar_t::INT)
		sq_pushinteger(v, it->second.iVal);
	else
		sq_pushinteger(v, 0);

	return SQ_OK;
}

SQRESULT Script_GetGlobalNonRewindNetFloat(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	auto it = s_nonRewindVars.find(name);
	if (it != s_nonRewindVars.end() && it->second.type == NonRewindVar_t::FLOAT)
		sq_pushfloat(v, it->second.fVal);
	else
		sq_pushfloat(v, 0.0f);

	return SQ_OK;
}

SQRESULT Script_GetGlobalNonRewindNetTime(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	auto it = s_nonRewindVars.find(name);
	if (it != s_nonRewindVars.end() && it->second.type == NonRewindVar_t::TIME)
		sq_pushfloat(v, it->second.fVal);
	else
		sq_pushfloat(v, 0.0f);

	return SQ_OK;
}

//-----------------------------------------------------------------------------
// Level shutdown
//-----------------------------------------------------------------------------
void GlobalNonRewind_LevelShutdown()
{
	s_nonRewindVars.clear();
}
