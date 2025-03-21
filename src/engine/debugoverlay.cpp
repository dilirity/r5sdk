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
#include "mathlib/mathlib.h"
#include "engine/client/clientstate.h"
#include "engine/host_cmd.h"
#include "engine/cmodel.h"
#include "engine/debugoverlay.h"
#include "materialsystem/cmaterialsystem.h"
#ifndef CLIENT_DLL
#include "engine/server/server.h"
#include "game/server/entitylist.h"
#include "game/server/baseentity.h"
#endif // !CLIENT_DLL
#ifndef DEDICATED
#include "game/client/c_baseentity.h"
#include "game/client/cliententitylist.h"
#endif // !DEDICATED
#if !defined(CLIENT_DLL) && !defined (DEDICATED)
#include "game/shared/ai_utility_shared.h"
#include "game/server/ai_network.h"
#endif // !CLIENT_DLL && !DEDICATED

ConVar enable_debug_text_overlays("enable_debug_text_overlays", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT | FCVAR_GAMEDLL, "Enable rendering of debug text overlays");

ConVar r_debug_draw_depth_test("r_debug_draw_depth_test", "1", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, "Toggle depth test for other debug draw functionality");
static ConVar r_debug_overlay_nodecay("r_debug_overlay_nodecay", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, "Keeps all debug overlays alive regardless of their lifetime. Use command 'clear_debug_overlays' to clear everything");

//------------------------------------------------------------------------------
// Purpose: returns whether the overlay can be added at this moment
//------------------------------------------------------------------------------
static bool DebugOverlay_CanAddOverlay()
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
// Purpose: determines and sets the end time for the overlay
//-----------------------------------------------------------------------------
static void DebugOverlay_SetEndTime(const float duration, int& creationTick, int& overlayTick, float& endTime)
{
    if (duration == 0.0f)
    {
        // note(kawe): this was originally 'n + 1' but this
        // makes it render for 2 frames causing moving text
        // and shape overlays to become blurry. This seems
        // to be done to make static text more solid on
        // lighter backgrounds, instead of having the same
        // characteristics of VGui text. If you wish to have
        // it more solid, pass the constant
        // NDEBUG_PERSIST_TILL_SECOND_NEXT_SERVER as value
        // for the duration.
        overlayTick = (*g_nOverlayStage) /*+ 1*/;	// stay alive for only one frame
    }
    else if (duration == (NDEBUG_PERSIST_TILL_SECOND_NEXT_SERVER))
    {
        creationTick = (*g_nRenderTickCount) + 1;
    }
    else if (duration == NDEBUG_PERSIST_TILL_NEXT_SERVER)
    {
        endTime = NDEBUG_PERSIST_TILL_NEXT_SERVER;
    }
    else
    {
        endTime = g_pClientState->GetClientTime() + duration;
    }
}

//-----------------------------------------------------------------------------
// Purpose: Hack to allow this code to run on a client that's not connected to a server
//  (i.e., demo playback, or multiplayer game )
// Input  : entNum - 
//          origin - 
//-----------------------------------------------------------------------------
static bool DebugOverlay_GetEntityOriginClientOrServer(const int entNum, Vector3D& origin)
{
#ifndef CLIENT_DLL
    if (g_pServer->IsActive())
    {
        const CEntInfo* const entInfo = g_serverEntityList->GetEntInfoPtrByIndex(entNum);
        const CBaseEntity* const serverEntity = (CBaseEntity*)entInfo->m_pEntity;

        if (!entInfo->m_pEntity)
            return false;

        CM_WorldSpaceCenter(serverEntity->CollisionProp(), &origin);
        return true;
    }
#endif // CLIENT_DLL

#ifndef DEDICATED
    IClientEntity* const clientEntity = g_clientEntityList->GetClientEntity(entNum);

    if (!clientEntity)
        return false;
#endif // DEDICATED

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: add new overlay capsule
//------------------------------------------------------------------------------
void CIVDebugOverlay::AddCapsuleOverlay(const Vector3D& vStart, const Vector3D& vEnd, const float flRadius, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration)
{
    if (!DebugOverlay_CanAddOverlay())
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
// Purpose: sets the shape overlay end time
// Input  : duration
//------------------------------------------------------------------------------
void OverlayBase_t::SetEndTime(const float duration)
{
    (*g_nNewOtherOverlays)++;
    m_nServerCount = g_pClientState->GetServerCount();

    if (m_Type == OverlayType_t::OVERLAY_SPLINE)
        m_nCreationTick = *g_nRenderTickCount;
    else
        DebugOverlay_SetEndTime(duration, m_nCreationTick, m_nOverlayTick, m_flEndTime);
}

//------------------------------------------------------------------------------
// Purpose: sets the text overlay end time
// Input  : duration
//------------------------------------------------------------------------------
void OverlayText_t::SetEndTime(const float duration)
{
    (*g_nNewTextOverlays)++;
    m_nServerCount = g_pClientState->GetServerCount();

    DebugOverlay_SetEndTime(duration, m_nCreationTick, m_nOverlayTick, m_flEndTime);
}

//------------------------------------------------------------------------------
// Purpose: destroys the overlay
// Input  : *pOverlay - 
//------------------------------------------------------------------------------
static void DebugOverlay_DestroyOverlay(OverlayBase_t* const pOverlay)
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
static void DebugOverlay_DrawOverlay(OverlayBase_t* const pOverlay)
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
static void DebugOverlay_DrawAllOverlays(const bool bRender)
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
            DebugOverlay_DestroyOverlay(pCurrOverlay);
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
                DebugOverlay_DrawOverlay(pCurrOverlay);
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

//-----------------------------------------------------------------------------
// Purpose: clears all overlays
//-----------------------------------------------------------------------------
static void DebugOverlay_ClearAllOverlays()
{
    AUTO_LOCK(*s_OverlayMutex);

    while (*s_pOverlays)
    {
        OverlayBase_t* pOldOverlay = *s_pOverlays;
        *s_pOverlays = (*s_pOverlays)->m_pNextOverlay;
        DebugOverlay_DestroyOverlay(pOldOverlay);
    }

    while (*s_pOverlayText)
    {
        OverlayText_t* cur_ol = *s_pOverlayText;
        *s_pOverlayText = (*s_pOverlayText)->nextOverlayText;
        delete cur_ol;
    }

    *s_bDrawGrid = false;
}

//-----------------------------------------------------------------------------
// Purpose: internal wrapper for adding new world positioned overlay text
//-----------------------------------------------------------------------------
static void DebugOverlay_AddTextOverlay(const Vector3D& pos, const int lineOffset, const float duration,
    const int r, const int g, const int b, const int a, const char* const text, const ssize_t textLen)
{
    OverlayText_t* const newOverlay = new OverlayText_t;

    if (!newOverlay)
        return;

    VectorCopy(pos, newOverlay->origin);

    newOverlay->textLen = textLen;
    newOverlay->textBuf = new char[textLen + 1];

    if (!newOverlay->textBuf)
        return;

    Q_strncpy(newOverlay->textBuf, text, textLen + 1);

    newOverlay->bUseOrigin = true;
    newOverlay->lineOffset = lineOffset;

    newOverlay->SetEndTime(duration);

    newOverlay->r = r;
    newOverlay->g = g;
    newOverlay->b = b;
    newOverlay->a = a;

    newOverlay->nextOverlayText = *s_pOverlayText;
    *s_pOverlayText = newOverlay;
}

//-----------------------------------------------------------------------------
// Purpose: add new entity positioned overlay text
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddEntityTextOverlay(CIVDebugOverlay* const thisptr, const int entIndex, const int lineOffset, const float duration, 
                                            const int r, const int g, const int b, const int a, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanAddOverlay())
        return;

    Vector3D pos;

    if (!DebugOverlay_GetEntityOriginClientOrServer(entIndex, pos))
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(pos, lineOffset, duration, r, g, b, a, thisptr->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddTextOverlay(CIVDebugOverlay* const thisptr, const Vector3D& origin, const float duration, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanAddOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(origin, 0, duration, 255, 255, 255, 255, thisptr->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text at line offset
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddTextOverlayAtOffset(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration, const char* const format, ...)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanAddOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(origin, lineOffset, duration, 255, 255, 255, 255, thisptr->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text using 32 bit color
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddTextOverlayRGBu32(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration, 
    const int r, const int g, const int b, const int a, PRINTF_FORMAT_STRING const char* const format, ...) FMTFUNCTION(9, 10)
{
    if (!enable_debug_text_overlays.GetBool())
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
        DebugOverlay_AddTextOverlay(origin, lineOffset, duration, r, g, b, a, thisptr->m_text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new world positioned overlay text using float color
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddTextOverlayRGBf32(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration,
    const float r, const float g, const float b, const float a, PRINTF_FORMAT_STRING const char* const format, ...) FMTFUNCTION(8, 9)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanAddOverlay())
        return;

    AUTO_LOCK(*s_OverlayMutex);

    va_start(thisptr->m_argptr, format);
    const int textLen = Q_vsnprintf(thisptr->m_text, sizeof(thisptr->m_text), format, thisptr->m_argptr);
    va_end(thisptr->m_argptr);

    if (textLen > 0)
    {
        const int cr = (int)Clamp(r * 255.f, 0.f, 255.f);
        const int cg = (int)Clamp(g * 255.f, 0.f, 255.f);
        const int cb = (int)Clamp(b * 255.f, 0.f, 255.f);
        const int ca = (int)Clamp(a * 255.f, 0.f, 255.f);

        DebugOverlay_AddTextOverlay(origin, lineOffset, duration, cr, cg, cb, ca, thisptr->m_text, textLen);
    }
}

//-----------------------------------------------------------------------------
// Purpose: internal wrapper for adding new screen positioned overlay text
//-----------------------------------------------------------------------------
static void DebugOverlay_AddScreenTextOverlay(const Vector2D& pos, const int lineOffset, const float duration,
    const int r, const int g, const int b, const int a, const char* const text, const ssize_t textLen)
{
    OverlayText_t* const newOverlay = new OverlayText_t;

    if (!newOverlay)
        return;

    newOverlay->screenPos = pos;

    newOverlay->textLen = textLen;
    newOverlay->textBuf = new char[textLen + 1];

    if (!newOverlay->textBuf)
        return;

    Q_strncpy(newOverlay->textBuf, text, textLen + 1);

    newOverlay->bUseOrigin = false;
    newOverlay->lineOffset = lineOffset;

    newOverlay->SetEndTime(duration);

    newOverlay->r = r;
    newOverlay->g = g;
    newOverlay->b = b;
    newOverlay->a = a;

    newOverlay->nextOverlayText = *s_pOverlayText;
    *s_pOverlayText = newOverlay;
}

//-----------------------------------------------------------------------------
// Purpose: add new screen positioned overlay text at offset
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddScreenTextOverlayAtOffset(CIVDebugOverlay* const thisptr, const Vector2D& screenPos, const int lineOffset,
    const float flDuration, const int r, const int g, const int b, const int a, const char* const text)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanAddOverlay())
        return;

    const ssize_t textLen = (ssize_t)strlen(text);

    if (textLen < 1)
        return; // Empty.

    AUTO_LOCK(*s_OverlayMutex);
    DebugOverlay_AddScreenTextOverlay(screenPos, lineOffset, flDuration, r, g, b, a, text, textLen);
}

//-----------------------------------------------------------------------------
// Purpose: add new screen positioned overlay text at center
//-----------------------------------------------------------------------------
void CIVDebugOverlay::AddScreenTextOverlayAtCenter(CIVDebugOverlay* const thisptr, IVDebugOverlay* const unused1, const char* const text, const void* unused2, const int unk1, const int unk2)
{
    if (!enable_debug_text_overlays.GetBool() || !DebugOverlay_CanAddOverlay())
        return;

    const ssize_t textLen = (ssize_t)strlen(text);

    if (textLen < 1)
        return; // Empty.

    AUTO_LOCK(*s_OverlayMutex);
    DebugOverlay_AddScreenTextOverlay({0.5f, 0.5f}, 0, 0.f, 255, 0, 0, unk2, text, textLen);
}

///////////////////////////////////////////////////////////////////////////////
void VDebugOverlay::Detour(const bool bAttach) const
{
    DetourSetup(&v_DebugOverlay_DrawAllOverlays, &DebugOverlay_DrawAllOverlays, bAttach);
    DetourSetup(&v_DebugOverlay_ClearAllOverlays, &DebugOverlay_ClearAllOverlays, bAttach);

    if (bAttach)
    {
        void* null;

        // Replace the nulled functions in the IVPhysicsDebugOverlay implementation with ours.
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddEntityTextOverlay, 0, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlay, 4, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlayAtOffset, 5, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddScreenTextOverlayAtOffset, 6, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddScreenTextOverlayAtCenter, 7, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVPhysicsDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlayRGBf32, 8, &null);

        // Replace the nulled functions in the IVDebugOverlay implementation with ours.
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddEntityTextOverlay, 0, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlayAtOffset, 8, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlay, 9, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddScreenTextOverlayAtOffset, 10, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlayRGBu32, 24, &null);
        CMemory::HookVirtualMethod((uintptr_t)g_pIVDebugOverlay_VFTable, CIVDebugOverlay::AddTextOverlayRGBf32, 25, &null);
    }
}
