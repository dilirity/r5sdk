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

	if (mode == PaintMode_t::PAINT_UIPANELS)
	{
		if (r_drawvgui->GetBool())
			g_TextOverlay.UpdateMiniConsole();
	}

	if (mode == PaintMode_t::PAINT_INGAMEPANELS)
	{
		if (r_drawvgui->GetBool())
			g_TextOverlay.UpdateInGamePanels();
	}

	return result;
}

///////////////////////////////////////////////////////////////////////////////
void VEngineVGui::Detour(const bool bAttach) const
{
	DetourSetup(&CEngineVGui__Paint, &CEngineVGui::VPaint, bAttach);
}

///////////////////////////////////////////////////////////////////////////////