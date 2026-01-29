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

    const dtStatus status = query.findNearestPoly(&searchPoint, &halfExtents, &filter, &nearestRef, &nearestPt);

    if (dtStatusFailed(status) || !nearestRef)
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

    const Vector3D& maxs = NAI_Hull::Maxs(hullType);
    outHalfExtents->init(maxs.x, maxs.y, maxs.z);

    return nav;
}

//-----------------------------------------------------------------------------
// Purpose: finds a path between two points on the navmesh.
// Returns a table with 'positions' (array of vectors) and 'traverseTypes'
// (array of ints), or null if no path found. Traverse type 255 means normal
// walking; values 0-31 indicate specific traverse actions (climb, jump, etc).
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_FindPath(HSQUIRRELVM v)
{
    const SQVector3D* startVec = nullptr;
    const SQVector3D* endVec = nullptr;
    SQInteger hullIdx;

    sq_getvector(v, 2, &startVec);
    sq_getvector(v, 3, &endVec);
    sq_getinteger(v, 4, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    const dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    // init() allocates node pools needed by findPath; destructor frees them.
    dtNavMeshQuery query;

    if (dtStatusFailed(query.init(nav, 2048)))
    {
        v_SQVM_ScriptError("Failed to initialize NavMesh query");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const rdVec3D startPos(startVec->x, startVec->y, startVec->z);
    const rdVec3D endPos(endVec->x, endVec->y, endVec->z);

    // Find nearest polys for start and end
    dtPolyRef startRef = 0, endRef = 0;
    rdVec3D startNearest, endNearest;

    query.findNearestPoly(&startPos, &halfExtents, &filter, &startRef, &startNearest);
    query.findNearestPoly(&endPos, &halfExtents, &filter, &endRef, &endNearest);

    if (!startRef || !endRef)
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Compute polygon corridor via A*
    static const int MAX_PATH_POLYS = 256;
    dtPolyRef pathPolys[MAX_PATH_POLYS];
    unsigned char jumpTypes[MAX_PATH_POLYS];
    int pathCount = 0;

    const dtStatus pathStatus = query.findPath(
        startRef, endRef,
        &startNearest, &endNearest, &filter,
        pathPolys, jumpTypes, &pathCount, MAX_PATH_POLYS);

    if (dtStatusFailed(pathStatus) || pathCount == 0)
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Convert polygon corridor to waypoints
    static const int MAX_STRAIGHT_PATH = 256;
    rdVec3D straightPath[MAX_STRAIGHT_PATH];
    unsigned char straightFlags[MAX_STRAIGHT_PATH];
    dtPolyRef straightRefs[MAX_STRAIGHT_PATH];
    unsigned char straightJumps[MAX_STRAIGHT_PATH];
    int straightCount = 0;

    const dtStatus straightStatus = query.findStraightPath(
        &startNearest, &endNearest,
        pathPolys, jumpTypes, pathCount,
        straightPath, straightFlags, straightRefs,
        straightJumps, &straightCount, MAX_STRAIGHT_PATH,
        0xFF, 0);

    if (dtStatusFailed(straightStatus) || straightCount == 0)
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Push result as table with positions and traverseTypes arrays
    sq_newtable(v);

    // Add positions array
    sq_pushstring(v, "positions", -1);
    sq_newarray(v, 0);
    for (int i = 0; i < straightCount; i++)
    {
        const SQVector3D pt(straightPath[i].x, straightPath[i].y, straightPath[i].z);
        sq_pushvector(v, &pt);
        sq_arrayappend(v, -2);
    }
    sq_newslot(v, -3);

    // Add traverseTypes array
    sq_pushstring(v, "traverseTypes", -1);
    sq_newarray(v, 0);
    for (int i = 0; i < straightCount; i++)
    {
        sq_pushinteger(v, straightJumps[i]);
        sq_arrayappend(v, -2);
    }
    sq_newslot(v, -3);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: casts a ray along the navmesh surface.
// Returns the furthest reachable point, or null if start is off the mesh.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_Raycast(HSQUIRRELVM v)
{
    const SQVector3D* startVec = nullptr;
    const SQVector3D* endVec = nullptr;
    SQInteger hullIdx;

    sq_getvector(v, 2, &startVec);
    sq_getvector(v, 3, &endVec);
    sq_getinteger(v, 4, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    const dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    dtNavMeshQuery query;
    query.attachNavMeshUnsafe(nav);

    const rdVec3D startPos(startVec->x, startVec->y, startVec->z);
    const rdVec3D endPos(endVec->x, endVec->y, endVec->z);

    dtPolyRef startRef = 0;
    rdVec3D startNearest;

    query.findNearestPoly(&startPos, &halfExtents, &filter, &startRef, &startNearest);

    if (!startRef)
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Use the simple raycast overload (no node pools required)
    float t = 0.f;
    rdVec3D hitNormal;
    dtPolyRef hitPath[256];
    int hitPathCount = 0;

    const dtStatus status = query.raycast(
        startRef, &startNearest, &endPos, &filter,
        &t, &hitNormal, hitPath, &hitPathCount, 256);

    if (dtStatusFailed(status))
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    SQVector3D result;

    if (t >= 1.0f)
    {
        // Ray reached the end — full line of sight
        result.x = endPos.x;
        result.y = endPos.y;
        result.z = endPos.z;
    }
    else
    {
        // Ray hit something — interpolate to the hit point
        result.x = startNearest.x + (endPos.x - startNearest.x) * t;
        result.y = startNearest.y + (endPos.y - startNearest.y) * t;
        result.z = startNearest.z + (endPos.z - startNearest.z) * t;
    }

    sq_pushvector(v, &result);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: slides along the navmesh surface from start toward end.
// Returns the actual reached position, or null if start is off the mesh.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_MoveAlongSurface(HSQUIRRELVM v)
{
    const SQVector3D* startVec = nullptr;
    const SQVector3D* endVec = nullptr;
    SQInteger hullIdx;

    sq_getvector(v, 2, &startVec);
    sq_getvector(v, 3, &endVec);
    sq_getinteger(v, 4, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    const dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    // init() allocates tinyNodePool needed by moveAlongSurface.
    dtNavMeshQuery query;

    if (dtStatusFailed(query.init(nav, 2048)))
    {
        v_SQVM_ScriptError("Failed to initialize NavMesh query");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const rdVec3D startPos(startVec->x, startVec->y, startVec->z);
    const rdVec3D endPos(endVec->x, endVec->y, endVec->z);

    dtPolyRef startRef = 0;
    rdVec3D startNearest;

    query.findNearestPoly(&startPos, &halfExtents, &filter, &startRef, &startNearest);

    if (!startRef)
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    rdVec3D resultPos;
    dtPolyRef visited[64];
    int visitedCount = 0;

    const dtStatus status = query.moveAlongSurface(
        startRef, &startNearest, &endPos, &filter,
        &resultPos, visited, &visitedCount, 64, 0);

    if (dtStatusFailed(status))
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    const SQVector3D result(resultPos.x, resultPos.y, resultPos.z);
    sq_pushvector(v, &result);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the navmesh surface height at a given XY position.
// Returns -1.0 if the position is not over any navmesh polygon.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetPolyHeight(HSQUIRRELVM v)
{
    const SQVector3D* posVec = nullptr;
    SQInteger hullIdx;

    sq_getvector(v, 2, &posVec);
    sq_getinteger(v, 3, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    const dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    dtNavMeshQuery query;
    query.attachNavMeshUnsafe(nav);

    const rdVec3D pos(posVec->x, posVec->y, posVec->z);

    dtPolyRef nearestRef = 0;
    rdVec3D nearestPt;

    query.findNearestPoly(&pos, &halfExtents, &filter, &nearestRef, &nearestPt);

    if (!nearestRef)
    {
        sq_pushfloat(v, -1.0f);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    float height = 0.f;

    const dtStatus status = query.getPolyHeight(nearestRef, &nearestPt, &height);

    if (dtStatusFailed(status))
    {
        sq_pushfloat(v, -1.0f);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    sq_pushfloat(v, height);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: checks if a goal position is reachable from a start position
// using the navmesh traverse tables. Much cheaper than computing a full path.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_IsGoalReachable(HSQUIRRELVM v)
{
    const SQVector3D* startVec = nullptr;
    const SQVector3D* endVec = nullptr;
    SQInteger hullIdx;

    sq_getvector(v, 2, &startVec);
    sq_getvector(v, 3, &endVec);
    sq_getinteger(v, 4, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    // findNearestPoly only needs m_nav, so attachNavMeshUnsafe is fine.
    dtNavMeshQuery query;
    query.attachNavMeshUnsafe(nav);

    const rdVec3D startPos(startVec->x, startVec->y, startVec->z);
    const rdVec3D endPos(endVec->x, endVec->y, endVec->z);

    dtPolyRef startRef = 0, endRef = 0;
    rdVec3D startNearest, endNearest;

    query.findNearestPoly(&startPos, &halfExtents, &filter, &startRef, &startNearest);
    query.findNearestPoly(&endPos, &halfExtents, &filter, &endRef, &endNearest);

    if (!startRef || !endRef)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // v_Detour_IsGoalPolyReachable is a standalone function (not a class method).
    // It takes dtNavMesh* directly and handles traverse table indexing internally.
    // Default to ANIMTYPE_HUMAN for player-like bots.
    const bool reachable = v_Detour_IsGoalPolyReachable(
        nav, startRef, endRef, ANIMTYPE_HUMAN);

    sq_pushbool(v, reachable);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: finds distance to nearest wall/boundary from a position.
// Returns a table with 'distance', 'hitPos', and 'hitNormal', or null if
// the position is off the navmesh.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetWallDistance(HSQUIRRELVM v)
{
    const SQVector3D* posVec = nullptr;
    SQFloat maxRadius;
    SQInteger hullIdx;

    sq_getvector(v, 2, &posVec);
    sq_getfloat(v, 3, &maxRadius);
    sq_getinteger(v, 4, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    const dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    // findDistanceToWall requires node pools, so must use init()
    dtNavMeshQuery query;

    if (dtStatusFailed(query.init(nav, 512)))
    {
        v_SQVM_ScriptError("Failed to initialize NavMesh query");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const rdVec3D pos(posVec->x, posVec->y, posVec->z);

    dtPolyRef nearestRef = 0;
    rdVec3D nearestPt;

    query.findNearestPoly(&pos, &halfExtents, &filter, &nearestRef, &nearestPt);

    if (!nearestRef)
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    float hitDist = 0.f;
    rdVec3D hitPos, hitNormal;

    const dtStatus status = query.findDistanceToWall(
        nearestRef, &nearestPt, maxRadius, &filter,
        &hitDist, &hitPos, &hitNormal);

    if (dtStatusFailed(status))
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Return table: { distance, hitPos, hitNormal }
    sq_newtable(v);

    sq_pushstring(v, "distance", -1);
    sq_pushfloat(v, hitDist);
    sq_newslot(v, -3);

    sq_pushstring(v, "hitPos", -1);
    const SQVector3D sqHitPos(hitPos.x, hitPos.y, hitPos.z);
    sq_pushvector(v, &sqHitPos);
    sq_newslot(v, -3);

    sq_pushstring(v, "hitNormal", -1);
    const SQVector3D sqHitNormal(hitNormal.x, hitNormal.y, hitNormal.z);
    sq_pushvector(v, &sqHitNormal);
    sq_newslot(v, -3);

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

    sq_getvector(v, 2, &anglesVec);
    sq_getfloat(v, 3, &forwardMove);
    sq_getfloat(v, 4, &sideMove);
    sq_getinteger(v, 5, &buttons);

    const int idx = pPlayer->GetEdict() - 1;

    if (idx < 0 || idx >= MAX_PLAYERS)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    g_botInputs[idx].viewAngles.Init(anglesVec->x, anglesVec->y, anglesVec->z);
    g_botInputs[idx].forwardMove = forwardMove;
    g_botInputs[idx].sideMove = sideMove;
    g_botInputs[idx].buttons = (int)buttons;
    g_botInputs[idx].hasInput = true;

    sq_pushbool(v, true);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: persistently forces a bot button input (e.g. IN_DUCK, IN_FORWARD).
// Stays active until BotButtonRelease is called for the same button.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_BotButtonPress(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    if (!pPlayer || !pPlayer->IsBot())
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    SQInteger button;
    sq_getinteger(v, 2, &button);

    const int idx = pPlayer->GetEdict() - 1;

    if (idx < 0 || idx >= MAX_PLAYERS)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    g_botInputs[idx].forcedButtons |= (int)button;

    sq_pushbool(v, true);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: releases a persistently forced bot button input.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_BotButtonRelease(HSQUIRRELVM v)
{
    CPlayer* pPlayer = nullptr;

    if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
        return SQ_ERROR;

    if (!pPlayer || !pPlayer->IsBot())
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    SQInteger button;
    sq_getinteger(v, 2, &button);

    const int idx = pPlayer->GetEdict() - 1;

    if (idx < 0 || idx >= MAX_PLAYERS)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    g_botInputs[idx].forcedButtons &= ~(int)button;

    sq_pushbool(v, true);
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

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, CreateBox, "Create a permanent box for map making", "void", "vector origin, vector angles, vector mins, vector maxs, vector color, float alpha", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, ClearBoxes, "Clear all debug overlays and boxes", "void", "", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, SetAutoReloadState, "Set whether we can auto-reload the server", "void", "bool canAutoReload", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, GetSessionID, "Gets our current session ID", "string", "", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetNearestPos, "Finds the nearest position to the provided point on the hull's NavMesh using the hull's bounds as extents", "vector ornull", "vector searchPoint, int hullType", false);
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetNearestPosInBounds, "Finds the nearest position to the provided point on the hull's NavMesh using provided bounds as extents", "vector ornull", "vector searchPoint, vector halfExtents, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_FindPath,
        "Finds a path between two points. Returns table with 'positions' (array of vectors) and 'traverseTypes' (array of ints where 255=walk, 0-31=traverse action), or null if no path",
        "table ornull",
        "vector startPos, vector endPos, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_Raycast,
        "Casts a ray along the navmesh, returns furthest reachable point",
        "vector ornull",
        "vector startPos, vector endPos, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_MoveAlongSurface,
        "Slides along the navmesh surface toward a target position",
        "vector ornull",
        "vector startPos, vector endPos, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetPolyHeight,
        "Gets the navmesh surface height at an XY position (-1 if off mesh)",
        "float",
        "vector pos, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_IsGoalReachable,
        "Checks if a goal is reachable from start using traverse tables",
        "bool",
        "vector startPos, vector endPos, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetWallDistance,
        "Finds distance to nearest wall within radius. Returns {distance, hitPos, hitNormal} or null",
        "table ornull",
        "vector pos, float maxRadius, int hullType", false);

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
        "vector viewAngles, float forwardMove, float sideMove, int buttons",
        false,
        ServerScript_SetBotInput);

    g_serverScriptPlayerStruct->AddFunction("BotButtonPress",
        "Script_BotButtonPress",
        "Forces a bot player to activate an input (such as IN_ATTACK). Stays active until BotButtonRelease",
        "bool",
        "int button",
        false,
        ServerScript_BotButtonPress);

    g_serverScriptPlayerStruct->AddFunction("BotButtonRelease",
        "Script_BotButtonRelease",
        "Deactivates a forced bot input (such as IN_ATTACK)",
        "bool",
        "int button",
        false,
        ServerScript_BotButtonRelease);

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
