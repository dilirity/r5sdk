//===========================================================================//
//
// Purpose: particle system debug overlay
//
//===========================================================================//
#ifndef PARTICLE_OVERLAY_H
#define PARTICLE_OVERLAY_H
#include "mathlib/vector2d.h"

class CParticleMgr;

struct ParticleOverlayScreenText_s
{
	Vector2D position;
	float duration;
	int lineOffset;
};

inline void (*v_ParticleOverlay_Run)(CParticleMgr* const particleMgr);
inline void (*v_ParticleOverlay_AddScreenText)(ParticleOverlayScreenText_s* const overlay, const char* const format, ...);

///////////////////////////////////////////////////////////////////////////////
class VParticleOverlay : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ParticleOverlay_Run", v_ParticleOverlay_Run);
		LogFunAdr("ParticleOverlay_AddScreenText", v_ParticleOverlay_AddScreenText);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 4C 24 ? 55 41 56 48 8D AC 24").GetPtr(v_ParticleOverlay_Run);
		Module_FindPattern(g_GameDll, "48 8B C4 48 89 50 ? 4C 89 40 ? 4C 89 48 ? 53 56 57 48 81 EC ? ? ? ? 48 8B DA 48 8D 70 ? 48 8B F9 E8 ? ? ? ? 48 89 74 24 ? 48 8D 54 24 ? 33 F6 4C 8B CB 41 B8 ? ? ? ? 48 89 74 24 ? 48 8B 08 48 83 C9 ? E8 ? ? ? ? 85 C0 B9 ? ? ? ? 0F 48 C1 0F B6 8C 24 ? ? ? ? 3D ? ? ? ? 0F 47 CE").GetPtr(v_ParticleOverlay_AddScreenText);
	}
	virtual void GetVar(void) const {}
	virtual void GetCon(void) const {}
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // PARTICLE_OVERLAY_H
