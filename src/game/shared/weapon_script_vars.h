#ifndef WEAPON_SCRIPT_VARS_H
#define WEAPON_SCRIPT_VARS_H

struct ScriptClassDescriptor_t;

void WeaponScriptVars_RegisterWeaponFuncs(ScriptClassDescriptor_t* weaponStruct);
void WeaponScriptVars_RegisterEntityFuncs(ScriptClassDescriptor_t* entityStruct);
float WeaponScriptVars_GetScriptFloat0(void* pWeapon);
void WeaponScriptVars_LevelShutdown();

#endif // WEAPON_SCRIPT_VARS_H
