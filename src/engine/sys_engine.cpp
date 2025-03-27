#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "sys_engine.h"
#ifdef DEDICATED
#include "game/shared/shareddefs.h"
#endif // DEDICATED

///////////////////////////////////////////////////////////////////////////////
CEngine* g_pEngine = nullptr;
IEngine::QuitState_t* gsm_Quitting = nullptr;

#ifdef DEDICATED
static ConVar server_fps_clampToTicks("server_fps_clampToTicks", "0", FCVAR_RELEASE, "Clamp the server FPS to TIME_TO_TICKS( 1.0f ); significantly reduces CPU usage, but requires more precise and stable timing from the machine.");

static inline void ClampServerFPSToTicks()
{
	// The first engine frame is ran before the global variables are initialized.
	// By default, the tick interval is set to '0.0f'; we can't divide by zero.
	if (TICK_INTERVAL == 0.0f)
		return;

	const int tickRate = TIME_TO_TICKS(1.0f);

	if (fps_max->GetInt() == tickRate)
		return;

	// Clamp the framerate of the server to its simulation tick rate.
	// This saves a significant amount of CPU time in CEngine::Frame,
	// as the engine uses this to decided when to run a new frame.
	fps_max->SetValue(tickRate);
}
#endif // DEDICATED

bool CEngine::_Frame(CEngine* thisp)
{
#ifdef DEDICATED
	if (server_fps_clampToTicks.GetBool())
		ClampServerFPSToTicks();
#endif // DEDICATED

	return CEngine__Frame(thisp);
}

void VEngine::Detour(const bool bAttach) const
{
	DetourSetup(&CEngine__Frame, &CEngine::_Frame, bAttach);
}
