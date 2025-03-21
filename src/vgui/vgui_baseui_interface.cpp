//===========================================================================//
//
// Purpose: Implements all the functions exported by the GameUI dll.
//
// $NoKeywords: $
//===========================================================================//

#include <core/stdafx.h>
#include <tier1/cvar.h>
#include <engine/sys_utils.h>
#include <engine/debugoverlay.h>
#include <vgui/vgui_debugpanel.h>
#include <vgui/vgui_baseui_interface.h>
#include <vguimatsurface/MatSystemSurface.h>

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CEngineVGui::VPaint(CEngineVGui* const thisptr, const PaintMode_t mode)
{
	const int result = CEngineVGui__Paint(thisptr, mode);

	if (/*mode == PaintMode_t::PAINT_UIPANELS ||*/ mode == PaintMode_t::PAINT_INGAMEPANELS) // Render in-main menu and in-game.
	{
		if (r_drawvgui->GetBool())
			g_TextOverlay.Update();
	}

	// This must always be called, even when VGui is disabled because
	// we still need to decay old text overlays. Else they will stack
	// up forever and burn CPU.
	g_pDebugOverlay->ClearDeadTextOverlays();
	return result;
}

///////////////////////////////////////////////////////////////////////////////
void VEngineVGui::Detour(const bool bAttach) const
{
	DetourSetup(&CEngineVGui__Paint, &CEngineVGui::VPaint, bAttach);
}

///////////////////////////////////////////////////////////////////////////////