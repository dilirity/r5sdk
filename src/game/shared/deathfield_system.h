#ifndef DEATHFIELD_SYSTEM_H
#define DEATHFIELD_SYSTEM_H

#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "thirdparty/detours/include/idetour.h"

SQRESULT Script_DeathFieldIndex(HSQUIRRELVM v);
SQRESULT Script_SetDeathFieldIndex(HSQUIRRELVM v);
SQRESULT Script_DeathField_IsActive(HSQUIRRELVM v);
SQRESULT Script_DeathField_PointDistanceFromFrontier(HSQUIRRELVM v);
SQRESULT Script_DeathField_GetRadiusForNow(HSQUIRRELVM v);
SQRESULT Script_DeathField_GetRadiusForTime(HSQUIRRELVM v);
SQRESULT Script_DeathField_SetActive(HSQUIRRELVM v);
SQRESULT Script_DeathField_SetOrigin(HSQUIRRELVM v);
SQRESULT Script_DeathField_SetRadiusStartEnd(HSQUIRRELVM v);
SQRESULT Script_DeathField_SetTimeStartEnd(HSQUIRRELVM v);

void DeathField_LevelShutdown();

inline void** g_ppWorldEntity = nullptr;

///////////////////////////////////////////////////////////////////////////////
class VDeathFieldSystem : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogVarAdr("g_pWorldEntity", g_ppWorldEntity);
	}
	virtual void GetFun(void) const { }
	virtual void GetVar(void) const
	{
		g_ppWorldEntity = Module_FindPattern(g_GameDll,
			"40 53 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 33 D2 48 8B D9 38 90 40 0A 00 00")
			.ResolveRelativeAddressSelf(0x9, 0xD).RCast<void**>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // DEATHFIELD_SYSTEM_H
