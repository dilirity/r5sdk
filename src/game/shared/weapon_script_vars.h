#ifndef WEAPON_SCRIPT_VARS_H
#define WEAPON_SCRIPT_VARS_H

#include "thirdparty/detours/include/idetour.h"

struct ScriptClassDescriptor_t;

void WeaponScriptVars_RegisterWeaponFuncs(ScriptClassDescriptor_t* weaponStruct);
void WeaponScriptVars_RegisterEntityFuncs(ScriptClassDescriptor_t* entityStruct);
float WeaponScriptVars_GetScriptFloat0(void* pWeapon);
void WeaponScriptVars_SetScriptFloat0(void* pWeapon, float value);
void WeaponScriptVars_LevelShutdown();

// Engine: CBaseEntity::SetRadius(float) - sets collision cylinder radius
inline void(*v_CBaseEntity_SetRadius)(void* entity, float radius) = nullptr;

///////////////////////////////////////////////////////////////////////////////
class VWeaponScriptVars : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CBaseEntity::SetRadius", v_CBaseEntity_SetRadius);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll,
			"48 89 5C 24 ?? 57 48 83 EC 30 F3 0F 10 81 10 0D 00 00")
			.GetPtr(v_CBaseEntity_SetRadius);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // WEAPON_SCRIPT_VARS_H
