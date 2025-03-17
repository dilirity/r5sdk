//============================================================================//
//
// Purpose: Debug interface functions
//
//============================================================================//

#include "core/stdafx.h"
#include "common/pseudodefs.h"
#include "tier0/memstd.h"
#include "tier0/basetypes.h"
#include "tier1/cvar.h"
#include "tier2/renderutils.h"
#include "engine/client/clientstate.h"
#include "engine/host_cmd.h"
#include "engine/debugoverlay.h"
#ifndef CLIENT_DLL
#include "engine/server/server.h"
#endif // !CLIENT_DLL
#include "materialsystem/cmaterialsystem.h"
#include "mathlib/mathlib.h"
#ifndef CLIENT_DLL
#include "game/shared/ai_utility_shared.h"
#include "game/server/ai_network.h"
#endif // !CLIENT_DLL

ConVar r_debug_draw_depth_test("r_debug_draw_depth_test", "1", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, "Toggle depth test for other debug draw functionality");
static ConVar r_debug_overlay_nodecay("r_debug_overlay_nodecay", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, "Keeps all debug overlays alive regardless of their lifetime. Use command 'clear_debug_overlays' to clear everything");

//------------------------------------------------------------------------------
// Purpose: returns whether the overlay can be added at this moment
//------------------------------------------------------------------------------
static bool CanAddOverlay()
{
#ifndef DEDICATED
    if (!g_pClientState->IsPaused())
        return true;
#endif // !DEDICATED

#ifndef CLIENT_DLL
    if (g_pServer->CanApplyOverlays())
        return true;
#endif // !CLIENT_DLL

    return false;
}

//-----------------------------------------------------------------------------
// Purpose: add new overlay capsule
//------------------------------------------------------------------------------
void CIVDebugOverlay::AddCapsuleOverlay(const Vector3D& vStart, const Vector3D& vEnd, const float flRadius, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration)
{
    if (!CanAddOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);
    OverlayCapsule_t* const newOverlay = new OverlayCapsule_t;

    if (!newOverlay)
        return;

    newOverlay->start = vStart;
    newOverlay->end = vEnd;
    newOverlay->radius = flRadius;
    newOverlay->r = r;
    newOverlay->g = g;
    newOverlay->b = b;
    newOverlay->a = a;
    newOverlay->noDepthTest = noDepthTest;

    newOverlay->SetEndTime(flDuration);

    newOverlay->m_pNextOverlay = *s_pOverlays;
    *s_pOverlays = newOverlay;
}

//------------------------------------------------------------------------------
// Purpose: checks if overlay should be decayed
// Output : true to decay, false otherwise
//------------------------------------------------------------------------------
bool OverlayBase_t::IsDead() const
{
    if (r_debug_overlay_nodecay.GetBool())
    {
        // Keep rendering the overlay if no-decay is set.
        return false;
    }

    if (g_pClientState->IsPaused())
    {
        // Keep rendering the overlay if the client simulation is paused.
        return false;
    }

    if (m_nCreationTick == -1)
    {
        if (m_nOverlayTick == -1)
        {
            if (m_flEndTime == NDEBUG_PERSIST_TILL_NEXT_SERVER)
            {
                return false;
            }
            return g_pClientState->GetClientTime() >= m_flEndTime;
        }
        else
        {
            return m_nOverlayTick < *g_nOverlayTickCount;
        }
    }
    else
    {
        return m_nCreationTick < *g_nRenderTickCount;
    }
}

//------------------------------------------------------------------------------
// Purpose: sets the overlay end time
// Input  : duration
//------------------------------------------------------------------------------
void OverlayBase_t::SetEndTime(const float duration)
{
    // todo: obtain pointer to g_nNewOtherOverlays and increment here!!!.
    m_nServerCount = g_pClientState->GetServerCount();

    if (duration == 0.0f)
    {
        m_nCreationTick = *g_nRenderTickCount;	// stay alive for only one frame
    }
    else if (duration == (NDEBUG_PERSIST_TILL_NEXT_SERVER * 2))
    {
        m_flEndTime = (NDEBUG_PERSIST_TILL_NEXT_SERVER * 2);
    }
    else if (duration == NDEBUG_PERSIST_TILL_NEXT_SERVER)
    {
        m_flEndTime = NDEBUG_PERSIST_TILL_NEXT_SERVER;
    }
    else
    {
        m_flEndTime = g_pClientState->GetClientTime() + duration;
    }
}

//------------------------------------------------------------------------------
// Purpose: destroys the overlay
// Input  : *pOverlay - 
//------------------------------------------------------------------------------
void DestroyOverlay(OverlayBase_t* pOverlay)
{
    AUTO_LOCK(*s_OverlayMutex);
    switch (pOverlay->m_Type)
    {
    case OverlayType_t::OVERLAY_BOX:
    case OverlayType_t::OVERLAY_SPHERE:
    case OverlayType_t::OVERLAY_LINE:
    case OverlayType_t::OVERLAY_CUSTOM_MESH:
    case OverlayType_t::OVERLAY_TRIANGLE:
    case OverlayType_t::OVERLAY_SWEPT_BOX:
    case OverlayType_t::OVERLAY_UNKNOWN:
    case OverlayType_t::OVERLAY_CAPSULE:
        pOverlay->m_Type = OverlayType_t::OVERLAY_DESTROYED;
        delete pOverlay;

        break;
        // The laser line overlay, used for the smart pistol's guidance
        // line, appears to be not deleted in this particular function.
        // Its unclear whether or not something else takes care of this,
        // research needed!!!
    case OverlayType_t::OVERLAY_SPLINE:
        pOverlay->m_Type = OverlayType_t::OVERLAY_DESTROYED;
        break;
    default:
        Assert(0); // Code bug; invalid overlay type.
        break;
    }
}

//------------------------------------------------------------------------------
// Purpose: draws a generic overlay
// Input  : *pOverlay - 
//------------------------------------------------------------------------------
void DrawOverlay(OverlayBase_t* pOverlay)
{
    AUTO_LOCK(*s_OverlayMutex);

    switch (pOverlay->m_Type)
    {
    case OverlayType_t::OVERLAY_BOX:
    {
        OverlayBox_t* pBox = static_cast<OverlayBox_t*>(pOverlay);

        if (pBox->a > 0)
        {
            RenderBox(pBox->transforms.mat, pBox->mins, pBox->maxs, Color(pBox->r, pBox->g, pBox->b, pBox->a), !pBox->noDepthTest);
        }
        if (pBox->a < 255)
        {
            v_RenderWireFrameBox(pBox->transforms.mat, pBox->mins, pBox->maxs, Color(pBox->r, pBox->g, pBox->b, 255), !pBox->noDepthTest);
        }

        break;
    }
    case OverlayType_t::OVERLAY_SPHERE:
    {
        OverlaySphere_t* pSphere = static_cast<OverlaySphere_t*>(pOverlay);
        v_RenderWireframeSphere(pSphere->vOrigin, pSphere->flRadius, pSphere->nTheta, pSphere->nPhi,
            Color(pSphere->r, pSphere->g, pSphere->b, pSphere->a), r_debug_draw_depth_test.GetBool());

        break;
    }
    case OverlayType_t::OVERLAY_LINE:
    {
        OverlayLine_t* pLine = static_cast<OverlayLine_t*>(pOverlay);
        v_RenderLine(pLine->origin, pLine->dest, Color(pLine->r, pLine->g, pLine->b, pLine->a), !pLine->noDepthTest);

        break;
    }
    case OverlayType_t::OVERLAY_CUSTOM_MESH:
    {
        // TODO: 128 * matrix3x4_t, figure out how to render this...
        // Nothing in the game is currently calling this overlay, so
        // implementing this isn't necessary.
        break;
    }
    case OverlayType_t::OVERLAY_SPLINE:
    {
        // This is used for the Smart Pistol laser.
        OverlayLine_t* pLaser = reinterpret_cast<OverlayLine_t*>(pOverlay);
        v_RenderLine(pLaser->origin, pLaser->dest, Color(pLaser->r, pLaser->g, pLaser->b, pLaser->a), !pLaser->noDepthTest);

        break;
    }
    case OverlayType_t::OVERLAY_TRIANGLE:
    {
        OverlayTriangle_t* pTriangle = reinterpret_cast<OverlayTriangle_t*>(pOverlay);
        RenderTriangle(pTriangle->p1, pTriangle->p2, pTriangle->p3, Color(pTriangle->r, pTriangle->g, pTriangle->b, pTriangle->a), !pTriangle->noDepthTest);

        break;
    }
    case OverlayType_t::OVERLAY_SWEPT_BOX:
    {
        OverlaySweptBox_t* pSweptBox = reinterpret_cast<OverlaySweptBox_t*>(pOverlay);
        RenderWireframeSweptBox(pSweptBox->start, pSweptBox->end, pSweptBox->angles, pSweptBox->mins, pSweptBox->maxs, 
            Color(pSweptBox->r, pSweptBox->g, pSweptBox->b, pSweptBox->a), r_debug_draw_depth_test.GetBool());
        break;
    }
    case OverlayType_t::OVERLAY_UNKNOWN:
    {
        //printf("UNK0 %p\n", pOverlay);
        break;
    }
    case OverlayType_t::OVERLAY_CAPSULE:
    {
        OverlayCapsule_t* pCapsule = static_cast<OverlayCapsule_t*>(pOverlay);
        RenderCapsule(pCapsule->start, pCapsule->end, pCapsule->radius, Color(pCapsule->r, pCapsule->g, pCapsule->b, pCapsule->a), !pCapsule->noDepthTest);

        break;
    }
    }
}

//------------------------------------------------------------------------------
// Purpose : overlay drawing entrypoint
// Input  : bRender - won't render anything if false
//------------------------------------------------------------------------------
static void DrawAllOverlays(const bool bRender)
{
    AUTO_LOCK(*s_OverlayMutex);

    const bool bOverlayEnabled = (bRender && enable_debug_overlays->GetBool());
    OverlayBase_t* pCurrOverlay = *s_pOverlays;
    OverlayBase_t* pPrevOverlay = nullptr;
    OverlayBase_t* pNextOverlay = nullptr;

    while (pCurrOverlay)
    {
        if (pCurrOverlay->IsDead())
        {
            if (pPrevOverlay)
            {
                // If I had a last overlay reset it's next pointer
                pPrevOverlay->m_pNextOverlay = pCurrOverlay->m_pNextOverlay;
            }
            else
            {
                // If the first line, reset the s_pOverlays pointer
                *s_pOverlays = pCurrOverlay->m_pNextOverlay;
            }

            pNextOverlay = pCurrOverlay->m_pNextOverlay;
            DestroyOverlay(pCurrOverlay);
            pCurrOverlay = pNextOverlay;
        }
        else
        {
            bool bShouldDraw = false;

            if (pCurrOverlay->m_nCreationTick == -1)
            {
                if (pCurrOverlay->m_nOverlayTick == *g_nOverlayTickCount ||
                    pCurrOverlay->m_nOverlayTick == -1)
                {
                    bShouldDraw = true;
                }
            }
            else
            {
                bShouldDraw = pCurrOverlay->m_nCreationTick == *g_nRenderTickCount;
            }
            if (bOverlayEnabled && bShouldDraw)
            {
                DrawOverlay(pCurrOverlay);
            }

            pPrevOverlay = pCurrOverlay;
            pCurrOverlay = pCurrOverlay->m_pNextOverlay;
        }
    }

#ifndef CLIENT_DLL
    if (bOverlayEnabled)
    {
        g_AIUtility.RunRenderFrame();
    }
#endif // !CLIENT_DLL
}

///////////////////////////////////////////////////////////////////////////////
void VDebugOverlay::Detour(const bool bAttach) const
{
    DetourSetup(&v_DrawAllOverlays, &DrawAllOverlays, bAttach);
}
