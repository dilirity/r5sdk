//=============================================================================//
// 
// Purpose: Miles Sound System interface shim
// 
//-----------------------------------------------------------------------------
// The engine is compiled with version 10.0.42, this shim layer fixes any
// incompatibilities between upgrades. On more recent versions of the Miles
// Sound System, some exports have been renamed and/or thoroughly changed.
// If we upgrade to these versions, we need to convert this into an actual
// DLL shim layer instead of linking it statically with the SDK module.
//=============================================================================//
#include "miles_impl.h"
#include "miles_shim.h"
#include "tier1/cvar.h"

// NOTE: when prototypes of API's change and one or more parameters reside on
// the stack, optimization must be turned OFF as the compiler will otherwise
// optimize the stack setup of the new prototype away, causing UB. This does
// not incur a performance penalty as this is just a proxy, the actual logic
// within the Miles Sound System library remains unmodified.

#pragma region ConVars
static ConVar miles_pcm_log("miles_pcm_log", "0", FCVAR_RELEASE, "Log MilesSampleSetSourceRaw args and injection");
#pragma endregion

#pragma optimize( "", off )
static unsigned int MilesSampleSetSourceRaw(__int64 a1, __int64 a2, unsigned int a3, int a4, unsigned __int16 a5, bool a6)
{
	NOTE_UNUSED(a6);

	if (miles_pcm_log.GetBool())
	{
		Msg(eDLL_T::AUDIO, "MilesSampleSetSourceRaw pass-through: a3=%u a4=%d a5=0x%04x a6=%d\n", a3, a4, a5, (int)a6);
	}
	return v_MilesSampleSetSourceRaw(a1, a2, a3, a4, a5, a6);
}

#pragma optimize( "", off )
static unsigned int MilesEventGetDetails(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6, void* const releaseList)
{
	NOTE_UNUSED(releaseList);
	// interface fix from 10.0.42 --> 10.0.57. Added 'releaseList'. Release
	// list isn't used by the game, we however must set it explicitly to null
	// here as Miles will otherwise attempt to dereference whatever is there
	// in the stack and write to it.
	return v_MilesEventGetDetails(a1, a2, a3, a4, a5, a6, nullptr);
}

void MilesShim::Detour(const bool bAttach) const
{
	DetourSetup(&v_MilesSampleSetSourceRaw, &MilesSampleSetSourceRaw, bAttach);
	DetourSetup(&v_MilesEventGetDetails, &MilesEventGetDetails, bAttach);
}
