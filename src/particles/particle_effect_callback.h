//===========================================================================//
//
// Purpose: EFCT callback type bypass patch
//
// Fixes particle effects with callback type != 16 not being registered.
// When a GUID already exists, the callback type is set to 4, causing new
// elements inside particles to not be registered and leaving particle buffers
// uninitialized (NULL pointer crashes in particle trail rendering).
//
// Root cause: EFCT_PostLoadCallback has `if (callbackType == 16)` check that
// gates effect registration. Effects with type 4 (existing GUID) get skipped.
//
//===========================================================================//
#ifndef PARTICLE_EFFECT_CALLBACK_H
#define PARTICLE_EFFECT_CALLBACK_H

//-----------------------------------------------------------------------------
// Function pointer for original EFCT_PostLoadCallback
//-----------------------------------------------------------------------------
inline void (*v_EFCT_PostLoadCallback)(__int64 a1, int callbackType, int assetMagic, uint64_t* a4, int a5, int a6, __int64 a7);

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------
constexpr int EFCT_ASSET_MAGIC = 0x65666374;          // 'efct' in little-endian
constexpr int EFCT_CALLBACK_TYPE_REGISTER = 16;       // Type that triggers registration
constexpr int EFCT_CALLBACK_TYPE_GUID_EXISTS = 4;     // Type when GUID already exists (skipped)

///////////////////////////////////////////////////////////////////////////////
class VParticleEffectCallback : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("EFCT_PostLoadCallback", v_EFCT_PostLoadCallback);
	}
	virtual void GetFun(void) const
	{
		// Pattern: "cmp r8d, 65666374h" - the EFCT magic check
		// Found at offset 0x18 into EFCT_PostLoadCallback
		CMemory cmpInstr = Module_FindPattern(g_GameDll, "41 81 F8 74 63 66 65");
		if (cmpInstr)
		{
			cmpInstr.Offset(-0x18).GetPtr(v_EFCT_PostLoadCallback);
		}
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // PARTICLE_EFFECT_CALLBACK_H
