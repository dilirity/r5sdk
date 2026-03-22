//=============================================================================//
//
// Purpose: Highlight context system - SDK-side name-to-ID registry
//
// Stores highlight contexts in a hash map by name with integer IDs.
// HighlightContext_GetId(name) returns the ID, and HighlightContext_Set*/Get*
// functions modify context properties by ID.
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "highlight_context.h"

#include <unordered_map>
#include <string>
#include <cstring>

//-----------------------------------------------------------------------------
// Highlight context data
//-----------------------------------------------------------------------------
static constexpr int MAX_HIGHLIGHT_CONTEXTS = 256;
static constexpr int MAX_HIGHLIGHT_PARAMS = 4;
static constexpr int INVALID_HIGHLIGHT_ID = 255;

struct HighlightContextData_t
{
	char name[64] = {};
	int insideFunction = 0;
	int outsideFunction = 0;
	float outlineRadius = 0.0f;
	int params[MAX_HIGHLIGHT_PARAMS] = {};
	int drawFunc = 0;
	int flags = 0;
	float nearFadeDistance = 0.0f;
	float farFadeDistance = 0.0f;
	float focusedColor[3] = {};
	bool isAfterPostProcess = false;
	bool isEntityVisible = true;
	bool registered = false;
};

static HighlightContextData_t s_contexts[MAX_HIGHLIGHT_CONTEXTS];
static std::unordered_map<std::string, int> s_nameToId;
static int s_nextId = 0;

//-----------------------------------------------------------------------------
// Internal helpers
//-----------------------------------------------------------------------------
static int FindOrCreateContext(const char* name)
{
	if (!name || !*name)
		return INVALID_HIGHLIGHT_ID;

	auto it = s_nameToId.find(name);
	if (it != s_nameToId.end())
		return it->second;

	if (s_nextId >= MAX_HIGHLIGHT_CONTEXTS)
		return INVALID_HIGHLIGHT_ID;

	int id = s_nextId++;
	strncpy(s_contexts[id].name, name, sizeof(s_contexts[id].name) - 1);
	s_contexts[id].registered = true;
	s_nameToId[name] = id;
	return id;
}

static bool IsValidContext(int id)
{
	return id >= 0 && id < MAX_HIGHLIGHT_CONTEXTS && s_contexts[id].registered;
}

//-----------------------------------------------------------------------------
// HighlightContext_GetId(string name) -> int
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_GetId(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return SQ_ERROR;

	int id = FindOrCreateContext(name);
	sq_pushinteger(v, id);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// HighlightContext_SetParam(int contextId, int paramIndex, int value)
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_SetParam(HSQUIRRELVM v)
{
	SQInteger contextId, paramIdx, value;
	sq_getinteger(v, 2, &contextId);
	sq_getinteger(v, 3, &paramIdx);
	sq_getinteger(v, 4, &value);

	if (IsValidContext(static_cast<int>(contextId)) && paramIdx >= 0 && paramIdx < MAX_HIGHLIGHT_PARAMS)
		s_contexts[contextId].params[paramIdx] = static_cast<int>(value);

	return SQ_OK;
}

SQRESULT Script_HighlightContext_GetParam(HSQUIRRELVM v)
{
	SQInteger contextId, paramIdx;
	sq_getinteger(v, 2, &contextId);
	sq_getinteger(v, 3, &paramIdx);

	int result = 0;
	if (IsValidContext(static_cast<int>(contextId)) && paramIdx >= 0 && paramIdx < MAX_HIGHLIGHT_PARAMS)
		result = s_contexts[contextId].params[paramIdx];

	sq_pushinteger(v, result);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// HighlightContext_SetDrawFunc / GetDrawFunc
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_SetDrawFunc(HSQUIRRELVM v)
{
	SQInteger contextId, drawFunc;
	sq_getinteger(v, 2, &contextId);
	sq_getinteger(v, 3, &drawFunc);

	if (IsValidContext(static_cast<int>(contextId)))
		s_contexts[contextId].drawFunc = static_cast<int>(drawFunc);

	return SQ_OK;
}

SQRESULT Script_HighlightContext_GetDrawFunc(HSQUIRRELVM v)
{
	SQInteger contextId;
	sq_getinteger(v, 2, &contextId);

	int result = 0;
	if (IsValidContext(static_cast<int>(contextId)))
		result = s_contexts[contextId].drawFunc;

	sq_pushinteger(v, result);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// HighlightContext_SetRadius / GetOutlineRadius
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_SetRadius(HSQUIRRELVM v)
{
	SQInteger contextId;
	SQFloat radius;
	sq_getinteger(v, 2, &contextId);
	sq_getfloat(v, 3, &radius);

	if (IsValidContext(static_cast<int>(contextId)))
		s_contexts[contextId].outlineRadius = static_cast<float>(radius);

	return SQ_OK;
}

SQRESULT Script_HighlightContext_GetOutlineRadius(HSQUIRRELVM v)
{
	SQInteger contextId;
	sq_getinteger(v, 2, &contextId);

	float result = 0.0f;
	if (IsValidContext(static_cast<int>(contextId)))
		result = s_contexts[contextId].outlineRadius;

	sq_pushfloat(v, result);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// HighlightContext_GetInsideFunction / GetOutlineFunction
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_GetInsideFunction(HSQUIRRELVM v)
{
	SQInteger contextId;
	sq_getinteger(v, 2, &contextId);

	int result = 0;
	if (IsValidContext(static_cast<int>(contextId)))
		result = s_contexts[contextId].insideFunction;

	sq_pushinteger(v, result);
	return SQ_OK;
}

SQRESULT Script_HighlightContext_GetOutlineFunction(HSQUIRRELVM v)
{
	SQInteger contextId;
	sq_getinteger(v, 2, &contextId);

	int result = 0;
	if (IsValidContext(static_cast<int>(contextId)))
		result = s_contexts[contextId].outsideFunction;

	sq_pushinteger(v, result);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// HighlightContext_SetFlags
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_SetFlags(HSQUIRRELVM v)
{
	SQInteger contextId, flags;
	sq_getinteger(v, 2, &contextId);
	sq_getinteger(v, 3, &flags);

	if (IsValidContext(static_cast<int>(contextId)))
		s_contexts[contextId].flags = static_cast<int>(flags);

	return SQ_OK;
}

//-----------------------------------------------------------------------------
// HighlightContext_SetNearFadeDistance / SetFarFadeDistance
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_SetNearFadeDistance(HSQUIRRELVM v)
{
	SQInteger contextId;
	SQFloat dist;
	sq_getinteger(v, 2, &contextId);
	sq_getfloat(v, 3, &dist);

	if (IsValidContext(static_cast<int>(contextId)))
		s_contexts[contextId].nearFadeDistance = static_cast<float>(dist);

	return SQ_OK;
}

SQRESULT Script_HighlightContext_SetFarFadeDistance(HSQUIRRELVM v)
{
	SQInteger contextId;
	SQFloat dist;
	sq_getinteger(v, 2, &contextId);
	sq_getfloat(v, 3, &dist);

	if (IsValidContext(static_cast<int>(contextId)))
		s_contexts[contextId].farFadeDistance = static_cast<float>(dist);

	return SQ_OK;
}

//-----------------------------------------------------------------------------
// HighlightContext_SetFocusedColor(int contextId, vector color)
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_SetFocusedColor(HSQUIRRELVM v)
{
	SQInteger contextId;
	sq_getinteger(v, 2, &contextId);

	const SQVector3D* color = nullptr;
	sq_getvector(v, 3, &color);

	if (IsValidContext(static_cast<int>(contextId)) && color)
	{
		s_contexts[contextId].focusedColor[0] = color->x;
		s_contexts[contextId].focusedColor[1] = color->y;
		s_contexts[contextId].focusedColor[2] = color->z;
	}

	return SQ_OK;
}

//-----------------------------------------------------------------------------
// HighlightContext_IsEntityVisible / IsAfterPostProcess
//-----------------------------------------------------------------------------
SQRESULT Script_HighlightContext_IsEntityVisible(HSQUIRRELVM v)
{
	SQInteger contextId;
	sq_getinteger(v, 2, &contextId);

	bool result = true;
	if (IsValidContext(static_cast<int>(contextId)))
		result = s_contexts[contextId].isEntityVisible;

	sq_pushbool(v, result);
	return SQ_OK;
}

SQRESULT Script_HighlightContext_IsAfterPostProcess(HSQUIRRELVM v)
{
	SQInteger contextId;
	sq_getinteger(v, 2, &contextId);

	bool result = false;
	if (IsValidContext(static_cast<int>(contextId)))
		result = s_contexts[contextId].isAfterPostProcess;

	sq_pushbool(v, result);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// eHighlightDrawFunc enum registration
//-----------------------------------------------------------------------------
void HighlightContext_RegisterDrawFuncEnum(HSQUIRRELVM v)
{
	static const char* s_drawFuncNames[] = {
		"ALWAYS",
		"OCCLUDED",
		"LOS",
		"LOS_LINE",
		"LOS_LINE_FADE",
		"LOS_LINE_LONGFADE",
		"REVENANT_ASSASSINS_INSTINCT",
		"LOS_LINE_ENTSONLY_BLOCKSCAN",
		"ENT_APPEAR_EFFECT",
		"LOS_LINE_ENTSONLYCONTENTSBLOCK",
		"CAUSTIC_THREAT",
		"SONAR_DETECTED",
		"SPOT",
		"ALWAYS_LONG_FADE",
		"PICKUP",
		"FORCE_ON",
		"ABILITY_REVEAL",
		"ALLIANCE_PROXIMITY",
	};
	static constexpr int NUM_DRAW_FUNCS = sizeof(s_drawFuncNames) / sizeof(s_drawFuncNames[0]);

	sq_startconsttable(v);
	sq_pushstring(v, "eHighlightDrawFunc", -1);
	sq_newtable(v);

	for (int i = 0; i < NUM_DRAW_FUNCS; i++)
	{
		sq_pushstring(v, s_drawFuncNames[i], -1);
		sq_pushinteger(v, i);
		sq_newslot(v, -3);
	}

	sq_pushstring(v, "LAST_DRAW_FUNC", -1);
	sq_pushinteger(v, NUM_DRAW_FUNCS);
	sq_newslot(v, -3);

	sq_newslot(v, -3);
	sq_endconsttable(v);
}

//-----------------------------------------------------------------------------
// Level shutdown
//-----------------------------------------------------------------------------
void HighlightContext_LevelShutdown()
{
	for (int i = 0; i < MAX_HIGHLIGHT_CONTEXTS; i++)
		s_contexts[i] = HighlightContextData_t();

	s_nameToId.clear();
	s_nextId = 0;
}
