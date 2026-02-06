//=============================================================================//
// 
// Purpose: Expose native code to VScript API
// 
//-----------------------------------------------------------------------------
// 
// Read the documentation in 'game/shared/vscript_shared.cpp' before modifying
// existing code or adding new code!
// 
// To create server script bindings:
// - use the DEFINE_SERVER_SCRIPTFUNC_NAMED() macro.
// - prefix your function with "ServerScript_" i.e.: "ServerScript_GetVersion".
// 
//=============================================================================//

#include "core/stdafx.h"
#include "common/callback.h"
#include "engine/server/server.h"
#include "engine/host_state.h"
#include "engine/debugoverlay.h"
#include "pluginsystem/pluginsystem.h"
#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"

#include "game/shared/vscript_gamedll_defs.h"

#include "game/shared/vscript_shared.h"
#include "game/shared/vscript_debug_overlay_shared.h"

#include "liveapi/liveapi.h"
#include "vscript_server.h"
#include "player.h"
#include "detour_impl.h"

#include "engine/server/vengineserver_impl.h" // g_pEngineServer, CreateFakeClient
#include "game/server/gameinterface.h"        // g_pServerGameClients, ClientFullyConnect
#include "game/server/player_command.h"       // g_botInputs, BotInput

#include "thirdparty/recast/DetourCrowd/Include/DetourPathCorridor.h"
#include "public/game/server/ai_agent.h"  // g_traverseAnimDefaultCosts
#include <unordered_map>

//=============================================================================
// Bot Navigation Corridor System
//=============================================================================
struct BotCorridor
{
    dtPathCorridor corridor;
    dtNavMeshQuery query;
    dtQueryFilter filter;
    Hull_e hullType;
    bool initialized;

    BotCorridor() : hullType(HULL_HUMAN), initialized(false) {}
};

static std::unordered_map<int, BotCorridor*> g_corridors;
static int g_nextCorridorHandle = 1;

/*
=====================
SQVM_ServerScript_f

  Executes input on the
  VM in SERVER context.
=====================
*/
static void SQVM_ServerScript_f(const CCommand& args)
{
    if (args.ArgC() >= 2)
    {
        Script_Execute(args.ArgS(), SQCONTEXT::SERVER);
    }
}
static ConCommand script("script", SQVM_ServerScript_f, "Run input code as SERVER script on the VM", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL | FCVAR_CHEAT | FCVAR_SERVER_FRAME_THREAD);

//-----------------------------------------------------------------------------
// Purpose: server NDebugOverlay proxies
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_DebugDrawSolidBox(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSolidBox(v);
}
static SQRESULT ServerScript_DebugDrawSweptBox(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSweptBox(v);
}
static SQRESULT ServerScript_DebugDrawTriangle(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawTriangle(v);
}
static SQRESULT ServerScript_DebugDrawSolidSphere(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSolidSphere(v);
}
static SQRESULT ServerScript_DebugDrawCapsule(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawCapsule(v);
}
static SQRESULT ServerScript_DebugDrawText(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawText(v);
}
static SQRESULT ServerScript_CreateBox(HSQUIRRELVM v)
{
    return SharedScript_CreateBox(v);
}
static SQRESULT ServerScript_ClearBoxes(HSQUIRRELVM v)
{
    return SharedScript_ClearBoxes(v);
}

//-----------------------------------------------------------------------------
// Purpose: calculates the duration for the debug text overlay
//-----------------------------------------------------------------------------
static float ServerScript_DebugScreenText_DetermineDuration(HSQUIRRELVM v)
{
    const float serverFPS = script_server_fps->GetFloat();
    // Make sure the overlay exists as long as the entire server
    // script frame, as it must last until the next call from the
    // server is initiated. Otherwise the following happens:
    // 
    // - 1 / script_server_fps < NDEBUG_PERSIST_TILL_NEXT_SERVER =
    //                           text will flicker as they decay
    //                           before the next frame is fired.
    // - 1 / script_server_fps > NDEBUG_PERSIST_TILL_NEXT_SERVER =
    //                           text will overlap with previous
    //                           as the prev hasn't decayed yet.
    return 1.0f / serverFPS;
}

//-----------------------------------------------------------------------------
// Purpose: internal handler for adding debug texts on screen through scripts
//-----------------------------------------------------------------------------
static void ServerScript_Internal_DebugScreenTextWithColor(HSQUIRRELVM v, const float posX, const float posY, const Color color, const char* const text)
{
    const float duration = ServerScript_DebugScreenText_DetermineDuration(v);
    g_pDebugOverlay->AddScreenTextOverlay(posX, posY, duration, color.r(), color.g(), color.b(), color.a(), text);
}

//-----------------------------------------------------------------------------
// Purpose: adds a debug text on the screen at given position
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_DebugScreenText(HSQUIRRELVM v)
{
    if (g_pDebugOverlay)
    {
        SQFloat posX;
        SQFloat posY;
        const SQChar* text;

        sq_getfloat(v, 2, &posX);
        sq_getfloat(v, 3, &posY);
        sq_getstring(v, 4, &text);

        const Color color(255, 255, 255, 255);
        ServerScript_Internal_DebugScreenTextWithColor(v, posX, posY, color, text);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: adds a debug text on the screen at given position with color
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_DebugScreenTextWithColor(HSQUIRRELVM v)
{
    if (g_pDebugOverlay)
    {
        SQFloat posX;
        SQFloat posY;
        const SQChar* text;
        const SQVector3D* colorVec;

        sq_getfloat(v, 2, &posX);
        sq_getfloat(v, 3, &posY);
        sq_getstring(v, 4, &text);
        sq_getvector(v, 5, &colorVec);

        const Color color = Script_VectorToColor(colorVec, 1.0f);
        ServerScript_Internal_DebugScreenTextWithColor(v, posX, posY, color, text);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: sets whether the server could auto reload at this time (e.g. if
// server admin has host_autoReloadRate AND host_autoReloadRespectGameState
// set, and its time to auto reload, but the match hasn't finished yet, wait
// until this is set to proceed the reload of the server
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_SetAutoReloadState(HSQUIRRELVM v)
{
    SQBool state = false;
    sq_getbool(v, 2, &state);

    g_hostReloadState = state;
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: kicks a player by given name
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_KickPlayerByName(HSQUIRRELVM v)
{
    const SQChar* playerName = nullptr;
    const SQChar* reason = nullptr;

    sq_getstring(v, 2, &playerName);
    sq_getstring(v, 3, &reason);

    if (!VALID_CHARSTAR(playerName))
    {
        v_SQVM_ScriptError("Empty or null player name");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Discard empty strings, this will use the default message instead.
    if (!VALID_CHARSTAR(reason))
        reason = nullptr;

    g_BanSystem.KickPlayerByName(playerName, reason);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: kicks a player by given handle or id
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_KickPlayerById(HSQUIRRELVM v)
{
    const SQChar* playerHandle = nullptr;
    const SQChar* reason = nullptr;

    sq_getstring(v, 2, &playerHandle);
    sq_getstring(v, 3, &reason);

    if (!VALID_CHARSTAR(playerHandle))
    {
        v_SQVM_ScriptError("Empty or null player handle");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Discard empty strings, this will use the default message instead.
    if (!VALID_CHARSTAR(reason))
        reason = nullptr;

    g_BanSystem.KickPlayerById(playerHandle, reason);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: bans a player by given name
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_BanPlayerByName(HSQUIRRELVM v)
{
    const SQChar* playerName = nullptr;
    const SQChar* reason = nullptr;

    sq_getstring(v, 2, &playerName);
    sq_getstring(v, 3, &reason);

    if (!VALID_CHARSTAR(playerName))
    {
        v_SQVM_ScriptError("Empty or null player name");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Discard empty strings, this will use the default message instead.
    if (!VALID_CHARSTAR(reason))
        reason = nullptr;

    g_BanSystem.BanPlayerByName(playerName, reason);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: bans a player by given handle or id
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_BanPlayerById(HSQUIRRELVM v)
{
    const SQChar* playerHandle = nullptr;
    const SQChar* reason = nullptr;

    sq_getstring(v, 2, &playerHandle);
    sq_getstring(v, 3, &reason);

    if (!VALID_CHARSTAR(playerHandle))
    {
        v_SQVM_ScriptError("Empty or null player handle");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Discard empty strings, this will use the default message instead.
    if (!VALID_CHARSTAR(reason))
        reason = nullptr;

    g_BanSystem.BanPlayerById(playerHandle, reason);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: unbans a player by given Steam ID or ip address
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_UnbanPlayer(HSQUIRRELVM v)
{
    const SQChar* szCriteria = nullptr;
    sq_getstring(v, 2, &szCriteria);

    if (!VALID_CHARSTAR(szCriteria))
    {
        v_SQVM_ScriptError("Empty or null player criteria");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    g_BanSystem.UnbanPlayer(szCriteria);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_BroadcastServerTextMessage(HSQUIRRELVM v)
{
    const SQChar* pszPrefix = nullptr;
    const SQChar* pszMessage = nullptr;
    SQBool bAdminMsg = false;

    sq_getstring(v, 2, &pszPrefix);
    sq_getstring(v, 3, &pszMessage);
    sq_getbool(v, 4, &bAdminMsg);

    if (!VALID_CHARSTAR(pszPrefix))
    {
        v_SQVM_ScriptError("Null prefix string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (!VALID_CHARSTAR(pszMessage))
    {
        v_SQVM_ScriptError("Null message string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    SVC_SystemSayText message(pszPrefix, pszMessage, bAdminMsg);

    g_pServer->BroadcastMessage(&message, true, false);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_SendServerTextMessage(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;
    const SQChar* pszPrefix = nullptr;
    const SQChar* pszMessage = nullptr;
    SQBool bAdminMsg = false;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    sq_getstring(v, 2, &pszPrefix);
    sq_getstring(v, 3, &pszMessage);
    sq_getbool(v, 4, &bAdminMsg);

    if (!VALID_CHARSTAR(pszPrefix))
    {
        v_SQVM_ScriptError("Null prefix string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (!VALID_CHARSTAR(pszMessage))
    {
        v_SQVM_ScriptError("Null message string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    CClient* const pClient = g_pServer->GetClient(pPlayer->GetEdict() - 1);

    if (!pClient)
        return SQ_ERROR;

    SVC_SystemSayText message(pszPrefix, pszMessage, bAdminMsg);

    sq_pushbool(v, pClient->SendNetMsgEx(&message, false, false, false));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

// Chat Builder API - gives complete control over chat rendering
// Format: "CMD|data|CMD|data|..."
// Commands: N=newline, T=text, C=color(r,g,b), F=fade(dur,fade)

static SQRESULT ServerScript_ChatBuilder(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;
    const SQChar* pszCommands = nullptr;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    sq_getstring(v, 2, &pszCommands);

    if (!VALID_CHARSTAR(pszCommands))
    {
        v_SQVM_ScriptError("Null commands string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    CClient* const pClient = g_pServer->GetClient(pPlayer->GetEdict() - 1);

    if (!pClient)
        return SQ_ERROR;

    // Prefix commands with special marker in message field
    char szMarkedCommands[256];
    V_snprintf(szMarkedCommands, sizeof(szMarkedCommands), "~~~CB~~~%s", pszCommands);

    // Send with empty prefix
    SVC_SystemSayText message("", szMarkedCommands, true);

    sq_pushbool(v, pClient->SendNetMsgEx(&message, false, false, false));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

// Broadcast ChatBuilder message to all players
static SQRESULT ServerScript_BroadcastChatBuilder(HSQUIRRELVM v)
{
    const SQChar* pszCommands = nullptr;
    sq_getstring(v, 2, &pszCommands);

    if (!VALID_CHARSTAR(pszCommands))
    {
        v_SQVM_ScriptError("Null commands string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Prefix commands with special marker in message field
    char szMarkedCommands[256];
    V_snprintf(szMarkedCommands, sizeof(szMarkedCommands), "~~~CB~~~%s", pszCommands);

    // Send with empty prefix
    SVC_SystemSayText message("", szMarkedCommands, true);

    g_pServer->BroadcastMessage(&message, true, false);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ServerScript_ChatBuilderRainbow(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;
    const SQChar* pszText = nullptr;
    SQFloat flDuration = 5.0f;
    SQFloat flFadeTime = 1.0f;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    sq_getstring(v, 2, &pszText);
    sq_getfloat(v, 3, &flDuration);
    sq_getfloat(v, 4, &flFadeTime);

    if (!VALID_CHARSTAR(pszText))
    {
        v_SQVM_ScriptError("Null text string");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    CClient* const pClient = g_pServer->GetClient(pPlayer->GetEdict() - 1);
    if (!pClient)
        return SQ_ERROR;

    // Build simplified command using client-side 'R' command
    // Format: "N|F|duration,fade|R|text|"
    char szCommands[512];
    int written = snprintf(szCommands, sizeof(szCommands), "N|F|%.1f,%.1f|R|%s|",
        flDuration, flFadeTime, pszText);

    if (written < 0 || written >= sizeof(szCommands))
    {
        v_SQVM_ScriptError("Command buffer overflow - text too long");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Send the message
    char szMarkedCommands[512];
    V_snprintf(szMarkedCommands, sizeof(szMarkedCommands), "~~~CB~~~%s", szCommands);

    SVC_SystemSayText message("", szMarkedCommands, true);
    sq_pushbool(v, pClient->SendNetMsgEx(&message, false, false, false));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the number of real players on this server
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_GetNumHumanPlayers(HSQUIRRELVM v)
{
    sq_pushinteger(v, g_pServer->GetNumHumanPlayers());
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the number of fake players on this server
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_GetNumFakeClients(HSQUIRRELVM v)
{
    sq_pushinteger(v, g_pServer->GetNumFakeClients());
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets our current session id
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_GetSessionID(HSQUIRRELVM v)
{
    sq_pushstring(v, g_LogSessionUUID.c_str(), (SQInteger)g_LogSessionUUID.length());
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: creates a fake player and returns the edict index
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_CreateFakePlayer(HSQUIRRELVM v)
{
    if (!g_pServer->IsActive())
    {
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const int numPlayers = g_pServer->GetNumClients();

    // Already at max, don't create
    if (numPlayers >= g_ServerGlobalVariables->maxClients)
    {
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const SQChar* playerName;
    SQInteger teamNum;

    // Get parameters
    sq_getstring(v, 2, &playerName);
    sq_getinteger(v, 3, &teamNum);

    if (!VALID_CHARSTAR(playerName))
    {
        v_SQVM_ScriptError("Empty or null player name");
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Thread synchronization required
    ThreadJoinServerJob();

    // Create the fake client
    const edict_t nHandle = g_pEngineServer->CreateFakeClient(playerName, static_cast<int>(teamNum));

    if (nHandle < 0)
    {
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Fully connect the client
    g_pServerGameClients->ClientFullyConnect(nHandle, false);

    // Return the edict index - scripts can use GetPlayerArray() or GetPlayerByIndex() to get the entity
    sq_pushinteger(v, static_cast<SQInteger>(nHandle));
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: sets a class var on the server and each client
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_ScriptSetClassVar(HSQUIRRELVM v)
{
    CPlayer* player = nullptr;

    if (!v_sq_getentity(v, (SQEntity*)&player))
        return SQ_ERROR;

    const SQChar* key = nullptr;
    sq_getstring(v, 2, &key);

    if (!VALID_CHARSTAR(key))
    {
        v_SQVM_ScriptError("Empty or null class key");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const SQChar* val = nullptr;
    sq_getstring(v, 3, &val);

    if (!VALID_CHARSTAR(val))
    {
        v_SQVM_ScriptError("Empty or null class value");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    CClient* const client = g_pServer->GetClient(player->GetEdict() - 1);
    SVC_SetClassVar msg(key, val);

    const bool success = client->SendNetMsgEx(&msg, false, true, false);

    if (success)
    {
        const char* pArgs[3] = {
            "_setClassVarServer",
            key,
            val
        };

        const CCommand cmd((int)V_ARRAYSIZE(pArgs), pArgs, cmd_source_t::kCommandSrcCode);
        const int oldIdx = *g_nCommandClientIndex;

        *g_nCommandClientIndex = client->GetUserID();
        v__setClassVarServer_f(cmd);

        *g_nCommandClientIndex = oldIdx;
    }

    sq_pushbool(v, success);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: checks if the provided hull type is valid
//-----------------------------------------------------------------------------
static bool Internal_ServerScript_ValidateHull(const SQInteger hull)
{
    if (hull < 0 || hull >= Hull_e::NUM_HULLS)
    {
        v_SQVM_ScriptError("Hull type with value %d does not index the maximum number of hulls(%d)", hull, Hull_e::NUM_HULLS);
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: checks if the provided half-extents is valid
//-----------------------------------------------------------------------------
static bool Internal_ServerScript_NavMesh_GetExtents(HSQUIRRELVM v, const SQInteger stackIdx, rdVec3D* const out)
{
    const SQVector3D* extents;
    sq_getvector(v, stackIdx, &extents);

    const SQFloat maxMagnitudeSqr = 9000000.0f;
    const SQFloat magnitudeSqr = extents->Dot();

    if (magnitudeSqr > maxMagnitudeSqr)
    {
        v_SQVM_ScriptError("Extents magnitude (%f) is too big. Max magnitude is %f", sqrtf(magnitudeSqr), sqrtf(maxMagnitudeSqr));
        return false;
    }

    if (extents->x <= 0.f || extents->y <= 0.f || extents->z <= 0.f)
    {
        v_SQVM_ScriptError("Extents elements (%f, %f, %f) must all be greater than zero", extents->x, extents->y, extents->z);
        return false;
    }

    out->init(extents->x, extents->y, extents->z);
    return true;
}
 
//-----------------------------------------------------------------------------
// Purpose: finds nearest polygon prioritizing Z-closeness over 3D distance.
// This prevents selecting polygons on objects above/below when multiple
// polygons exist at the same XY location.
// Returns true if a polygon was found, false otherwise.
//-----------------------------------------------------------------------------
static bool Internal_FindNearestPolyByHeight(
    dtNavMeshQuery* query,
    const rdVec3D* center,
    const rdVec3D* halfExtents,
    const dtQueryFilter* filter,
    dtPolyRef* outRef,
    rdVec3D* outNearest)
{
    // Query all polygons in the search box
    dtPolyRef polys[64];
    int polyCount = 0;

    dtStatus status = query->queryPolygons(center, halfExtents, filter, polys, &polyCount, 64);
    if (dtStatusFailed(status) || polyCount == 0)
    {
        *outRef = 0;
        return false;
    }

    // Find polygon with closest height to query position
    float bestZDiff = FLT_MAX;
    dtPolyRef bestRef = 0;
    rdVec3D bestPos(0, 0, 0);

    for (int i = 0; i < polyCount; i++)
    {
        float height = 0.f;
        status = query->getPolyHeight(polys[i], center, &height);
        if (dtStatusFailed(status))
            continue;

        float zDiff = fabsf(height - center->z);
        if (zDiff < bestZDiff)
        {
            bestZDiff = zDiff;
            bestRef = polys[i];
            bestPos.init(center->x, center->y, height);
        }
    }

    if (bestRef == 0)
    {
        *outRef = 0;
        return false;
    }

    *outRef = bestRef;
    *outNearest = bestPos;
    return true;
}

//-----------------------------------------------------------------------------
// Purpose: finds the nearest poly to given point, with an optional bounds filter.
//-----------------------------------------------------------------------------
static bool Internal_ServerScript_NavMesh_FindNearestPos(HSQUIRRELVM v, const bool useBounds)
{
    SQInteger hullIdx;
    sq_getinteger(v, useBounds ? 4 : 3, &hullIdx);

    if (!Internal_ServerScript_ValidateHull(hullIdx))
        return false;

    const Hull_e hullType = Hull_e(hullIdx);

    const NavMeshType_e navType = NAI_Hull::NavMeshType(hullType);
    const dtNavMesh* const nav = Detour_GetNavMeshByType(navType);

    if (!nav)
    {
        v_SQVM_ScriptError("NavMesh \"%s\" for hull \"%s\" hasn't been loaded!",
            NavMesh_GetNameForType(navType), g_aiHullNames[hullType]);

        return false;
    }

    rdVec3D halfExtents;

    if (useBounds)
    {
        if (!Internal_ServerScript_NavMesh_GetExtents(v, 3, &halfExtents))
            return false;
    }
    else
    {
        const Vector3D& maxs = NAI_Hull::Maxs(hullType);
        halfExtents.init(maxs.x, maxs.y, maxs.z);
    }

    const SQVector3D* point;
    sq_getvector(v, 2, &point);

    const rdVec3D searchPoint(point->x, point->y, point->z);

    dtNavMeshQuery query;
    query.attachNavMeshUnsafe(nav);

    dtQueryFilter filter;
    filter.setIncludeFlags(DT_POLYFLAGS_ALL);
    filter.setExcludeFlags(DT_POLYFLAGS_DISABLED);

    dtPolyRef nearestRef;
    rdVec3D nearestPt;

    if (!Internal_FindNearestPolyByHeight(&query, &searchPoint, &halfExtents, &filter, &nearestRef, &nearestPt))
    {
        v->PushNull();
        return true;
    }

    if (!nearestRef)
    {
        v->PushNull();
        return true;
    }

    const SQVector3D result(nearestPt.x, nearestPt.y, nearestPt.z);
    sq_pushvector(v, &result);

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: finds the nearest polygon to provided point
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetNearestPos(HSQUIRRELVM v)
{
    const bool ret = Internal_ServerScript_NavMesh_FindNearestPos(v, false);

    if (!ret)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: finds the nearest polygon to provided point within extents
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetNearestPosInBounds(HSQUIRRELVM v)
{
    const bool ret = Internal_ServerScript_NavMesh_FindNearestPos(v, true);

    if (!ret)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: finds the closest point on navmesh even when query point is outside
//          polygon bounds. Used for recovery when bot is completely off-mesh.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_FindClosestNavmeshPoint(HSQUIRRELVM v)
{
    // Parameters: (vector searchPoint, vector halfExtents, int hullType)
    // Slot 2: searchPoint, Slot 3: halfExtents, Slot 4: hullType
    SQInteger hullIdx;
    sq_getinteger(v, 4, &hullIdx);

    if (!Internal_ServerScript_ValidateHull(hullIdx))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const Hull_e hullType = Hull_e(hullIdx);
    const NavMeshType_e navType = NAI_Hull::NavMeshType(hullType);
    const dtNavMesh* const nav = Detour_GetNavMeshByType(navType);

    if (!nav)
    {
        v_SQVM_ScriptError("NavMesh \"%s\" for hull \"%s\" hasn't been loaded!",
            NavMesh_GetNameForType(navType), g_aiHullNames[hullType]);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    rdVec3D halfExtents;
    if (!Internal_ServerScript_NavMesh_GetExtents(v, 3, &halfExtents))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const SQVector3D* point = nullptr;
    sq_getvector(v, 2, &point);
    const rdVec3D searchPoint(point->x, point->y, point->z);

    dtNavMeshQuery query;
    query.attachNavMeshUnsafe(nav);

    dtQueryFilter filter;
    filter.setIncludeFlags(DT_POLYFLAGS_ALL);
    filter.setExcludeFlags(DT_POLYFLAGS_DISABLED);

    // Query all polygons in the search box
    dtPolyRef polys[64];
    int polyCount = 0;

    dtStatus status = query.queryPolygons(&searchPoint, &halfExtents, &filter, polys, &polyCount, 64);
    if (dtStatusFailed(status) || polyCount == 0)
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Find polygon with closest point to query position
    float bestDistSq = FLT_MAX;
    dtPolyRef bestRef = 0;
    rdVec3D bestPos(0, 0, 0);

    for (int i = 0; i < polyCount; i++)
    {
        rdVec3D closestPt;
        bool posOverPoly = false;
        status = query.closestPointOnPoly(polys[i], &searchPoint, &closestPt, &posOverPoly);
        if (dtStatusFailed(status))
            continue;

        // Calculate distance to this point
        float dx = closestPt.x - searchPoint.x;
        float dy = closestPt.y - searchPoint.y;
        float dz = closestPt.z - searchPoint.z;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestRef = polys[i];
            bestPos = closestPt;
        }
    }

    if (bestRef == 0)
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const SQVector3D result(bestPos.x, bestPos.y, bestPos.z);
    sq_pushvector(v, &result);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// NavMesh Pathfinding API
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: resolves navmesh + sets up filter and extents for a given hull type.
// Returns the navmesh pointer, or null (and raises script error) on failure.
//-----------------------------------------------------------------------------
static dtNavMesh* Internal_ServerScript_NavMesh_GetNavMesh(
    HSQUIRRELVM v,
    const SQInteger hullIdx,
    dtQueryFilter* outFilter,
    rdVec3D* outHalfExtents)
{
    if (!Internal_ServerScript_ValidateHull(hullIdx))
        return nullptr;

    const Hull_e hullType = Hull_e(hullIdx);
    const NavMeshType_e navType = NAI_Hull::NavMeshType(hullType);
    dtNavMesh* const nav = Detour_GetNavMeshByType(navType);

    if (!nav)
    {
        v_SQVM_ScriptError("NavMesh \"%s\" for hull \"%s\" hasn't been loaded!",
            NavMesh_GetNameForType(navType), g_aiHullNames[hullType]);
        return nullptr;
    }

    outFilter->setIncludeFlags(DT_POLYFLAGS_ALL);
    outFilter->setExcludeFlags(DT_POLYFLAGS_DISABLED);

    // Use full hull height for search box - Z-priority selection handles multi-floor
    const Vector3D& maxs = NAI_Hull::Maxs(hullType);
    outHalfExtents->init(maxs.x, maxs.y, maxs.z);

    return nav;
}

//=============================================================================
// NavMesh Corridor API
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: creates a path corridor for bot navigation. Returns handle.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_CreateCorridor(HSQUIRRELVM v)
{
    SQInteger hullIdx;
    sq_getinteger(v, 2, &hullIdx);

    if (!Internal_ServerScript_ValidateHull(hullIdx))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const Hull_e hullType = Hull_e(hullIdx);
    const NavMeshType_e navType = NAI_Hull::NavMeshType(hullType);
    const dtNavMesh* const nav = Detour_GetNavMeshByType(navType);

    if (!nav)
    {
        v_SQVM_ScriptError("NavMesh_CreateCorridor: navmesh not loaded for hull type %d", hullIdx);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    BotCorridor* pCorridor = new BotCorridor();
    pCorridor->hullType = hullType;

    // Initialize the navmesh query with node pools for pathfinding
    if (dtStatusFailed(pCorridor->query.init(nav, 2048)))
    {
        delete pCorridor;
        v_SQVM_ScriptError("NavMesh_CreateCorridor: failed to init navmesh query");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Initialize corridor path buffer
    if (!pCorridor->corridor.init(256))
    {
        delete pCorridor;
        v_SQVM_ScriptError("NavMesh_CreateCorridor: failed to init corridor");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    // Setup filter for pilot traversal (includes ziplines)
    pCorridor->filter.setIncludeFlags(0xFFFF);
    pCorridor->filter.setExcludeFlags(0);
    pCorridor->filter.setTraverseFlags(0x8013F); // ANIMTYPE_PILOT traverse types (0-5, 8, 19)

    // Set traverse costs from game's default table so A* properly weighs
    // traverse links against walking distance. Without this, all traverse costs
    // default to 1.0, making climbs/jumps appear ~300x cheaper than walking.
    for (int i = 0; i < DT_MAX_TRAVERSE_TYPES; i++)
        pCorridor->filter.setTraverseCost(i, g_traverseAnimDefaultCosts[ANIMTYPE_PILOT][i]);

    // Type 19 (ziplines) has 0 cost in pilot table - override with reasonable value
    // Use similar cost to wall climb (type 8) since ziplines are similar effort
    pCorridor->filter.setTraverseCost(19, 1294.07f);

    pCorridor->initialized = true;

    const int handle = g_nextCorridorHandle++;
    g_corridors[handle] = pCorridor;

    sq_pushinteger(v, handle);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: destroys a path corridor, freeing its memory.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_DestroyCorridor(HSQUIRRELVM v)
{
    SQInteger handle;
    sq_getinteger(v, 2, &handle);

    auto it = g_corridors.find((int)handle);
    if (it == g_corridors.end())
    {
        // Corridor not found - not an error, just return
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    delete it->second;
    g_corridors.erase(it);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: sets the path in the corridor from start to target position.
// Internally performs findPath + setCorridor.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_CorridorSetPath(HSQUIRRELVM v)
{
    SQInteger handle;
    const SQVector3D* startVec = nullptr;
    const SQVector3D* targetVec = nullptr;

    sq_getinteger(v, 2, &handle);
    sq_getvector(v, 3, &startVec);
    sq_getvector(v, 4, &targetVec);

    auto it = g_corridors.find((int)handle);
    if (it == g_corridors.end())
    {
        v_SQVM_ScriptError("NavMesh_CorridorSetPath: invalid corridor handle %d", handle);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    BotCorridor* pCorridor = it->second;
    if (!pCorridor->initialized)
    {
        v_SQVM_ScriptError("NavMesh_CorridorSetPath: corridor not initialized");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const rdVec3D startPos(startVec->x, startVec->y, startVec->z);
    const rdVec3D targetPos(targetVec->x, targetVec->y, targetVec->z);

    // Get extents for poly search - use full hull height, Z-priority handles multi-floor
    const Vector3D& maxs = NAI_Hull::Maxs(pCorridor->hullType);
    const rdVec3D halfExtents(maxs.x, maxs.y, maxs.z);

    // Find nearest polys (Z-priority to avoid wrong floor)
    dtPolyRef startRef = 0, targetRef = 0;
    rdVec3D nearestStart, nearestTarget;

    if (!Internal_FindNearestPolyByHeight(&pCorridor->query, &startPos, &halfExtents, &pCorridor->filter, &startRef, &nearestStart))
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    if (!Internal_FindNearestPolyByHeight(&pCorridor->query, &targetPos, &halfExtents, &pCorridor->filter, &targetRef, &nearestTarget))
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Find path
    dtPolyRef pathPolys[256];
    unsigned char pathJumps[256];
    int pathCount = 0;

    dtStatus status = pCorridor->query.findPath(
        startRef, targetRef,
        &nearestStart, &nearestTarget, &pCorridor->filter,
        pathPolys, pathJumps, &pathCount, 256);

    if (dtStatusFailed(status) || pathCount == 0)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Load path into corridor
    pCorridor->corridor.setCorridor(&nearestTarget, pathPolys, pathJumps, pathCount);
    pCorridor->corridor.setPos(&nearestStart);

    sq_pushbool(v, true);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: updates the corridor position, sliding it forward as the agent moves.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_CorridorMove(HSQUIRRELVM v)
{
    SQInteger handle;
    const SQVector3D* posVec = nullptr;

    sq_getinteger(v, 2, &handle);
    sq_getvector(v, 3, &posVec);

    auto it = g_corridors.find((int)handle);
    if (it == g_corridors.end())
    {
        v_SQVM_ScriptError("NavMesh_CorridorMove: invalid corridor handle %d", handle);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    BotCorridor* pCorridor = it->second;
    if (!pCorridor->initialized)
    {
        v_SQVM_ScriptError("NavMesh_CorridorMove: corridor not initialized");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const rdVec3D pos(posVec->x, posVec->y, posVec->z);

    const bool success = pCorridor->corridor.movePosition(&pos, &pCorridor->query, &pCorridor->filter);

    sq_pushbool(v, success);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets steering corners from the corridor. Returns array of
// {pos, traverseType} tables, or empty array if no path.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_CorridorGetCorners(HSQUIRRELVM v)
{
    SQInteger handle;
    SQInteger maxCorners;

    sq_getinteger(v, 2, &handle);
    sq_getinteger(v, 3, &maxCorners);

    auto it = g_corridors.find((int)handle);
    if (it == g_corridors.end())
    {
        v_SQVM_ScriptError("NavMesh_CorridorGetCorners: invalid corridor handle %d", handle);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    BotCorridor* pCorridor = it->second;
    if (!pCorridor->initialized)
    {
        v_SQVM_ScriptError("NavMesh_CorridorGetCorners: corridor not initialized");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (maxCorners < 1) maxCorners = 1;
    if (maxCorners > 8) maxCorners = 8;

    rdVec3D cornerVerts[8];
    unsigned char cornerFlags[8];
    dtPolyRef cornerPolys[8];
    unsigned char cornerJumps[8];

    const int cornerCount = pCorridor->corridor.findCorners(
        cornerVerts, cornerFlags, cornerPolys, cornerJumps,
        (int)maxCorners, &pCorridor->query, &pCorridor->filter);

    // Create array of {pos, traverseType} tables
    sq_newarray(v, 0);

    for (int i = 0; i < cornerCount; i++)
    {
        sq_newtable(v);

        sq_pushstring(v, "pos", -1);
        const SQVector3D sqPos(cornerVerts[i].x, cornerVerts[i].y, cornerVerts[i].z);
        sq_pushvector(v, &sqPos);
        sq_newslot(v, -3);

        sq_pushstring(v, "traverseType", -1);
        // Mask off the off-mesh flags (bits 6-7), keep only traverse type (bits 0-4)
        sq_pushinteger(v, cornerJumps[i] & (DT_MAX_TRAVERSE_TYPES - 1));
        sq_newslot(v, -3);

        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets traversal links for the specific polygon at the given position.
// Returns array of {startPos, endPos, traverseType, traverseDist}.
// Only returns traverse links connected to the polygon at the query position.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetTileTraversePortals(HSQUIRRELVM v)
{
    const SQVector3D* posVec = nullptr;
    SQInteger hullIdx;

    sq_getvector(v, 2, &posVec);
    sq_getinteger(v, 3, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const rdVec3D pos(posVec->x, posVec->y, posVec->z);

    // Find the specific polygon at this position
    dtNavMeshQuery query;
    query.attachNavMeshUnsafe(nav);

    dtPolyRef polyRef = 0;
    rdVec3D nearestPt;

    if (!Internal_FindNearestPolyByHeight(&query, &pos, &halfExtents, &filter, &polyRef, &nearestPt))
    {
        sq_newarray(v, 0);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    if (!polyRef)
    {
        sq_newarray(v, 0);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Get the tile and polygon
    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    nav->getTileAndPolyByRefUnsafe(polyRef, &tile, &poly);

    if (!tile || !poly)
    {
        sq_newarray(v, 0);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Create result array
    sq_newarray(v, 0);

    // Follow the link chain for this specific polygon only
    unsigned int linkIdx = poly->firstLink;
    while (linkIdx != DT_NULL_LINK)
    {
        const dtLink* link = &tile->links[linkIdx];

        // Check if this link has a traverse type (not normal walking)
        if (link->hasTraverseType())
        {
            // Get the edge vertices for this link
            const unsigned char edgeIdx = link->edge;
            const unsigned short va = poly->verts[edgeIdx];
            const unsigned short vb = poly->verts[(edgeIdx + 1) % poly->vertCount];

            const rdVec3D* vertA = &tile->verts[va];
            const rdVec3D* vertB = &tile->verts[vb];

            // Calculate edge midpoint (this is where the traverse starts)
            rdVec3D edgeMid;
            edgeMid.x = (vertA->x + vertB->x) * 0.5f;
            edgeMid.y = (vertA->y + vertB->y) * 0.5f;
            edgeMid.z = (vertA->z + vertB->z) * 0.5f;

            // Get the target polygon to find the landing position
            const dtMeshTile* targetTile = nullptr;
            const dtPoly* targetPoly = nullptr;
            nav->getTileAndPolyByRefUnsafe(link->ref, &targetTile, &targetPoly);

            rdVec3D startPos = edgeMid; // Default to edge mid
            rdVec3D targetPos = edgeMid; // Default to edge mid if we can't find target
            if (targetTile && targetPoly)
            {
                // For off-mesh connections (ziplines, etc.), use both endpoint vertices
                // Off-mesh polys have 2 verts: one near the source, one at the destination
                if (targetPoly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION && targetPoly->vertCount == 2)
                {
                    const rdVec3D* v0 = &targetTile->verts[targetPoly->verts[0]];
                    const rdVec3D* v1 = &targetTile->verts[targetPoly->verts[1]];

                    // Pick vertex closest to source poly center as start, furthest as end
                    const float dist0 = rdVdistSqr(v0, &poly->center);
                    const float dist1 = rdVdistSqr(v1, &poly->center);
                    if (dist0 < dist1)
                    {
                        startPos = *v0;
                        targetPos = *v1;
                    }
                    else
                    {
                        startPos = *v1;
                        targetPos = *v0;
                    }
                }
                else
                {
                    // Regular polygon - use center as the end position
                    targetPos = targetPoly->center;
                }
            }

            sq_newtable(v);

            // Start position (edge midpoint, or off-mesh connection start)
            sq_pushstring(v, "startPos", -1);
            const SQVector3D sqStartPos(startPos.x, startPos.y, startPos.z);
            sq_pushvector(v, &sqStartPos);
            sq_newslot(v, -3);

            // End position (target poly center)
            sq_pushstring(v, "endPos", -1);
            const SQVector3D sqEndPos(targetPos.x, targetPos.y, targetPos.z);
            sq_pushvector(v, &sqEndPos);
            sq_newslot(v, -3);

            // Traverse type
            sq_pushstring(v, "traverseType", -1);
            sq_pushinteger(v, link->getTraverseType());
            sq_newslot(v, -3);

            // Traverse distance - calculate from positions if stored value is 0
            sq_pushstring(v, "traverseDist", -1);
            float traverseDist = link->traverseDist * DT_TRAVERSE_DIST_QUANT_FACTOR;
            if (traverseDist <= 0.0f)
                traverseDist = rdVdist(&startPos, &targetPos);
            sq_pushfloat(v, traverseDist);
            sq_newslot(v, -3);

            // Source polygon center
            sq_pushstring(v, "sourceCenter", -1);
            const SQVector3D sqSrcCenter(poly->center.x, poly->center.y, poly->center.z);
            sq_pushvector(v, &sqSrcCenter);
            sq_newslot(v, -3);

            // Edge vertex A
            sq_pushstring(v, "vertA", -1);
            const SQVector3D sqVertA(vertA->x, vertA->y, vertA->z);
            sq_pushvector(v, &sqVertA);
            sq_newslot(v, -3);

            // Edge vertex B
            sq_pushstring(v, "vertB", -1);
            const SQVector3D sqVertB(vertB->x, vertB->y, vertB->z);
            sq_pushvector(v, &sqVertB);
            sq_newslot(v, -3);

            sq_arrayappend(v, -2);
        }

        linkIdx = link->next;
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// Bot Control API
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: creates a bot (fake client) and returns its edict index.
// Returns -1 on failure. Use GetEntByIndex() in script to get the entity.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_Bot_Create(HSQUIRRELVM v)
{
    const SQChar* pszName = nullptr;
    SQInteger teamNum;

    sq_getstring(v, 2, &pszName);
    sq_getinteger(v, 3, &teamNum);

    if (!VALID_CHARSTAR(pszName))
    {
        v_SQVM_ScriptError("Bot_Create: null or empty name");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (g_pServer->GetNumClients() >= g_ServerGlobalVariables->maxClients)
    {
        // Server full — return -1, not an error (caller should handle this)
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const edict_t nHandle = g_pEngineServer->CreateFakeClient(pszName, (int)teamNum);

    if (nHandle == FL_EDICT_INVALID)
    {
        sq_pushinteger(v, -1);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    g_pServerGameClients->ClientFullyConnect(nHandle, false);

    sq_pushinteger(v, (SQInteger)nHandle);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: sets the bot's input for the current frame.
// Only works on bot (fake client) players. Returns false if not a bot.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_SetBotInput(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    if (!pPlayer || !pPlayer->IsBot())
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const SQVector3D* anglesVec = nullptr;
    SQFloat forwardMove;
    SQFloat sideMove;
    SQInteger buttons;
    SQFloat upMove = 0.0f; // Optional parameter, defaults to 0

    sq_getvector(v, 2, &anglesVec);
    sq_getfloat(v, 3, &forwardMove);
    sq_getfloat(v, 4, &sideMove);
    sq_getinteger(v, 5, &buttons);

    // Optional 6th parameter for upMove (vertical movement)
    if (sq_gettop(v) >= 6)
        sq_getfloat(v, 6, &upMove);

    const int idx = pPlayer->GetEdict() - 1;

    if (idx < 0 || idx >= MAX_PLAYERS)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    g_botInputs[idx].viewAngles.Init(anglesVec->x, anglesVec->y, anglesVec->z);
    g_botInputs[idx].forwardMove = forwardMove;
    g_botInputs[idx].sideMove = sideMove;
    g_botInputs[idx].upMove = upMove;
    g_botInputs[idx].buttons = (int)buttons;
    g_botInputs[idx].hasInput = true;

    sq_pushbool(v, true);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: stops persistent bot input (movement that continues across frames).
// Call this when you want the bot to stop moving between script frames.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_BotStopPersistentInput(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    if (!pPlayer || !pPlayer->IsBot())
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const int idx = pPlayer->GetEdict() - 1;

    if (idx < 0 || idx >= MAX_PLAYERS)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    g_botInputs[idx].hasPersistentInput = false;
    g_botInputs[idx].persistentViewAngles.Init();
    g_botInputs[idx].persistentForwardMove = 0.0f;
    g_botInputs[idx].persistentSideMove = 0.0f;

    sq_pushbool(v, true);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the wall climb setup state for a player
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_IsWallClimbSetUp(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    if (!pPlayer)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    sq_pushbool(v, pPlayer->m_wallClimbSetUp);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: saves a recorded animation on the disk to be used by bakery
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_SaveRecordedAnimation(HSQUIRRELVM v)
{
    if (!developer->GetBool())
    {
        v_SQVM_ScriptError("SaveRecordedAnimation() is dev only!");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    AnimRecordingAssetHeader_s* const animRecording = v_ServerScript_GetRecordedAnimationFromCurrentStack(v);

    if (!animRecording)
    {
        v_SQVM_ScriptError("Parameter must be a recorded animation");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    if (animRecording->numRecordedFrames == 0)
    {
        v_SQVM_ScriptError("Recorded animation has 0 frames");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const SQChar* fileName;
    sq_getstring(v, 3, &fileName);

    char fileNameBuf[MAX_OSPATH];
    const int fmtResult = snprintf(fileNameBuf, sizeof(fileNameBuf), "anim_recording/%s.anir", fileName);

    if (fmtResult < 0)
    {
        v_SQVM_ScriptError("Failed to format recorded animation file name; provided name \"%s\" is invalid", fileName);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    FileSystem()->CreateDirHierarchy("anim_recording/", "MOD");
    FileHandle_t animRecordingFile = FileSystem()->Open(fileNameBuf, "wb", "MOD");

    if (animRecordingFile == FILESYSTEM_INVALID_HANDLE)
    {
        v_SQVM_ScriptError("Failed to open recorded animation file \"%s\" for write", fileNameBuf);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    AnimRecordingFileHeader_s fileHdr;

    fileHdr.magic = ANIR_FILE_MAGIC;
    fileHdr.fileVersion = ANIR_FILE_VERSION;
    fileHdr.assetVersion = ANIR_ASSET_VERSION;

    fileHdr.startPos = animRecording->startPos;
    fileHdr.startAngles = animRecording->startAngles;

    fileHdr.stringBufSize = 0;

    fileHdr.numElements = 0;
    fileHdr.numSequences = 0;

    fileHdr.numRecordedFrames = animRecording->numRecordedFrames;
    fileHdr.numRecordedOverlays = animRecording->numRecordedOverlays;

    fileHdr.animRecordingId = animRecording->animRecordingId;
    FileSystem()->Write(&fileHdr, sizeof(AnimRecordingFileHeader_s), animRecordingFile);

    // This information can only be retrieved by counting the number
    // of valid pose parameter names.
    int numElems = 0;
    int stringBufLen = 0;

    // Write out the pose parameters.
    for (int i = 0; i < ANIR_MAX_ELEMENTS; i++)
    {
        const char* const poseParamName = animRecording->poseParamNames[i];

        if (poseParamName)
            numElems++;
        else
            break;

        const ssize_t strLen = (ssize_t)strlen(poseParamName) + 1; // Include the null too.
        FileSystem()->Write(poseParamName, strLen, animRecordingFile);

        stringBufLen += (int)strLen;
    }

    // Write out the pose values.
    for (int i = 0; i < numElems; i++)
    {
        const Vector2D* poseParamValue = &animRecording->poseParamValues[i];
        FileSystem()->Write(poseParamValue, sizeof(Vector2D), animRecordingFile);
    }

    int numSeqs = 0;

    // Write out the animation sequence names.
    for (int i = 0; i < ANIR_MAX_SEQUENCES; i++)
    {
        const char* const animSequenceName = animRecording->animSequences[i];

        if (animSequenceName)
            numSeqs++;
        else
            break;

        const ssize_t strLen = (ssize_t)strlen(animSequenceName) + 1; // Include the null too.
        FileSystem()->Write(animSequenceName, strLen, animRecordingFile);

        stringBufLen += (int)strLen;
    }

    // Write out the recorded frames.
    for (int i = 0; i < animRecording->numRecordedFrames; i++)
    {
        assert(animRecording->recordedFrames);

        const AnimRecordingFrame_s* const frame = &animRecording->recordedFrames[i];
        FileSystem()->Write(frame, sizeof(AnimRecordingFrame_s), animRecordingFile);
    }

    // Write out the recorded overlays.
    for (int i = 0; i < animRecording->numRecordedOverlays; i++)
    {
        assert(animRecording->recordedOverlays);

        const AnimRecordingOverlay_s* const overlay = &animRecording->recordedOverlays[i];
        FileSystem()->Write(overlay, sizeof(AnimRecordingOverlay_s), animRecordingFile);
    }

    // Update the data in the header if we ended up writing
    // elements and sequences.
    if (numElems > 0 || numSeqs > 0)
    {
        FileSystem()->Seek(animRecordingFile, offsetof(AnimRecordingFileHeader_s, stringBufSize), FILESYSTEM_SEEK_HEAD);

        FileSystem()->Write(&stringBufLen, sizeof(int), animRecordingFile);
        FileSystem()->Write(&numElems, sizeof(int), animRecordingFile);
        FileSystem()->Write(&numSeqs, sizeof(int), animRecordingFile);
    }

    FileSystem()->Close(animRecordingFile);

    Msg(eDLL_T::SERVER, "Recorded animation saved to \"%s\"\n", fileNameBuf);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//---------------------------------------------------------------------------------
// Purpose: registers script functions in SERVER context
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterServerFunctions(CSquirrelVM* s)
{
    Script_RegisterCommonAbstractions(s);
    Script_RegisterCoreServerFunctions(s);
    Script_RegisterAdminServerFunctions(s);

    Script_RegisterLiveAPIFunctions(s);

    // NOTE: plugin functions must always come after SDK functions!
    for (auto& callback : !PluginSystem()->GetRegisterServerScriptFuncsCallbacks())
    {
        // Register script functions inside plugins.
        callback.Function()(s);
    }
}

void Script_RegisterServerEnums(CSquirrelVM* const s)
{
    Script_RegisterLiveAPIEnums(s);
}

//---------------------------------------------------------------------------------
// Purpose: core server script functions
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterCoreServerFunctions(CSquirrelVM* s)
{
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawSolidBox, "Draw a debug overlay solid box", "void", "vector origin, vector mins, vector maxs, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawSweptBox, "Draw a debug overlay swept box", "void", "vector start, vector end, vector mins, vector maxs, vector angles, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawTriangle, "Draw a debug overlay triangle", "void", "vector p1, vector p2, vector p3, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawSolidSphere, "Draw a debug overlay solid sphere", "void", "vector origin, float radius, int theta, int phi, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawCapsule, "Draw a debug overlay capsule", "void", "vector start, vector end, float radius, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, DebugDrawText, "Draw debug overlay text at a world position", "void", "vector origin, string text, bool drawThroughWorld, float duration", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, CreateBox, "Create a permanent box for map making", "void", "vector origin, vector angles, vector mins, vector maxs, vector color, float alpha", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, ClearBoxes, "Clear all debug overlays and boxes", "void", "", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, SetAutoReloadState, "Set whether we can auto-reload the server", "void", "bool canAutoReload", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, GetSessionID, "Gets our current session ID", "string", "", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetNearestPos, "Finds the nearest position to the provided point on the hull's NavMesh using the hull's bounds as extents", "vector ornull", "vector searchPoint, int hullType", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetNearestPosInBounds, "Finds the nearest position to the provided point on the hull's NavMesh using provided bounds as extents", "vector ornull", "vector searchPoint, vector halfExtents, int hullType", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_FindClosestNavmeshPoint, "Finds the closest point on navmesh even when query point is outside polygon bounds. For recovery when off-mesh.", "vector ornull", "vector searchPoint, vector halfExtents, int hullType", false);

    // NavMesh Corridor API
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_CreateCorridor,
        "Creates a path corridor for bot navigation. Returns handle",
        "int",
        "int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_DestroyCorridor,
        "Destroys a path corridor, freeing its memory",
        "void",
        "int handle", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_CorridorSetPath,
        "Sets the path in the corridor from start to target. Returns true on success",
        "bool",
        "int handle, vector startPos, vector targetPos", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_CorridorMove,
        "Updates corridor position as agent moves. Returns true on success",
        "bool",
        "int handle, vector pos", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_CorridorGetCorners,
        "Gets steering corners from corridor. Returns array of {pos, traverseType}",
        "array",
        "int handle, int maxCorners", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetTileTraversePortals,
        "Gets traversal portals (off-mesh connections) for the navmesh tile at position. Returns array of {startPos, endPos, traverseType, radius, refPos, refYaw}",
        "array",
        "vector pos, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, Bot_Create,
        "Creates a bot player, returns edict index (-1 on failure). Use GetEntByIndex() to get entity",
        "int",
        "string name, int team", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, SaveRecordedAnimation, "Saves an anim_recording asset to be used by bakery. (dev only)", "void", "var recordedAnim, string fileName", false);
}

//---------------------------------------------------------------------------------
// Purpose: admin server script functions
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterAdminServerFunctions(CSquirrelVM* s)
{
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, GetNumHumanPlayers, "Gets the number of human players on the server", "int", "", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, GetNumFakeClients, "Gets the number of bot players on the server", "int", "", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, CreateFakePlayer, "Creates a fake player and returns the edict index (-1 on failure). Use GetPlayerArray() to get entity.", "int", "string name, int team", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, KickPlayerByName, "Kicks a player from the server by name", "void", "string name, string reason", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, KickPlayerById, "Kicks a player from the server by handle or Steam ID", "void", "string id, string reason", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, BanPlayerByName, "Bans a player from the server by name", "void", "string name, string reason", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, BanPlayerById, "Bans a player from the server by handle or Steam ID", "void", "string id, string reason", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, UnbanPlayer, "Unbans a player from the server by Steam ID or ip address", "void", "string handle", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, BroadcastServerTextMessage, "Broadcasts a chatmessage to all clients", "void", "string prefix, string message, bool adminMsg", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, BroadcastChatBuilder, "Broadcasts a ChatBuilder message to all clients", "void", "string commands", false);
}

//---------------------------------------------------------------------------------
// Purpose: script code class function registration
//---------------------------------------------------------------------------------
static void Script_RegisterServerEntityClassFuncs()
{
    v_Script_RegisterServerEntityClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerPlayerClassFuncs()
{
    v_Script_RegisterServerPlayerClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;

    g_serverScriptPlayerStruct->AddFunction("SetClassVar",
        "ScriptSetClassVar",
        "Change a variable in the player's class settings",
        "bool",
        "string key, string value",
        false,
        ServerScript_ScriptSetClassVar);

    g_serverScriptPlayerStruct->AddFunction("SendServerTextMessage",
        "ScriptSendServerTextMessage",
        "Sends a chat message to a player",
        "bool",
        "string prefix, string message, bool adminMsg",
        false,
        ServerScript_SendServerTextMessage);

    g_serverScriptPlayerStruct->AddFunction("SetBotInput",
        "ScriptSetBotInput",
        "Sets bot input for this frame. Only works on bot players. Returns false if not a bot",
        "bool",
        "vector viewAngles, float forwardMove, float sideMove, int buttons, float upMove = 0.0",
        false,
        ServerScript_SetBotInput);

    g_serverScriptPlayerStruct->AddFunction("ChatBuilder",
        "ScriptChatBuilder",
        "Advanced chat builder API. Send commands as: 'N|' (newline), 'T|text|' (text), 'C|r,g,b|' (color), 'F|dur,fade|' (fade). Example: 'N|F|5,1|C|255,0,0|T|Red text!|'",
        "bool",
        "string commands",
        false,
        ServerScript_ChatBuilder);

    g_serverScriptPlayerStruct->AddFunction("ChatBuilderRainbow",
        "ScriptChatBuilderRainbow",
        "Sends a rainbow-colored message (cycles through colors per character). Keep text SHORT (under 20 chars recommended)",
        "bool",
        "string text, float duration, float fadeTime",
        false,
        ServerScript_ChatBuilderRainbow);

    g_serverScriptPlayerStruct->AddFunction("BotStopPersistentInput",
        "Script_BotStopPersistentInput",
        "Stops persistent bot movement input. Bot input persists across frames to maintain continuous movement for wall climbing",
        "bool",
        "",
        false,
        ServerScript_BotStopPersistentInput);

    g_serverScriptPlayerStruct->AddFunction("IsWallClimbSetUp",
        "Script_IsWallClimbSetUp",
        "Returns true if the player is set up for wall climbing",
        "bool",
        "",
        false,
        ServerScript_IsWallClimbSetUp);
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerAIClassFuncs()
{
    v_Script_RegisterServerAIClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerWeaponClassFuncs()
{
    v_Script_RegisterServerWeaponClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerProjectileClassFuncs()
{
    v_Script_RegisterServerProjectileClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerTitanSoulClassFuncs()
{
    v_Script_RegisterServerTitanSoulClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerPlayerDecoyClassFuncs()
{
    v_Script_RegisterServerPlayerDecoyClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerSpawnpointClassFuncs()
{
    v_Script_RegisterServerSpawnpointClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterServerFirstPersonProxyClassFuncs()
{
    v_Script_RegisterServerFirstPersonProxyClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}

void VScriptServer::Detour(const bool bAttach) const
{
    DetourSetup(&v_ServerScript_DebugScreenText, &ServerScript_DebugScreenText, bAttach);
    DetourSetup(&v_ServerScript_DebugScreenTextWithColor, &ServerScript_DebugScreenTextWithColor, bAttach);

    DetourSetup(&v_Script_RegisterServerEntityClassFuncs, &Script_RegisterServerEntityClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerPlayerClassFuncs, &Script_RegisterServerPlayerClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerAIClassFuncs, &Script_RegisterServerAIClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerWeaponClassFuncs, &Script_RegisterServerWeaponClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerProjectileClassFuncs, &Script_RegisterServerProjectileClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerTitanSoulClassFuncs, &Script_RegisterServerTitanSoulClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerPlayerDecoyClassFuncs, &Script_RegisterServerPlayerDecoyClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerSpawnpointClassFuncs, &Script_RegisterServerSpawnpointClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterServerFirstPersonProxyClassFuncs, &Script_RegisterServerFirstPersonProxyClassFuncs, bAttach);
}
