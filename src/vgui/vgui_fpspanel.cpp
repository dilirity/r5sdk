//===========================================================================//
//
// Purpose: Framerate indicator panel.
//
// $NoKeywords: $
//===========================================================================//

#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "vgui/vgui_fpspanel.h"

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static ConVar* HCFPSPanel_Paint(void* thisptr)
{
	return CFPSPanel__Paint(thisptr);
}

///////////////////////////////////////////////////////////////////////////////
void VFPSPanel::Detour(const bool bAttach) const
{
	DetourSetup(&CFPSPanel__Paint, &HCFPSPanel_Paint, bAttach);
}
