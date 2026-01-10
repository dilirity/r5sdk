//===========================================================================//
//
// Purpose: EFCT callback type bypass patch
//
// Fixes particle effects with callback type != 16 not being registered.
//
// The game's EFCT_PostLoadCallback checks: if (assetMagic == 0x65666374 && callbackType == 16)
// When a GUID already exists, callback type is 4 and effects are skipped.
// This leaves particle buffers as NULL, causing crashes.
//
// This hook forces callback type to 16 for all EFCT assets, ensuring new
// elements inside particles get properly registered.
//
//===========================================================================//
#include "particle_effect_callback.h"

//-----------------------------------------------------------------------------
// Hook: Forces callback type to 16 for EFCT registration
//-----------------------------------------------------------------------------
static void EFCT_PostLoadCallback_Hook(__int64 a1, int callbackType, int assetMagic, uint64_t* a4, int a5, int a6, __int64 a7)
{
	// Check if this is an EFCT asset (assetMagic == 0x65666374 'efct')
	if (assetMagic == EFCT_ASSET_MAGIC)
	{
		// Force callback type to 16 to trigger EFCT registration
		// When GUID already exists, type is 4 and new elements don't get registered,
		// leaving particle buffers uninitialized and causing NULL pointer crashes.
		if (callbackType != EFCT_CALLBACK_TYPE_REGISTER)
		{
			DevMsg(eDLL_T::CLIENT, "[EFCT] Patching callback type %d -> %d for EFCT registration\n",
				callbackType, EFCT_CALLBACK_TYPE_REGISTER);
			callbackType = EFCT_CALLBACK_TYPE_REGISTER;
		}
	}

	// Call original function with (potentially patched) callback type
	v_EFCT_PostLoadCallback(a1, callbackType, assetMagic, a4, a5, a6, a7);
}

//-----------------------------------------------------------------------------
// Attach/Detach the hook
//-----------------------------------------------------------------------------
void VParticleEffectCallback::Detour(const bool bAttach) const
{
	DetourSetup(&v_EFCT_PostLoadCallback, &EFCT_PostLoadCallback_Hook, bAttach);
}
