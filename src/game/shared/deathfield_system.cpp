//=============================================================================//
//
// Purpose: Multi-index deathfield system for realm-based gameplay
//
// Index 0 reads live data from the engine world entity.
// Indexes 1+ are SDK-managed for multi-ring support.
//
//=============================================================================//

#include "core/stdafx.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "deathfield_system.h"
#include "public/globalvars_base.h"

#ifndef CLIENT_DLL
#include "public/edict.h"
extern CGlobalVars* gpGlobals;
#else
extern CGlobalVarsBase* gpGlobals;
#endif

#include <cmath>
#include <unordered_map>

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------
static constexpr int MAX_DEATHFIELDS = 16;

// World entity deathfield offsets
static constexpr int WORLD_DF_ISACTIVE     = 0xA40;
static constexpr int WORLD_DF_ORIGIN       = 0xA44;
static constexpr int WORLD_DF_RADIUS_START = 0xA50;
static constexpr int WORLD_DF_RADIUS_END   = 0xA54;
static constexpr int WORLD_DF_TIME_START   = 0xA58;
static constexpr int WORLD_DF_TIME_END     = 0xA5C;

//-----------------------------------------------------------------------------
// Per-deathfield data
//-----------------------------------------------------------------------------
struct DeathFieldData_t
{
	bool isActive = false;
	float originX = 0.0f;
	float originY = 0.0f;
	float originZ = 0.0f;
	float radiusStart = 0.0f;
	float radiusEnd = 0.0f;
	float timeStart = 0.0f;
	float timeEnd = 0.0f;
};

static DeathFieldData_t s_deathFields[MAX_DEATHFIELDS];
static std::unordered_map<uintptr_t, int> s_playerDeathFieldIndex;

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------
static inline float GraphCapped(float val, float inMin, float inMax, float outMin, float outMax)
{
	if (inMax <= inMin)
		return outMax;
	float t = (val - inMin) / (inMax - inMin);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return outMin + (outMax - outMin) * t;
}

static inline void* GetWorldEntity()
{
	if (!g_ppWorldEntity || !*g_ppWorldEntity)
		return nullptr;
	return *g_ppWorldEntity;
}

static bool ReadEngineDeathField(DeathFieldData_t& out)
{
	void* worldEnt = GetWorldEntity();
	if (!worldEnt)
		return false;

	const uintptr_t base = reinterpret_cast<uintptr_t>(worldEnt);
	out.isActive    = *(bool*)(base + WORLD_DF_ISACTIVE);
	out.originX     = *(float*)(base + WORLD_DF_ORIGIN);
	out.originY     = *(float*)(base + WORLD_DF_ORIGIN + 4);
	out.originZ     = *(float*)(base + WORLD_DF_ORIGIN + 8);
	out.radiusStart = *(float*)(base + WORLD_DF_RADIUS_START);
	out.radiusEnd   = *(float*)(base + WORLD_DF_RADIUS_END);
	out.timeStart   = *(float*)(base + WORLD_DF_TIME_START);
	out.timeEnd     = *(float*)(base + WORLD_DF_TIME_END);
	return true;
}

static const DeathFieldData_t& GetDeathField(int index)
{
	static DeathFieldData_t s_engineField;

	if (index == 0)
	{
		ReadEngineDeathField(s_engineField);
		return s_engineField;
	}

	if (index < 0 || index >= MAX_DEATHFIELDS)
	{
		static DeathFieldData_t s_empty;
		return s_empty;
	}

	return s_deathFields[index];
}

static float GetCurrentRadius(const DeathFieldData_t& df, float time)
{
	if (!df.isActive)
		return 3.4028235e38f;

	return GraphCapped(time, df.timeStart, df.timeEnd, df.radiusStart, df.radiusEnd);
}

//-----------------------------------------------------------------------------
// Player DeathFieldIndex
//-----------------------------------------------------------------------------
SQRESULT Script_DeathFieldIndex(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	auto it = s_playerDeathFieldIndex.find(reinterpret_cast<uintptr_t>(pPlayer));
	sq_pushinteger(v, (it != s_playerDeathFieldIndex.end()) ? it->second : 0);
	return SQ_OK;
}

SQRESULT Script_SetDeathFieldIndex(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;
	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	SQInteger index;
	sq_getinteger(v, 2, &index);

	if (index < 0 || index >= MAX_DEATHFIELDS)
	{
		Warning(eDLL_T::SERVER, "SetDeathFieldIndex: index %d out of range [0, %d)\n",
			static_cast<int>(index), MAX_DEATHFIELDS);
		return SQ_OK;
	}

	s_playerDeathFieldIndex[reinterpret_cast<uintptr_t>(pPlayer)] = static_cast<int>(index);
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// Deathfield query functions
//-----------------------------------------------------------------------------
SQRESULT Script_DeathField_IsActive(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	const DeathFieldData_t& df = GetDeathField(static_cast<int>(index));
	sq_pushbool(v, df.isActive);
	return SQ_OK;
}

SQRESULT Script_DeathField_PointDistanceFromFrontier(HSQUIRRELVM v)
{
	const SQVector3D* point = nullptr;
	sq_getvector(v, 2, &point);
	if (!point) return SQ_ERROR;

	SQInteger index;
	sq_getinteger(v, 3, &index);

	const DeathFieldData_t& df = GetDeathField(static_cast<int>(index));

	if (!gpGlobals)
	{
		sq_pushfloat(v, 0.0f);
		return SQ_OK;
	}

	float radius = GetCurrentRadius(df, gpGlobals->curTime);
	float dx = point->x - df.originX;
	float dy = point->y - df.originY;
	float dist2D = sqrtf(dx * dx + dy * dy);

	sq_pushfloat(v, radius - dist2D);
	return SQ_OK;
}

SQRESULT Script_DeathField_GetRadiusForNow(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	if (!gpGlobals)
	{
		sq_pushfloat(v, 3.4028235e38f);
		return SQ_OK;
	}

	const DeathFieldData_t& df = GetDeathField(static_cast<int>(index));
	sq_pushfloat(v, GetCurrentRadius(df, gpGlobals->curTime));
	return SQ_OK;
}

SQRESULT Script_DeathField_GetRadiusForTime(HSQUIRRELVM v)
{
	SQFloat time;
	sq_getfloat(v, 2, &time);

	SQInteger index;
	sq_getinteger(v, 3, &index);

	const DeathFieldData_t& df = GetDeathField(static_cast<int>(index));
	sq_pushfloat(v, GetCurrentRadius(df, static_cast<float>(time)));
	return SQ_OK;
}

//-----------------------------------------------------------------------------
// Deathfield configuration (server-only)
//-----------------------------------------------------------------------------
SQRESULT Script_DeathField_SetActive(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	SQBool active;
	sq_getbool(v, 3, &active);

	if (index >= 0 && index < MAX_DEATHFIELDS)
		s_deathFields[index].isActive = (active != 0);

	return SQ_OK;
}

SQRESULT Script_DeathField_SetOrigin(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	const SQVector3D* origin = nullptr;
	sq_getvector(v, 3, &origin);
	if (!origin) return SQ_ERROR;

	if (index >= 0 && index < MAX_DEATHFIELDS)
	{
		s_deathFields[index].originX = origin->x;
		s_deathFields[index].originY = origin->y;
		s_deathFields[index].originZ = origin->z;
	}

	return SQ_OK;
}

SQRESULT Script_DeathField_SetRadiusStartEnd(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	SQFloat start, end;
	sq_getfloat(v, 3, &start);
	sq_getfloat(v, 4, &end);

	if (index >= 0 && index < MAX_DEATHFIELDS)
	{
		s_deathFields[index].radiusStart = static_cast<float>(start);
		s_deathFields[index].radiusEnd = static_cast<float>(end);
	}

	return SQ_OK;
}

SQRESULT Script_DeathField_SetTimeStartEnd(HSQUIRRELVM v)
{
	SQInteger index;
	sq_getinteger(v, 2, &index);

	SQFloat start, end;
	sq_getfloat(v, 3, &start);
	sq_getfloat(v, 4, &end);

	if (index >= 0 && index < MAX_DEATHFIELDS)
	{
		s_deathFields[index].timeStart = static_cast<float>(start);
		s_deathFields[index].timeEnd = static_cast<float>(end);
	}

	return SQ_OK;
}

//-----------------------------------------------------------------------------
// Level shutdown
//-----------------------------------------------------------------------------
void DeathField_LevelShutdown()
{
	for (int i = 0; i < MAX_DEATHFIELDS; i++)
		s_deathFields[i] = DeathFieldData_t();

	s_playerDeathFieldIndex.clear();
}
