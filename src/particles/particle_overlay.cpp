//===========================================================================//
//
// Purpose: particle system debug overlay
//
//===========================================================================//
#include "engine/debugoverlay.h"
#include "particle_overlay.h"
#include "engine/client/clientstate.h"
#include "gameui/imgui_system.h"
#include "gameui/IParticleOverlay.h"

ConVar particle_overlay_list_enable("particle_overlay_list_enable", "0", FCVAR_DEVELOPMENTONLY, "Draw the particle overlay list");
extern CGlobalVarsBase* gpGlobals;

static void ParticleOverlay_Run(CParticleMgr* const particleMgr)
{
	if (gpGlobals->frameTime <= 0.001f || !particle_overlay->GetBool() && !particle_overlay_list_tally->GetBool())
		return;

	g_particleOverlay.Begin();
	v_ParticleOverlay_Run(particleMgr);
	g_particleOverlay.End();
}

static void ParticleOverlay_AddScreenText(ParticleOverlayScreenText_s* const overlay, const char* const format, ...)
{
	if (!particle_overlay_list_enable.GetBool())
		return;

	// If this bool is set, the list will be rendered on the VGui
	// interface instead of the new imgui one. The VGui interface
	// cannot be explored in case there's too much to be displayed.
	const bool useVGui = particle_overlay_old->GetBool() || !ImguiSystem()->IsInitialized();

	if (!useVGui && g_particleOverlay.IsFrozen())
		return; // Don't render the text if the imgui list is frozen.

	char buf[256];

	va_list args;
	va_start(args, format);
	const int len = vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if (len > 0)
	{
		if (useVGui)
			g_pDebugOverlay->AddScreenTextOverlay(overlay->position, overlay->lineOffset, 0.0f, 255, 255, 255, 255, buf);
		else
			g_particleOverlay.AppendText(buf, (size_t)len);

		overlay->lineOffset++;
	}
}

void VParticleOverlay::Detour(const bool bAttach) const
{
	DetourSetup(&v_ParticleOverlay_Run, &ParticleOverlay_Run, bAttach);
	DetourSetup(&v_ParticleOverlay_AddScreenText, &ParticleOverlay_AddScreenText, bAttach);
}
