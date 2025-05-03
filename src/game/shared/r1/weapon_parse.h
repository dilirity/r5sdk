#ifndef GAME_WEAPON_PARSE_H
#define GAME_WEAPON_PARSE_H

inline void (*WeaponParse_LoadServerData)(const bool parseScripts);
inline void (*WeaponParse_LoadClientData)(const bool parseScripts, const bool setupRumble);

///////////////////////////////////////////////////////////////////////////////
class V_Weapon_Parse : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("WeaponParse_LoadServerData", WeaponParse_LoadServerData);
		LogFunAdr("WeaponParse_LoadClientData", WeaponParse_LoadClientData);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 57 41 54 48 81 EC").GetPtr(WeaponParse_LoadServerData);
		Module_FindPattern(g_GameDll, "48 8B C4 88 50 ?? 48 81 EC").GetPtr(WeaponParse_LoadClientData);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { };
};
///////////////////////////////////////////////////////////////////////////////

#endif // GAME_WEAPON_PARSE_H
