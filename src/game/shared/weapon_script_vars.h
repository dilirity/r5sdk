#ifndef WEAPON_SCRIPT_VARS_H
#define WEAPON_SCRIPT_VARS_H

struct ScriptClassDescriptor_t;

void WeaponScriptVars_RegisterWeaponFuncs(ScriptClassDescriptor_t* weaponStruct);
float WeaponScriptVars_GetScriptFloat0(void* pWeapon);

#endif // WEAPON_SCRIPT_VARS_H
