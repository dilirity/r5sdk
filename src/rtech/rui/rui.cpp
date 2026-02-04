//=============================================================================//
//
// Purpose: rUI Utilities
//
//=============================================================================//

#include "core/stdafx.h"

#include "rui.h"
#include "tier1/cvar.h"

static ConVar rui_drawEnable("rui_drawEnable", "1", FCVAR_RELEASE, "Draws the RUI if set", false, 0.f, false, 0.f, "1 = draw; 0 (zero) = no draw");
static ConVar rui_codeAsserts("rui_codeAsserts", "0", FCVAR_RELEASE, "Prints the RUI code assertions that fail to the console if set", false, 0.f, false, 0.f, "1 = print; 0 (zero) = no print");

//-----------------------------------------------------------------------------
// Purpose: draw RUI frame
//-----------------------------------------------------------------------------
static bool Rui_Draw(__int64* a1, __m128* a2, const __m128i* a3, __int64 a4, __m128* a5)
{
	if (!rui_drawEnable.GetBool())
		return false;

	return v_Rui_Draw(a1, a2, a3, a4, a5);
}

static void Rui_CodeAssert(RuiInstance_s* const ruiInstance, const char* const errorMsg)
{
	if (rui_codeAsserts.GetBool())
		Error(eDLL_T::UI, 0, "%s", errorMsg);

	ruiInstance->hasError = true;
}

void V_Rui::Detour(const bool bAttach) const
{
	DetourSetup(&v_Rui_Draw, &Rui_Draw, bAttach);

	void* orgCodeAssertMethod;
	CMemory::HookVirtualMethod((uintptr_t)s_ruiApi, Rui_CodeAssert, 2, &orgCodeAssertMethod);
}
