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
#include "vscript/languages/squirrel_re/include/sqarray.h"

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
#include "public/game/server/ai_navmesh.h" // NavMesh_GetTraverseTableIndexForAnimType
#include "engine/enginetrace.h"            // g_pEngineTraceServer (cover query LOS traces)
#include "public/bspflags.h"               // TRACE_MASK_BLOCKLOS
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>

//=============================================================================
// Bot Navigation Corridor System
//=============================================================================
// Corridor poly buffer size - must match dtPathCorridor::init() arg below.
static const int BOT_CORRIDOR_MAX_PATH = 256;

// Lazy refill threshold: when corridor poly count drops to this, run the next
// chained A* to extend the trip toward the goal.
static const int BOT_CORRIDOR_REFILL_THRESHOLD = 32;

struct BotCorridor
{
    dtPathCorridor corridor;
    dtNavMeshQuery query;
    dtQueryFilter filter;
    Hull_e hullType;
    bool initialized;

    // Trip state - lets a long path be served by chained A* calls. SetPath does
    // the first one; CorridorMove triggers further ones lazily as the bot walks
    // and the corridor depletes. The bot script doesn't see any of this.
    rdVec3D goalPos;             // original brain goal, snapped to navmesh
    dtPolyRef goalRef;           // goal poly ref
    dtPolyRef nextStartRef;      // partial endpoint poly to start the next A* from
    rdVec3D nextStartPos;        // partial endpoint position (A* startPos arg)
    bool tripActive;             // a trip is in flight (set by SetPath)
    bool tripComplete;           // last A* returned non-partial - no more refills

    // Pending poly stash - polys produced by the latest A* refill, waiting to be
    // dripped into the corridor's tail as the front consumes. Boundary-deduped:
    // the duplicate first poly (matches corridor's current last poly) is skipped.
    dtPolyRef pendingPolys[BOT_CORRIDOR_MAX_PATH];
    unsigned char pendingJumps[BOT_CORRIDOR_MAX_PATH];
    int pendingCount;

    BotCorridor()
        : hullType(HULL_HUMAN)
        , initialized(false)
        , goalRef(0)
        , nextStartRef(0)
        , tripActive(false)
        , tripComplete(false)
        , pendingCount(0)
    {}
};

static std::unordered_map<int, BotCorridor*> g_corridors;
static int g_nextCorridorHandle = 1;

// Banned traverse links: stores original traverseType so links can be restored.
// Key: (sourcePolyRef << 32 | targetPolyRef), Value: original traverseType byte
static std::unordered_map<uint64_t, unsigned char> g_bannedTraverseLinks;

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

//-----------------------------------------------------------------------------
// Purpose: parses either an integer traverse type or array<int> of types.
//-----------------------------------------------------------------------------
static bool Internal_ServerScript_ParseTraverseTypesArg(
    HSQUIRRELVM v,
    const SQInteger argIdx,
    std::vector<int>& outTypes)
{
    outTypes.clear();

    HSQOBJECT obj{};
    if (SQ_FAILED(sq_getstackobj(v, argIdx, &obj)))
    {
        v_SQVM_ScriptError("Failed to read traverse type argument at index %d", argIdx);
        return false;
    }

    const SQObjectType type = sq_type(obj);
    if (type == OT_INTEGER)
    {
        SQInteger traverseType = 0;
        sq_getinteger(v, argIdx, &traverseType);
        outTypes.push_back((int)traverseType);
    }
    else if (type == OT_ARRAY)
    {
        const SQArray* arr = _array(obj);
        if (!arr)
        {
            v_SQVM_ScriptError("Traverse types argument is an invalid array");
            return false;
        }

        for (SQInteger i = 0; i < arr->Size(); ++i)
        {
            const SQObject& item = arr->_values[i];
            if (sq_isnull(item))
                continue;

            if (!sq_isinteger(item))
            {
                v_SQVM_ScriptError("Traverse types array must contain only integers");
                return false;
            }

            outTypes.push_back((int)_integer(item));
        }
    }
    else
    {
        v_SQVM_ScriptError("Traverse types must be an integer or array of integers");
        return false;
    }

    for (const int traverseType : outTypes)
    {
        if (traverseType < 0 || traverseType >= DT_MAX_TRAVERSE_TYPES)
        {
            v_SQVM_ScriptError("Traverse type %d out of range [0, %d)", traverseType, DT_MAX_TRAVERSE_TYPES);
            return false;
        }
    }

    return true;
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
    if (!pCorridor->corridor.init(BOT_CORRIDOR_MAX_PATH))
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

    it->second->filter.resetTraverseAngleChecks();
    delete it->second;
    g_corridors.erase(it);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: enables edge-angle filtering for one traverse type or array<int>.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_CorridorSetAngleFilter(HSQUIRRELVM v)
{
    SQInteger handle = -1;
    SQFloat maxAngleDeg = 0.0f;
    sq_getinteger(v, 2, &handle);
    sq_getfloat(v, 4, &maxAngleDeg);
    if (maxAngleDeg < 0.0f) maxAngleDeg = 0.0f;
    if (maxAngleDeg > 180.0f) maxAngleDeg = 180.0f;

    auto it = g_corridors.find((int)handle);
    if (it == g_corridors.end())
    {
        v_SQVM_ScriptError("NavMesh_CorridorSetAngleFilter: invalid corridor handle %d", handle);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    BotCorridor* pCorridor = it->second;
    if (!pCorridor->initialized)
    {
        v_SQVM_ScriptError("NavMesh_CorridorSetAngleFilter: corridor not initialized");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    std::vector<int> traverseTypes;
    if (!Internal_ServerScript_ParseTraverseTypesArg(v, 3, traverseTypes))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    for (const int traverseType : traverseTypes)
    {
        pCorridor->filter.setTraverseMaxAngleDeg(traverseType, (float)maxAngleDeg);
        pCorridor->filter.setTraverseIgnoreAngle(traverseType, false);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: disables edge-angle filtering for one traverse type or array<int>.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_CorridorClearAngleFilter(HSQUIRRELVM v)
{
    SQInteger handle = -1;
    sq_getinteger(v, 2, &handle);

    auto it = g_corridors.find((int)handle);
    if (it == g_corridors.end())
    {
        v_SQVM_ScriptError("NavMesh_CorridorClearAngleFilter: invalid corridor handle %d", handle);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    BotCorridor* pCorridor = it->second;
    if (!pCorridor->initialized)
    {
        v_SQVM_ScriptError("NavMesh_CorridorClearAngleFilter: corridor not initialized");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    std::vector<int> traverseTypes;
    if (!Internal_ServerScript_ParseTraverseTypesArg(v, 3, traverseTypes))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    for (const int traverseType : traverseTypes)
        pCorridor->filter.setTraverseIgnoreAngle(traverseType, true);

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

    // Reset trip state at the top - if we fail somewhere below, the corridor
    // ends up with no trip in flight, matching the "no path" scripts expect.
    pCorridor->tripActive = false;
    pCorridor->tripComplete = false;
    pCorridor->pendingCount = 0;

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

    // Static reachability check (precomputed traverse table). ANIMTYPE_PILOT
    // mirrors the corridor filter (setTraverseFlags(0x8013F)). If the table
    // says these polys aren't connected under PILOT's traverse-type set, no
    // amount of A* will find a path - fail immediately.
    const dtNavMesh* const nav = pCorridor->query.getAttachedNavMesh();
    const int traverseTableIndex = NavMesh_GetTraverseTableIndexForAnimType(ANIMTYPE_PILOT);
    if (!nav->isGoalPolyReachable(startRef, targetRef, false, traverseTableIndex))
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Find path
    dtPolyRef pathPolys[BOT_CORRIDOR_MAX_PATH];
    unsigned char pathJumps[BOT_CORRIDOR_MAX_PATH];
    int pathCount = 0;

    dtStatus status = pCorridor->query.findPath(
        startRef, targetRef,
        &nearestStart, &nearestTarget, &pCorridor->filter,
        pathPolys, pathJumps, &pathCount, BOT_CORRIDOR_MAX_PATH);

    if (dtStatusFailed(status) || pathCount == 0)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Buffer too small: getPathToNode truncates from the front, so the returned
    // path doesn't start where the agent is - unusable.
    if (status & DT_BUFFER_TOO_SMALL)
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Reachability above guarantees a full path exists. Partial here means A*
    // exhausted its node pool (DT_OUT_OF_NODES) - the returned path is the best
    // frontier toward the goal, treated as an intermediate destination. Partial
    // WITHOUT DT_OUT_OF_NODES means the filter rejected something the table
    // allowed (angle filtering, runtime poly flag changes) - fail loudly.
    const bool isPartial = (status & DT_PARTIAL_RESULT) != 0;
    if (isPartial && !(status & DT_OUT_OF_NODES))
    {
        sq_pushbool(v, false);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // For partial paths, snap the corridor's target to the path's last poly so
    // findCorners has a real, on-corridor destination to steer toward (the
    // original goal would be off the corridor and meaningless to findCorners).
    rdVec3D corridorTarget = nearestTarget;
    if (isPartial)
    {
        rdVec3D lastPolyPos;
        float distUnused;
        if (dtStatusSucceed(pCorridor->query.closestPointOnPolyBoundary(pathPolys[pathCount - 1], &nearestTarget, &lastPolyPos, &distUnused)))
            corridorTarget = lastPolyPos;
    }

    pCorridor->corridor.setCorridor(&corridorTarget, pathPolys, pathJumps, pathCount);
    pCorridor->corridor.setPos(&nearestStart);

    // Initialize trip state so CorridorMove can chain further A* calls if this
    // first segment was partial. For complete paths, tripComplete=true short-
    // circuits any future refill attempts.
    pCorridor->tripActive = true;
    pCorridor->goalPos = nearestTarget;
    pCorridor->goalRef = targetRef;

    if (isPartial)
    {
        pCorridor->tripComplete = false;
        pCorridor->nextStartRef = pathPolys[pathCount - 1];
        pCorridor->nextStartPos = corridorTarget;
    }
    else
    {
        pCorridor->tripComplete = true;
    }

    sq_pushbool(v, true);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Trip extension helpers. Internal_Trip_Refill runs the next chained A* from
// the latest partial endpoint toward the original brain goal, stashing the
// result in pending. Internal_Trip_Drip moves polys from pending into the
// corridor's tail as space appears. Both are no-ops when there's nothing to do.
//-----------------------------------------------------------------------------
static void Internal_Trip_Refill(BotCorridor* pCorridor)
{
    dtPolyRef pathPolys[BOT_CORRIDOR_MAX_PATH];
    unsigned char pathJumps[BOT_CORRIDOR_MAX_PATH];
    int pathCount = 0;

    const dtStatus status = pCorridor->query.findPath(
        pCorridor->nextStartRef, pCorridor->goalRef,
        &pCorridor->nextStartPos, &pCorridor->goalPos, &pCorridor->filter,
        pathPolys, pathJumps, &pathCount, BOT_CORRIDOR_MAX_PATH);

    // Any failure mode means we can't extend the trip further. Mark complete so
    // we stop trying; whatever's already in the corridor gets walked, then the
    // bot's BT will request a new path from current position when it stalls.
    if (dtStatusFailed(status) || pathCount == 0 || (status & DT_BUFFER_TOO_SMALL))
    {
        pCorridor->tripComplete = true;
        return;
    }

    // Partial without DT_OUT_OF_NODES means the filter rejected something the
    // reachability table allowed (angle filtering, runtime poly flag changes
    // since SetPath). Bail loudly - the static-navmesh assumption is broken.
    if ((status & DT_PARTIAL_RESULT) && !(status & DT_OUT_OF_NODES))
    {
        pCorridor->tripComplete = true;
        return;
    }

    // Boundary dedup: pathPolys[0] is nextStartRef, which already lives in the
    // corridor (or is being held there pending drip-in). Skip it.
    const int toStash = pathCount - 1;
    if (toStash <= 0)
    {
        // A* returned just the start poly (start == goal case). Nothing to add.
        pCorridor->tripComplete = true;
        return;
    }

    memcpy(pCorridor->pendingPolys, pathPolys + 1, sizeof(dtPolyRef) * toStash);
    memcpy(pCorridor->pendingJumps, pathJumps + 1, sizeof(unsigned char) * toStash);
    pCorridor->pendingCount = toStash;

    // The new partial endpoint becomes the start of the next refill (if any).
    pCorridor->nextStartRef = pathPolys[pathCount - 1];
    rdVec3D newStartPos;
    if (dtStatusSucceed(pCorridor->query.closestPointOnPolyBoundary(pCorridor->nextStartRef, &pCorridor->goalPos, &newStartPos, nullptr)))
        pCorridor->nextStartPos = newStartPos;

    // Complete path: this segment ends at the goal poly. Stop refilling.
    if (!(status & DT_PARTIAL_RESULT))
        pCorridor->tripComplete = true;
}

static void Internal_Trip_Drip(BotCorridor* pCorridor)
{
    if (pCorridor->pendingCount <= 0)
        return;

    const dtPolyRef* curPath = pCorridor->corridor.getPath();
    const unsigned char* curJumps = pCorridor->corridor.getJump();
    const int curCount = pCorridor->corridor.getPathCount();

    const int freeSlots = BOT_CORRIDOR_MAX_PATH - curCount;
    if (freeSlots <= 0)
        return;

    const int toAppend = (pCorridor->pendingCount < freeSlots) ? pCorridor->pendingCount : freeSlots;

    dtPolyRef mergedPolys[BOT_CORRIDOR_MAX_PATH];
    unsigned char mergedJumps[BOT_CORRIDOR_MAX_PATH];

    memcpy(mergedPolys, curPath, sizeof(dtPolyRef) * curCount);
    memcpy(mergedJumps, curJumps, sizeof(unsigned char) * curCount);
    memcpy(mergedPolys + curCount, pCorridor->pendingPolys, sizeof(dtPolyRef) * toAppend);
    memcpy(mergedJumps + curCount, pCorridor->pendingJumps, sizeof(unsigned char) * toAppend);

    const int newCount = curCount + toAppend;
    const dtPolyRef newLastPoly = mergedPolys[newCount - 1];

    // Update target to the new last poly's projection toward the goal. For an
    // intermediate partial-endpoint poly this is the snap to the boundary; for
    // the goal poly itself the projection is the goal position (or close to).
    rdVec3D newTarget = pCorridor->goalPos;
    rdVec3D snapped;
    if (dtStatusSucceed(pCorridor->query.closestPointOnPolyBoundary(newLastPoly, &pCorridor->goalPos, &snapped, nullptr)))
        newTarget = snapped;

    pCorridor->corridor.setCorridor(&newTarget, mergedPolys, mergedJumps, newCount);

    // Shift remaining pending polys down.
    const int remaining = pCorridor->pendingCount - toAppend;
    if (remaining > 0)
    {
        memmove(pCorridor->pendingPolys, pCorridor->pendingPolys + toAppend, sizeof(dtPolyRef) * remaining);
        memmove(pCorridor->pendingJumps, pCorridor->pendingJumps + toAppend, sizeof(unsigned char) * remaining);
    }
    pCorridor->pendingCount = remaining;
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

    // Trip extension only when movePosition succeeded (corridor state is sane).
    // Drip first to clear any pending stash into freed slots, then maybe refill
    // pending if it ran dry and the corridor needs more polys.
    if (success && pCorridor->tripActive)
    {
        Internal_Trip_Drip(pCorridor);

        if (pCorridor->pendingCount == 0
            && !pCorridor->tripComplete
            && pCorridor->corridor.getPathCount() <= BOT_CORRIDOR_REFILL_THRESHOLD)
        {
            Internal_Trip_Refill(pCorridor);
        }
    }

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

    // Check if corridor has a valid path
    if (pCorridor->corridor.getPathCount() <= 0)
    {
        // Return empty array if no path exists
        sq_newarray(v, 0);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
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

    // Get the corridor path to find target polygons
    const dtPolyRef* pathPolys = pCorridor->corridor.getPath();
    const int pathCount = pCorridor->corridor.getPathCount();

    // Get the navmesh for portal queries
    const dtNavMesh* nav = pCorridor->query.getAttachedNavMesh();

    // Create array of {pos, traverseType, polyRef, targetPolyRef, portal data...} tables
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

        sq_pushstring(v, "polyRef", -1);
        sq_pushinteger(v, (SQInteger)cornerPolys[i]);
        sq_newslot(v, -3);

        // Find the next polygon in the path for this corner
        // Search through the path to find where this corner's polygon is
        dtPolyRef targetPolyRef = 0;
        for (int j = 0; j < pathCount - 1; j++)
        {
            if (pathPolys[j] == cornerPolys[i])
            {
                // Found this corner's polygon, next one is the target
                targetPolyRef = pathPolys[j + 1];
                break;
            }
        }

        sq_pushstring(v, "targetPolyRef", -1);
        sq_pushinteger(v, (SQInteger)targetPolyRef);
        sq_newslot(v, -3);

        // If this corner has a traverse type, get portal data
        const int traverseType = cornerJumps[i] & (DT_MAX_TRAVERSE_TYPES - 1);
        if (traverseType != 0)
        {
            // Get tile and poly for this corner
            const dtMeshTile* tile = nullptr;
            const dtPoly* poly = nullptr;
            nav->getTileAndPolyByRefUnsafe(cornerPolys[i], &tile, &poly);

            if (tile && poly)
            {
                // Special handling if corner is ON an off-mesh connection (zipline)
                if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                {
                    const dtOffMeshConnection* con = nav->getOffMeshConnectionByRef(cornerPolys[i]);
                    if (con)
                    {
                        // For off-mesh, vertices are the connection endpoints
                        // Use them directly as portal data
                        sq_pushstring(v, "portalVertA", -1);
                        const SQVector3D sqVertA(con->posa.x, con->posa.y, con->posa.z);
                        sq_pushvector(v, &sqVertA);
                        sq_newslot(v, -3);

                        sq_pushstring(v, "portalVertB", -1);
                        const SQVector3D sqVertB(con->posb.x, con->posb.y, con->posb.z);
                        sq_pushvector(v, &sqVertB);
                        sq_newslot(v, -3);

                        // Determine direction based on path
                        rdVec3D startPos = con->posa;
                        rdVec3D endPos = con->posb;

                        // If we have a previous polygon in path, use it to determine direction
                        for (int j = 0; j < pathCount - 1; j++)
                        {
                            if (pathPolys[j + 1] == cornerPolys[i] && j >= 0)
                            {
                                // Found this off-mesh in path, get previous polygon
                                const dtMeshTile* prevTile = nullptr;
                                const dtPoly* prevPoly = nullptr;
                                nav->getTileAndPolyByRefUnsafe(pathPolys[j], &prevTile, &prevPoly);

                                if (prevPoly)
                                {
                                    // Pick endpoint closer to previous polygon as start
                                    const float distA = rdVdistSqr(&con->posa, &prevPoly->center);
                                    const float distB = rdVdistSqr(&con->posb, &prevPoly->center);
                                    if (distA < distB)
                                    {
                                        startPos = con->posa;
                                        endPos = con->posb;
                                    }
                                    else
                                    {
                                        startPos = con->posb;
                                        endPos = con->posa;
                                    }
                                }
                                break;
                            }
                        }

                        sq_pushstring(v, "portalStart", -1);
                        const SQVector3D sqStartPos(startPos.x, startPos.y, startPos.z);
                        sq_pushvector(v, &sqStartPos);
                        sq_newslot(v, -3);

                        sq_pushstring(v, "portalEnd", -1);
                        const SQVector3D sqEndPos(endPos.x, endPos.y, endPos.z);
                        sq_pushvector(v, &sqEndPos);
                        sq_newslot(v, -3);
                    }
                }
                else if (targetPolyRef != 0)
                {
                    // Regular polygon with traverse link - find the link to targetPolyRef
                    unsigned int linkIdx = poly->firstLink;
                    while (linkIdx != DT_NULL_LINK)
                    {
                        const dtLink* link = &tile->links[linkIdx];

                        if (link->ref == targetPolyRef && link->hasTraverseType())
                        {
                            // Found the portal link - extract geometry
                            const unsigned char edgeIdx = link->edge;
                            const unsigned short va = poly->verts[edgeIdx];
                            const unsigned short vb = poly->verts[(edgeIdx + 1) % poly->vertCount];

                            const rdVec3D* vertA = &tile->verts[va];
                            const rdVec3D* vertB = &tile->verts[vb];

                            // Calculate edge midpoint
                            rdVec3D edgeMid;
                            edgeMid.x = (vertA->x + vertB->x) * 0.5f;
                            edgeMid.y = (vertA->y + vertB->y) * 0.5f;
                            edgeMid.z = (vertA->z + vertB->z) * 0.5f;

                            // Get target polygon for endPos
                            const dtMeshTile* targetTile = nullptr;
                            const dtPoly* targetPoly = nullptr;
                            nav->getTileAndPolyByRefUnsafe(targetPolyRef, &targetTile, &targetPoly);

                            rdVec3D startPos = edgeMid;
                            rdVec3D endPos = edgeMid;

                            if (targetTile && targetPoly)
                            {
                                if (targetPoly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION && targetPoly->vertCount == 2)
                                {
                                    // Off-mesh connection - get both endpoints
                                    const rdVec3D* v0 = &targetTile->verts[targetPoly->verts[0]];
                                    const rdVec3D* v1 = &targetTile->verts[targetPoly->verts[1]];

                                    const float dist0 = rdVdistSqr(v0, &poly->center);
                                    const float dist1 = rdVdistSqr(v1, &poly->center);
                                    if (dist0 < dist1)
                                    {
                                        startPos = *v0;
                                        endPos = *v1;
                                    }
                                    else
                                    {
                                        startPos = *v1;
                                        endPos = *v0;
                                    }
                                }
                                else
                                {
                                    // Regular polygon - use center
                                    endPos = targetPoly->center;
                                }
                            }

                            // Add portal data to corner table
                            sq_pushstring(v, "portalVertA", -1);
                            const SQVector3D sqVertA(vertA->x, vertA->y, vertA->z);
                            sq_pushvector(v, &sqVertA);
                            sq_newslot(v, -3);

                            sq_pushstring(v, "portalVertB", -1);
                            const SQVector3D sqVertB(vertB->x, vertB->y, vertB->z);
                            sq_pushvector(v, &sqVertB);
                            sq_newslot(v, -3);

                            sq_pushstring(v, "portalStart", -1);
                            const SQVector3D sqStartPos(startPos.x, startPos.y, startPos.z);
                            sq_pushvector(v, &sqStartPos);
                            sq_newslot(v, -3);

                            sq_pushstring(v, "portalEnd", -1);
                            const SQVector3D sqEndPos(endPos.x, endPos.y, endPos.z);
                            sq_pushvector(v, &sqEndPos);
                            sq_newslot(v, -3);

                            break; // Found the link, done
                        }

                        linkIdx = link->next;
                    }
                }
            }
        }

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

//-----------------------------------------------------------------------------
// Purpose: gets traversal links for a specific polygon reference.
// Returns array of {startPos, endPos, traverseType, traverseDist}.
// Query portals directly from a known polygon (from corridor corners).
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetPortalsFromPoly(HSQUIRRELVM v)
{
    SQInteger polyRefInt;
    SQInteger hullIdx;

    sq_getinteger(v, 2, &polyRefInt);
    sq_getinteger(v, 3, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const dtPolyRef polyRef = (dtPolyRef)polyRefInt;

    if (!polyRef)
    {
        sq_newarray(v, 0);
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Get the tile and polygon directly from the reference
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

    // Special handling for off-mesh connections (ziplines, etc.)
    // These polygons ARE the traversal, not links to a traversal
    if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
    {
        const dtOffMeshConnection* con = nav->getOffMeshConnectionByRef(polyRef);
        if (!con)
        {
            SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
        }

        // Get the actual endpoints from the connection data
        // Note: Need to cast away const to call non-const methods
        dtOffMeshConnection* conNonConst = const_cast<dtOffMeshConnection*>(con);

        sq_newtable(v);

        // Return posa, posb, refPos, and vertOrder - let script determine direction
        sq_pushstring(v, "posa", -1);
        const SQVector3D sqPosA(con->posa.x, con->posa.y, con->posa.z);
        sq_pushvector(v, &sqPosA);
        sq_newslot(v, -3);

        sq_pushstring(v, "posb", -1);
        const SQVector3D sqPosB(con->posb.x, con->posb.y, con->posb.z);
        sq_pushvector(v, &sqPosB);
        sq_newslot(v, -3);

        sq_pushstring(v, "refPos", -1);
        const SQVector3D sqRefPos(con->refPos.x, con->refPos.y, con->refPos.z);
        sq_pushvector(v, &sqRefPos);
        sq_newslot(v, -3);

        sq_pushstring(v, "vertOrder", -1);
        sq_pushinteger(v, (SQInteger)conNonConst->getVertLookupOrder());
        sq_newslot(v, -3);

        sq_pushstring(v, "traverseType", -1);
        sq_pushinteger(v, (SQInteger)conNonConst->getTraverseType());
        sq_newslot(v, -3);

        sq_pushstring(v, "traverseDist", -1);
        sq_pushfloat(v, rdVdist(&con->posa, &con->posb));
        sq_newslot(v, -3);

        sq_arrayappend(v, -2);

        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Follow the link chain for this specific polygon
    unsigned int linkIdx = poly->firstLink;
    int linkCount = 0;
    int traverseLinkCount = 0;
    while (linkIdx != DT_NULL_LINK)
    {
        const dtLink* link = &tile->links[linkIdx];
        linkCount++;

        // Check if this link has a traverse type (not normal walking)
        if (link->hasTraverseType())
        {
            traverseLinkCount++;
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

            rdVec3D startPos = edgeMid;
            rdVec3D targetPos = edgeMid;
            if (targetTile && targetPoly)
            {
                if (targetPoly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION && targetPoly->vertCount == 2)
                {
                    const rdVec3D* v0 = &targetTile->verts[targetPoly->verts[0]];
                    const rdVec3D* v1 = &targetTile->verts[targetPoly->verts[1]];

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
                    targetPos = targetPoly->center;
                }
            }

            sq_newtable(v);

            sq_pushstring(v, "startPos", -1);
            const SQVector3D sqStartPos(startPos.x, startPos.y, startPos.z);
            sq_pushvector(v, &sqStartPos);
            sq_newslot(v, -3);

            sq_pushstring(v, "endPos", -1);
            const SQVector3D sqEndPos(targetPos.x, targetPos.y, targetPos.z);
            sq_pushvector(v, &sqEndPos);
            sq_newslot(v, -3);

            sq_pushstring(v, "traverseType", -1);
            sq_pushinteger(v, link->getTraverseType());
            sq_newslot(v, -3);

            sq_pushstring(v, "traverseDist", -1);
            float traverseDist = link->traverseDist * DT_TRAVERSE_DIST_QUANT_FACTOR;
            if (traverseDist <= 0.0f)
                traverseDist = rdVdist(&startPos, &targetPos);
            sq_pushfloat(v, traverseDist);
            sq_newslot(v, -3);

            sq_pushstring(v, "vertA", -1);
            const SQVector3D sqVertA(vertA->x, vertA->y, vertA->z);
            sq_pushvector(v, &sqVertA);
            sq_newslot(v, -3);

            sq_pushstring(v, "vertB", -1);
            const SQVector3D sqVertB(vertB->x, vertB->y, vertB->z);
            sq_pushvector(v, &sqVertB);
            sq_newslot(v, -3);

            sq_pushstring(v, "targetPolyRef", -1);
            sq_pushinteger(v, (SQInteger)link->ref);
            sq_newslot(v, -3);

            sq_arrayappend(v, -2);
        }

        linkIdx = link->next;
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// NavMesh Traverse Link Ban API
//=============================================================================

// Banned traverse type marker — type 31 is not in the corridor's traverseFlags
// (0x8013F), so traverseFilter rejects it during pathfinding.
static const unsigned char TRAVERSE_TYPE_BANNED = 31;

//-----------------------------------------------------------------------------
// Purpose: bans a single directional traverse link (sourceRef -> targetRef).
// Returns true if the link was found and banned.
//-----------------------------------------------------------------------------
static bool Internal_BanTraverseLinkOneWay(dtNavMesh* nav, dtPolyRef sourceRef, dtPolyRef targetRef)
{
    const dtMeshTile* constTile = nullptr;
    const dtPoly* constPoly = nullptr;
    nav->getTileAndPolyByRefUnsafe(sourceRef, &constTile, &constPoly);

    if (!constTile || !constPoly)
        return false;

    dtMeshTile* tile = const_cast<dtMeshTile*>(constTile);

    unsigned int linkIdx = constPoly->firstLink;
    while (linkIdx != DT_NULL_LINK)
    {
        dtLink& link = tile->links[linkIdx];

        if (link.ref == targetRef && link.hasTraverseType())
        {
            const uint64_t key = ((uint64_t)sourceRef << 32) | (uint64_t)targetRef;
            g_bannedTraverseLinks[key] = link.traverseType;

            link.traverseType = (link.traverseType & 0xE0) | TRAVERSE_TYPE_BANNED;
            return true;
        }

        linkIdx = link.next;
    }

    return false;
}

//-----------------------------------------------------------------------------
// Purpose: bans a traverse link in both directions (A->B and B->A).
// Returns true if at least one direction was found and banned.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_BanTraverseLink(HSQUIRRELVM v)
{
    SQInteger sourcePolyRefInt;
    SQInteger targetPolyRefInt;
    SQInteger hullIdx;

    sq_getinteger(v, 2, &sourcePolyRefInt);
    sq_getinteger(v, 3, &targetPolyRefInt);
    sq_getinteger(v, 4, &hullIdx);

    if (!Internal_ServerScript_ValidateHull(hullIdx))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const Hull_e hullType = Hull_e(hullIdx);
    const NavMeshType_e navType = NAI_Hull::NavMeshType(hullType);
    dtNavMesh* const nav = Detour_GetNavMeshByType(navType);

    if (!nav)
    {
        v_SQVM_ScriptError("NavMesh_BanTraverseLink: navmesh not loaded for hull type %d", hullIdx);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const dtPolyRef sourceRef = (dtPolyRef)sourcePolyRefInt;
    const dtPolyRef targetRef = (dtPolyRef)targetPolyRefInt;

    // Ban both directions
    bool fwd = Internal_BanTraverseLinkOneWay(nav, sourceRef, targetRef);
    bool rev = Internal_BanTraverseLinkOneWay(nav, targetRef, sourceRef);

    sq_pushbool(v, fwd || rev);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: unbans a single directional traverse link by restoring its original
// traverseType. Returns true if the link was found and restored.
//-----------------------------------------------------------------------------
static bool Internal_UnbanTraverseLinkOneWay(dtNavMesh* nav, dtPolyRef sourceRef, dtPolyRef targetRef)
{
    const uint64_t key = ((uint64_t)sourceRef << 32) | (uint64_t)targetRef;
    auto it = g_bannedTraverseLinks.find(key);

    if (it == g_bannedTraverseLinks.end())
        return false;

    const unsigned char originalType = it->second;

    const dtMeshTile* constTile = nullptr;
    const dtPoly* constPoly = nullptr;
    nav->getTileAndPolyByRefUnsafe(sourceRef, &constTile, &constPoly);

    if (!constTile || !constPoly)
        return false;

    dtMeshTile* tile = const_cast<dtMeshTile*>(constTile);

    unsigned int linkIdx = constPoly->firstLink;
    while (linkIdx != DT_NULL_LINK)
    {
        dtLink& link = tile->links[linkIdx];

        if (link.ref == targetRef)
        {
            link.traverseType = originalType;
            g_bannedTraverseLinks.erase(it);
            return true;
        }

        linkIdx = link.next;
    }

    return false;
}

//-----------------------------------------------------------------------------
// Purpose: unbans a previously banned traverse link in both directions.
// Returns true if at least one direction was restored.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_UnbanTraverseLink(HSQUIRRELVM v)
{
    SQInteger sourcePolyRefInt;
    SQInteger targetPolyRefInt;
    SQInteger hullIdx;

    sq_getinteger(v, 2, &sourcePolyRefInt);
    sq_getinteger(v, 3, &targetPolyRefInt);
    sq_getinteger(v, 4, &hullIdx);

    if (!Internal_ServerScript_ValidateHull(hullIdx))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const Hull_e hullType = Hull_e(hullIdx);
    const NavMeshType_e navType = NAI_Hull::NavMeshType(hullType);
    dtNavMesh* const nav = Detour_GetNavMeshByType(navType);

    if (!nav)
    {
        v_SQVM_ScriptError("NavMesh_UnbanTraverseLink: navmesh not loaded for hull type %d", hullIdx);
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const dtPolyRef sourceRef = (dtPolyRef)sourcePolyRefInt;
    const dtPolyRef targetRef = (dtPolyRef)targetPolyRefInt;

    bool fwd = Internal_UnbanTraverseLinkOneWay(nav, sourceRef, targetRef);
    bool rev = Internal_UnbanTraverseLinkOneWay(nav, targetRef, sourceRef);

    sq_pushbool(v, fwd || rev);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns navmesh polygon refs whose vertices lie within a horizontal
//          radius of a point. Intended for spatial lookups that need to walk
//          polygon geometry from script (e.g. border-edge cover queries).
//
//          Walks every polygon in the navmesh and filters by distance. Since
//          polys are only stored per-tile, the loop visits each tile (via
//          getMaxTiles() / getTile()) to reach its poly array — tiles are
//          the container, polys are the unit of interest. Same iteration
//          pattern the engine's own drawPolyMeshFaces() uses at
//          DetourDebugDraw.cpp:62. Deliberately avoids
//          dtNavMeshQuery::queryPolygons, which routes through a
//          small/large-area dispatcher (DetourNavMeshQuery.cpp:1102) whose
//          large-area spiral search bails early and drops polys near the
//          outer edge of the query region.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_QueryPolysInRadius(HSQUIRRELVM v)
{
    const SQVector3D* centerVec = nullptr;
    SQFloat radius;
    SQInteger hullIdx;

    sq_getvector(v, 2, &centerVec);
    sq_getfloat(v, 3, &radius);
    sq_getinteger(v, 4, &hullIdx);

    dtQueryFilter unusedFilter;
    rdVec3D unusedHalfExtents;

    dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &unusedFilter, &unusedHalfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const rdVec3D center(centerVec->x, centerVec->y, centerVec->z);
    const float radius2DSq = radius * radius;

    sq_newarray(v, 0);

    const int maxTiles = nav->getMaxTiles();
    for (int t = 0; t < maxTiles; t++)
    {
        const dtMeshTile* tile = nav->getTile(t);
        if (!tile || !tile->header)
            continue;

        const dtPolyRef base = nav->getPolyRefBase(tile);
        const int polyCount = tile->header->polyCount;

        for (int i = 0; i < polyCount; i++)
        {
            const dtPoly* p = &tile->polys[i];

            if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                continue;

            // Include the poly if any of its vertices is within the query
            // radius (2D). Permissive by design: script does per-edge
            // filtering afterward with GetPolyVerts / GetPolyEdgeNeighbors.
            bool inRange = false;
            for (int k = 0; k < p->vertCount; k++)
            {
                const rdVec3D* vert = &tile->verts[p->verts[k]];
                const float dx = vert->x - center.x;
                const float dy = vert->y - center.y;
                if (dx * dx + dy * dy <= radius2DSq)
                {
                    inRange = true;
                    break;
                }
            }

            if (!inRange)
                continue;

            const dtPolyRef ref = base | (dtPolyRef)i;
            sq_pushinteger(v, (SQInteger)ref);
            sq_arrayappend(v, -2);
        }
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns the closest position inside a horizontal circle that is
//          (a) on a walkable navmesh poly, (b) at least `inwardBuffer` units
//          inside the circle's boundary, and (c) reachable from
//          `searchOrigin`'s poly under the PILOT traverse table. Returns
//          null when no candidate satisfies all three.
//
//          Internal flow:
//          1. Snap searchOrigin to a start poly (Z-priority, hull extents).
//          2. Walk every poly in every tile (same pattern as
//             NavMesh_QueryPolysInRadius — see that function's comment for
//             why we avoid dtNavMeshQuery::queryPolygons).
//          3. For each poly: find its closest vertex to searchOrigin that is
//             also at least inwardBuffer inside the circle. Skip if none.
//          4. Sort candidates ascending by 2D distance to searchOrigin.
//          5. First candidate whose poly is reachable wins; return its vert.
//
//          Reachability uses the same isGoalPolyReachable table check that
//          NavMesh_CorridorSetPath uses internally — so a non-null result
//          is guaranteed to be a target the corridor will accept.
//
//          Designed to replace a script-side workflow that called
//          NavMesh_QueryPolysInRadius + NavMesh_GetPolyVerts (×N) +
//          per-bot script sort + repeated CorridorSetPath probes. Keeping
//          everything in C++ eliminates the marshaling and per-comparison
//          VM overhead of doing this from Squirrel.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_FindClosestReachableInRing(HSQUIRRELVM v)
{
    const SQVector3D* searchOriginVec = nullptr;
    const SQVector3D* centerVec = nullptr;
    SQFloat radius;
    SQFloat inwardBuffer;
    SQInteger hullIdx;

    sq_getvector(v, 2, &searchOriginVec);
    sq_getvector(v, 3, &centerVec);
    sq_getfloat(v, 4, &radius);
    sq_getfloat(v, 5, &inwardBuffer);
    sq_getinteger(v, 6, &hullIdx);

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

    // Match the corridor's filter setup (vscript_server.cpp:1091-1104) so the
    // start-poly snap resolves to the same poly NavMesh_CorridorSetPath would.
    // Confirmed via [Nav] SetPath FAILED logs after [Rotate] Locked target —
    // the helper's filter (excludes DISABLED) was snapping to a different poly
    // than the corridor's filter (excludes nothing), causing isGoalPolyReachable
    // to return true here but false in SetPath.
    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF);
    filter.setExcludeFlags(0);
    filter.setTraverseFlags(0x8013F); // ANIMTYPE_PILOT traverse types

    const Vector3D& maxs = NAI_Hull::Maxs(hullType);
    const rdVec3D halfExtents(maxs.x, maxs.y, maxs.z);

    const rdVec3D searchOrigin(searchOriginVec->x, searchOriginVec->y, searchOriginVec->z);
    const rdVec3D center(centerVec->x, centerVec->y, centerVec->z);

    // Snap searchOrigin to a start poly so we can run reachability checks
    // from it. Off-mesh bots get null back.
    dtNavMeshQuery query;
    query.attachNavMeshUnsafe(nav);

    dtPolyRef startRef = 0;
    rdVec3D nearestStart;
    if (!Internal_FindNearestPolyByHeight(&query, &searchOrigin, &halfExtents,
                                           &filter, &startRef, &nearestStart))
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    // Inward buffer clamps to zero so tiny rings still allow center-area verts.
    float effectiveInward = (float)radius - (float)inwardBuffer;
    if (effectiveInward < 0.0f)
        effectiveInward = 0.0f;
    const float inwardLimitSq = effectiveInward * effectiveInward;

    struct Candidate
    {
        dtPolyRef polyRef;
        rdVec3D   rep;
        float     distToBotSq;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(512);

    const int maxTiles = nav->getMaxTiles();
    for (int t = 0; t < maxTiles; t++)
    {
        const dtMeshTile* tile = nav->getTile(t);
        if (!tile || !tile->header)
            continue;

        const dtPolyRef base = nav->getPolyRefBase(tile);
        const int polyCount = tile->header->polyCount;

        for (int i = 0; i < polyCount; i++)
        {
            const dtPoly* p = &tile->polys[i];

            if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                continue;

            // Use the poly's precomputed center (middle of the tile) as the
            // candidate target. Centers sit unambiguously inside one poly, so
            // when CorridorSetPath later snaps this position to find a poly,
            // it lands on the same poly we're checking reachability against.
            // Picking a vertex (corner) caused snap ambiguity because corners
            // are shared by adjacent polys.
            const rdVec3D& polyCenter = p->center;

            // Filter: poly center must be inside the buffered ring.
            const float fcx = polyCenter.x - center.x;
            const float fcy = polyCenter.y - center.y;
            if (fcx * fcx + fcy * fcy > inwardLimitSq)
                continue;

            const float bx = polyCenter.x - searchOrigin.x;
            const float by = polyCenter.y - searchOrigin.y;
            const float distToBotSq = bx * bx + by * by;

            const dtPolyRef ref = base | (dtPolyRef)i;
            candidates.push_back({ref, polyCenter, distToBotSq});
        }
    }

    if (candidates.empty())
    {
        v->PushNull();
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.distToBotSq < b.distToBotSq;
        });

    const int traverseTableIndex = NavMesh_GetTraverseTableIndexForAnimType(ANIMTYPE_PILOT);

    for (const Candidate& c : candidates)
    {
        if (nav->isGoalPolyReachable(startRef, c.polyRef, false, traverseTableIndex))
        {
            const SQVector3D result(c.rep.x, c.rep.y, c.rep.z);
            sq_pushvector(v, &result);
            SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
        }
    }

    // No reachable poly inside the ring.
    v->PushNull();
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns the world-space vertices of a navmesh polygon, in the
//          polygon's native vertex order. Pair index i of this result with
//          index i of NavMesh_GetPolyEdgeNeighbors to reason about edge i.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetPolyVerts(HSQUIRRELVM v)
{
    SQInteger polyRefInt;
    SQInteger hullIdx;

    sq_getinteger(v, 2, &polyRefInt);
    sq_getinteger(v, 3, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const dtPolyRef polyRef = (dtPolyRef)polyRefInt;

    sq_newarray(v, 0);

    if (!polyRef)
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    nav->getTileAndPolyByRefUnsafe(polyRef, &tile, &poly);

    if (!tile || !poly)
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    for (int i = 0; i < poly->vertCount; i++)
    {
        const unsigned short vertIdx = poly->verts[i];
        const rdVec3D* vert = &tile->verts[vertIdx];
        const SQVector3D sqVert(vert->x, vert->y, vert->z);
        sq_pushvector(v, &sqVert);
        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: returns a polygon's per-edge neighbor values (dtPoly::neis). A
//          value of 0 means the edge is a border edge (no walkable neighbor
//          in the same tile and not an external link) and therefore runs
//          along real obstacle geometry. Any non-zero value means the edge
//          is internal to the walkable surface and is not cover.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_GetPolyEdgeNeighbors(HSQUIRRELVM v)
{
    SQInteger polyRefInt;
    SQInteger hullIdx;

    sq_getinteger(v, 2, &polyRefInt);
    sq_getinteger(v, 3, &hullIdx);

    dtQueryFilter filter;
    rdVec3D halfExtents;

    dtNavMesh* const nav = Internal_ServerScript_NavMesh_GetNavMesh(
        v, hullIdx, &filter, &halfExtents);

    if (!nav)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const dtPolyRef polyRef = (dtPolyRef)polyRefInt;

    sq_newarray(v, 0);

    if (!polyRef)
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    const dtMeshTile* tile = nullptr;
    const dtPoly* poly = nullptr;
    nav->getTileAndPolyByRefUnsafe(polyRef, &tile, &poly);

    if (!tile || !poly)
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    for (int i = 0; i < poly->vertCount; i++)
    {
        sq_pushinteger(v, (SQInteger)poly->neis[i]);
        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// Cover Query (NavMesh_QueryCoverCandidates)
//=============================================================================
// One-shot port of the script-side cover/peek query in
// scripts/vscripts/botai/world/_cover_query.nut. The original ran the entire
// candidate-generation + scoring + nav-reach + peek-classification pipeline
// in Squirrel, marshalling thousands of poly verts and trace results across
// the script ↔ C++ boundary per call. This native runs the same algorithm
// in C++ memory and returns one ranked list — eliminating the per-poly,
// per-trace, per-NavDist marshalling cost.
//
// Algorithm + tuning constants mirror the script implementation 1:1 so the
// returned shape is interchangeable with the original CoverQuery_Find. See
// the script comments for the design rationale (alignment gate, two-phase
// sort, hideAnchor/exposeAnchor split, etc.) — they're not duplicated here.

namespace {

// --- Tuning constants -------------------------------------------------------
// Mirror values in scripts/vscripts/botai/_constants.nut and the file-private
// constants in scripts/vscripts/botai/world/_cover_query.nut. Update both
// sides together if any of these change.
constexpr float COVER_QUERY_RADIUS_DEFAULT   = 600.0f;
constexpr float COVER_EDGE_PAD               = 4.0f;
constexpr float COVER_HULL_RADIUS            = 18.0f;
constexpr float COVER_EDGE_ALIGNMENT_MIN     = 0.87f;   // cos(30°)
constexpr float COVER_W_LOS_CLEAR_OUT_OF_FOV = 0.4f;
constexpr float COVER_W_COVER                = 0.75f;
constexpr float COVER_W_REACH                = 0.25f;
constexpr int   COVER_MAX_RESULTS            = 16;
constexpr int   COVER_PRESCORE_TOP_K         = 32;
constexpr float COVER_PEEK_STEP              = 30.0f;
constexpr int   COVER_PEEK_MAX_STEPS         = 4;
constexpr float COVER_EYE_STAND_Z            = 60.0f;
constexpr float COVER_EYE_CROUCH_Z           = 32.0f;
constexpr float COVER_LOS_SAMPLE_FEET_Z      = 4.0f;
constexpr float COVER_LOS_SAMPLE_CENTER_Z    = 36.0f;
constexpr float COVER_LOS_CLEAR_FRACTION     = 0.99f;
constexpr float COVER_BOT_FOV_DEGREES        = 90.0f;
constexpr float COVER_DEG_TO_RAD             = 0.01745329251994329577f;
// Mirrored from script COVER_MIN_VIABLE — used only to gate `result.best`.
constexpr float COVER_MIN_VIABLE             = 0.45f;
// Tight bounds passed to NavMesh_GetNearestPosInBounds during SIDE peek
// sweep (script: navBounds = <2, 2, 16>).
constexpr float COVER_PEEK_NAV_BOUNDS_XY     = 2.0f;
constexpr float COVER_PEEK_NAV_BOUNDS_Z      = 16.0f;

// String labels — must match the constants in _constants.nut.
constexpr const char* COVER_STATE_BLOCKED_STANDING_STR    = "BLOCKED_STANDING";
constexpr const char* COVER_STATE_BLOCKED_CROUCH_ONLY_STR = "BLOCKED_CROUCH_ONLY";
constexpr const char* COVER_STATE_EXPOSED_STR             = "EXPOSED";
constexpr const char* COVER_PEEK_TYPE_DUCK_STR            = "DUCK";
constexpr const char* COVER_PEEK_TYPE_SIDE_LEFT_STR       = "SIDE_LEFT";
constexpr const char* COVER_PEEK_TYPE_SIDE_RIGHT_STR      = "SIDE_RIGHT";

// Stance-aware LOS classification.
enum class CoverBlockedState
{
    BlockedStanding   = 0,
    BlockedCrouchOnly = 1,
    Exposed           = 2,
};

const char* CoverQuery_BlockedStateStr(CoverBlockedState s)
{
    switch (s)
    {
    case CoverBlockedState::BlockedStanding:   return COVER_STATE_BLOCKED_STANDING_STR;
    case CoverBlockedState::BlockedCrouchOnly: return COVER_STATE_BLOCKED_CROUCH_ONLY_STR;
    case CoverBlockedState::Exposed:           return COVER_STATE_EXPOSED_STR;
    }
    return COVER_STATE_EXPOSED_STR;
}

// --- Internal candidate -----------------------------------------------------
// Accumulates fields through phases. Mirrors the script-side cand table.
struct Cover_Candidate
{
    rdVec3D pos;
    rdVec3D inwardDir;
    float   euclideanDist = 0.0f;   // tiebreak in phase-1 sort
    float   coverScore    = 0.0f;   // min per-threat (phase 1)
    float   minPerThreat  = 0.0f;
    float   score         = 0.0f;   // final blended (phase 2)
    bool    navUnreachable = false;

    // Up to 3 peeks: DUCK, SIDE+, SIDE-.
    struct Peek
    {
        rdVec3D     exposeAnchor;
        const char* peekType;
    };
    Peek peeks[3];
    int  peekCount = 0;

    // Debug-only peek probe metadata (matches script cand.peekProbe).
    bool              hasPeekProbe       = false;
    CoverBlockedState targetBlockedState = CoverBlockedState::Exposed;
    float             standShotFrac      = -1.0f;
    const char*       peekDecision       = "NOT_COVER_FROM_TARGET";
};

struct Cover_Threat
{
    rdVec3D eyePos;
    rdVec3D forward;
};

// --- Shared nav-distance scratch -------------------------------------------
// One nav query + corridor reused across every NavDist probe in a single
// cover query call. Lazy-init on first use; reattached when the navmesh
// changes (level reload). Same single-shared-corridor pattern as the
// script-side NavDistance system — safe because the server is single-threaded
// and cover queries don't recurse.
constexpr int COVER_NAV_MAX_PATH = 256;

struct CoverNavScratch
{
    dtNavMeshQuery   query;
    dtPathCorridor   corridor;
    dtQueryFilter    filter;
    bool             initialized   = false;
    bool             filterReady   = false;
    const dtNavMesh* attachedNav   = nullptr;
};
static CoverNavScratch g_coverNavScratch;

bool CoverQuery_EnsureNavScratch(const dtNavMesh* nav)
{
    if (!g_coverNavScratch.initialized)
    {
        if (!g_coverNavScratch.corridor.init(COVER_NAV_MAX_PATH))
            return false;
        g_coverNavScratch.initialized = true;
    }

    // One-time filter setup — the configuration is fixed (PILOT traverse
    // flags + per-type costs from g_traverseAnimDefaultCosts + zipline
    // override). Mirrors NavMesh_CreateCorridor's filter setup
    // (vscript_server.cpp:1091-1104) so reach distances align with what the
    // script-side NavDistance corridor would have produced.
    if (!g_coverNavScratch.filterReady)
    {
        dtQueryFilter& f = g_coverNavScratch.filter;
        f.setIncludeFlags(0xFFFF);
        f.setExcludeFlags(0);
        f.setTraverseFlags(0x8013F); // ANIMTYPE_PILOT traverse types.
        for (int i = 0; i < DT_MAX_TRAVERSE_TYPES; ++i)
            f.setTraverseCost(i, g_traverseAnimDefaultCosts[ANIMTYPE_PILOT][i]);
        f.setTraverseCost(19, 1294.07f); // zipline override (matches CreateCorridor).
        g_coverNavScratch.filterReady = true;
    }

    if (g_coverNavScratch.attachedNav != nav)
    {
        if (dtStatusFailed(g_coverNavScratch.query.init(nav, 2048)))
            return false;
        g_coverNavScratch.attachedNav = nav;
    }
    return true;
}

// --- Trace + blocked state --------------------------------------------------
float CoverQuery_TraceLOS(const rdVec3D& start, const rdVec3D& end)
{
    Vector3D s(start.x, start.y, start.z);
    Vector3D e(end.x, end.y, end.z);
    Ray_t ray(s, e);
    trace_t tr;
    g_pEngineTraceServer->TraceRay(ray, TRACE_MASK_BLOCKLOS, &tr);
    return tr.fraction;
}

CoverBlockedState CoverQuery_GetBlockedState(const rdVec3D& fromEye, const rdVec3D& candidatePos)
{
    // Feet sanity check — typical floor-to-ceiling cover blocks all three.
    rdVec3D feetTarget(candidatePos.x, candidatePos.y, candidatePos.z + COVER_LOS_SAMPLE_FEET_Z);
    if (CoverQuery_TraceLOS(fromEye, feetTarget) >= COVER_LOS_CLEAR_FRACTION)
        return CoverBlockedState::Exposed;

    rdVec3D crouchTarget(candidatePos.x, candidatePos.y, candidatePos.z + COVER_EYE_CROUCH_Z);
    if (CoverQuery_TraceLOS(fromEye, crouchTarget) >= COVER_LOS_CLEAR_FRACTION)
        return CoverBlockedState::Exposed;

    rdVec3D standTarget(candidatePos.x, candidatePos.y, candidatePos.z + COVER_EYE_STAND_Z);
    if (CoverQuery_TraceLOS(fromEye, standTarget) >= COVER_LOS_CLEAR_FRACTION)
        return CoverBlockedState::BlockedCrouchOnly;

    return CoverBlockedState::BlockedStanding;
}

// --- Nav distance (mirrors script NavDistance_Get) --------------------------
// Returns total walk distance along the corridor's findCorners output, or
// -1.0f when unreachable (snap fail / not in reachability table / pathfind
// fail / no corners). Reuses the file-static query+corridor; each call
// fully overwrites prior state via setCorridor + setPos.
float CoverQuery_NavDistance(
    dtQueryFilter& filter,
    const rdVec3D& halfExtents,
    const rdVec3D& start,
    const rdVec3D& target)
{
    dtNavMeshQuery& query    = g_coverNavScratch.query;
    dtPathCorridor& corridor = g_coverNavScratch.corridor;

    dtPolyRef startRef = 0, targetRef = 0;
    rdVec3D nearestStart, nearestTarget;
    if (!Internal_FindNearestPolyByHeight(&query, &start, &halfExtents, &filter, &startRef, &nearestStart))
        return -1.0f;
    if (!Internal_FindNearestPolyByHeight(&query, &target, &halfExtents, &filter, &targetRef, &nearestTarget))
        return -1.0f;

    const dtNavMesh* nav = query.getAttachedNavMesh();
    const int traverseTableIndex = NavMesh_GetTraverseTableIndexForAnimType(ANIMTYPE_PILOT);
    if (!nav->isGoalPolyReachable(startRef, targetRef, false, traverseTableIndex))
        return -1.0f;

    dtPolyRef     pathPolys[COVER_NAV_MAX_PATH];
    unsigned char pathJumps[COVER_NAV_MAX_PATH];
    int pathCount = 0;

    dtStatus status = query.findPath(
        startRef, targetRef, &nearestStart, &nearestTarget, &filter,
        pathPolys, pathJumps, &pathCount, COVER_NAV_MAX_PATH);
    if (dtStatusFailed(status) || pathCount == 0 || (status & DT_BUFFER_TOO_SMALL))
        return -1.0f;

    const bool isPartial = (status & DT_PARTIAL_RESULT) != 0;
    if (isPartial && !(status & DT_OUT_OF_NODES))
        return -1.0f;

    rdVec3D corridorTarget = nearestTarget;
    if (isPartial)
    {
        rdVec3D lastPolyPos;
        float distUnused;
        if (dtStatusSucceed(query.closestPointOnPolyBoundary(pathPolys[pathCount - 1], &nearestTarget, &lastPolyPos, &distUnused)))
            corridorTarget = lastPolyPos;
    }
    corridor.setCorridor(&corridorTarget, pathPolys, pathJumps, pathCount);
    corridor.setPos(&nearestStart);
    corridor.movePosition(&nearestStart, &query, &filter);

    // Script asks for 32 corners; the existing CorridorGetCorners native
    // clamps to 8 (vscript_server.cpp:1538-1539), so script effectively
    // sums up to 8. Match that ceiling here.
    rdVec3D       cornerVerts[8];
    unsigned char cornerFlags[8];
    dtPolyRef     cornerPolys[8];
    unsigned char cornerJumps[8];
    const int cornerCount = corridor.findCorners(
        cornerVerts, cornerFlags, cornerPolys, cornerJumps,
        8, &query, &filter);
    if (cornerCount == 0)
        return -1.0f;

    float total = 0.0f;
    rdVec3D prev = nearestStart;
    for (int i = 0; i < cornerCount; ++i)
    {
        const float dx = cornerVerts[i].x - prev.x;
        const float dy = cornerVerts[i].y - prev.y;
        const float dz = cornerVerts[i].z - prev.z;
        total += sqrtf(dx*dx + dy*dy + dz*dz);
        prev = cornerVerts[i];
    }
    return total;
}

// --- Argument parsing -------------------------------------------------------
bool Internal_ReadVectorArray(HSQUIRRELVM v, SQInteger argIdx, std::vector<rdVec3D>& out)
{
    out.clear();
    HSQOBJECT obj{};
    if (SQ_FAILED(sq_getstackobj(v, argIdx, &obj)))
    {
        v_SQVM_ScriptError("NavMesh_QueryCoverCandidates: failed to read array argument at index %d", argIdx);
        return false;
    }
    if (sq_type(obj) != OT_ARRAY)
    {
        v_SQVM_ScriptError("NavMesh_QueryCoverCandidates: expected array at argument %d", argIdx);
        return false;
    }
    const SQArray* arr = _array(obj);
    if (!arr)
    {
        v_SQVM_ScriptError("NavMesh_QueryCoverCandidates: invalid array at argument %d", argIdx);
        return false;
    }
    out.reserve(arr->Size());
    for (SQInteger i = 0; i < arr->Size(); ++i)
    {
        const SQObject& item = arr->_values[i];
        if (sq_type(item) != OT_VECTOR)
        {
            v_SQVM_ScriptError("NavMesh_QueryCoverCandidates: array at argument %d must contain only vectors (item %lld)", argIdx, (long long)i);
            return false;
        }
        const SQVector3D* vec = _vector(item);
        out.emplace_back(vec->x, vec->y, vec->z);
    }
    return true;
}

// --- Result builders --------------------------------------------------------
void Internal_PushVector(HSQUIRRELVM v, const rdVec3D& vec)
{
    SQVector3D sq(vec.x, vec.y, vec.z);
    sq_pushvector(v, &sq);
}

void Internal_PushPeek(HSQUIRRELVM v, const Cover_Candidate::Peek& peek)
{
    sq_newtable(v);

    sq_pushstring(v, "exposeAnchor", -1);
    Internal_PushVector(v, peek.exposeAnchor);
    sq_newslot(v, -3);

    sq_pushstring(v, "peekType", -1);
    sq_pushstring(v, peek.peekType, -1);
    sq_newslot(v, -3);
}

void Internal_PushCandidate(HSQUIRRELVM v, const Cover_Candidate& cand, bool hasTarget)
{
    sq_newtable(v);

    sq_pushstring(v, "pos", -1);
    Internal_PushVector(v, cand.pos);
    sq_newslot(v, -3);

    // hideAnchor is an alias of pos (extended-cover-system.md naming).
    sq_pushstring(v, "hideAnchor", -1);
    Internal_PushVector(v, cand.pos);
    sq_newslot(v, -3);

    sq_pushstring(v, "inwardDir", -1);
    Internal_PushVector(v, cand.inwardDir);
    sq_newslot(v, -3);

    sq_pushstring(v, "score", -1);
    sq_pushfloat(v, cand.score);
    sq_newslot(v, -3);

    sq_pushstring(v, "peeks", -1);
    sq_newarray(v, 0);
    for (int i = 0; i < cand.peekCount; ++i)
    {
        Internal_PushPeek(v, cand.peeks[i]);
        sq_arrayappend(v, -2);
    }
    sq_newslot(v, -3);

    if (hasTarget && cand.hasPeekProbe)
    {
        sq_pushstring(v, "peekProbe", -1);
        sq_newtable(v);

        sq_pushstring(v, "targetBlockedState", -1);
        const char* stateStr = CoverQuery_BlockedStateStr(cand.targetBlockedState);
        sq_pushstring(v, stateStr, -1);
        sq_newslot(v, -3);

        sq_pushstring(v, "standShotFrac", -1);
        sq_pushfloat(v, cand.standShotFrac);
        sq_newslot(v, -3);

        sq_pushstring(v, "decision", -1);
        sq_pushstring(v, cand.peekDecision, -1);
        sq_newslot(v, -3);

        sq_newslot(v, -3);
    }
}

} // anonymous namespace

//-----------------------------------------------------------------------------
// Purpose: builds a sorted list of cover candidates for a bot at originPos
//          against the supplied threats. One-shot replacement for the
//          script-side CoverQuery_Find pipeline (poly fan-out + per-candidate
//          per-threat trace fan-out + per-survivor NavDistance + peek
//          classification + sort) — collapses thousands of script ↔ C++
//          marshalling crossings per call into one.
//
// Pipeline mirrors the script implementation 1:1 (see _cover_query.nut for
// design rationale):
//   1. Walk poly tiles whose any-vert is within `radius` of originPos.
//   2. For each border edge (neis[i] == 0), place a candidate inside the
//      walkable side, offset by hull radius + edge pad.
//   3. Drop candidates "past" any threat (would force running through them).
//   4. Phase 1 cheap: per-threat alignment + FOV. If no threat sees the
//      candidate's wall as cover from itself (alignment gate), score 0 and
//      skip the trace work. Otherwise compute stance-aware blocked state via
//      3 LOS traces per threat (feet, crouch eye, stand eye).
//   5. Sort by coverScore desc (tiebreak by euclidean dist asc), trim to
//      COVER_PRESCORE_TOP_K survivors.
//   6. Phase 2 heavy on survivors: nav-path distance for reach blend, and
//      peek classification — DUCK probe (one trace) or SIDE sweep
//      (NavMesh_GetNearestPosInBounds + 2 traces per step, both directions).
//   7. Drop nav-unreachable. Sort by final blended score desc, trim to
//      COVER_MAX_RESULTS.
//   8. Return { candidates, best, stats } — same shape as script.
//
// `targetPos = <0,0,0>` disables peek classification (no target → no peek).
// `radius = 0.0` falls back to COVER_QUERY_RADIUS_DEFAULT.
//-----------------------------------------------------------------------------
static SQRESULT ServerScript_NavMesh_QueryCoverCandidates(HSQUIRRELVM v)
{
    const SQVector3D* originVec = nullptr;
    const SQVector3D* targetVec = nullptr;
    SQFloat   radiusArg = 0.0f;
    SQInteger hullIdx   = 0;

    sq_getvector(v, 2, &originVec);
    // Slots 3 and 4 are arrays — read via Internal_ReadVectorArray below.
    sq_getvector(v, 5, &targetVec);
    sq_getfloat(v, 6, &radiusArg);
    sq_getinteger(v, 7, &hullIdx);

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

    if (!CoverQuery_EnsureNavScratch(nav))
    {
        v_SQVM_ScriptError("NavMesh_QueryCoverCandidates: failed to initialize nav scratch");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    std::vector<rdVec3D> threatEyes, threatForwards;
    if (!Internal_ReadVectorArray(v, 3, threatEyes))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    if (!Internal_ReadVectorArray(v, 4, threatForwards))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    if (threatEyes.size() != threatForwards.size())
    {
        v_SQVM_ScriptError("NavMesh_QueryCoverCandidates: threat eye/forward arrays differ in length (%zu vs %zu)",
            threatEyes.size(), threatForwards.size());
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    std::vector<Cover_Threat> threats;
    threats.reserve(threatEyes.size());
    for (size_t i = 0; i < threatEyes.size(); ++i)
        threats.push_back({ threatEyes[i], threatForwards[i] });
    const int threatCount = (int)threats.size();

    const rdVec3D origin(originVec->x, originVec->y, originVec->z);
    const rdVec3D targetPos(targetVec->x, targetVec->y, targetVec->z);
    const bool hasTarget = (targetPos.x != 0.0f) || (targetPos.y != 0.0f) || (targetPos.z != 0.0f);

    float radius = (float)radiusArg;
    if (radius <= 0.0f)
        radius = COVER_QUERY_RADIUS_DEFAULT;
    const float radius2DSq = radius * radius;

    // Hull half-extents for nav snaps (matches Internal_ServerScript_NavMesh_GetNavMesh).
    const Vector3D& hullMaxs = NAI_Hull::Maxs(hullType);
    const rdVec3D halfExtents(hullMaxs.x, hullMaxs.y, hullMaxs.z);

    // Filter is owned by g_coverNavScratch — initialized once in
    // CoverQuery_EnsureNavScratch above. Configuration matches NavDistance_Get's
    // corridor filter so reach distances are identical.
    dtQueryFilter& coverNavFilter = g_coverNavScratch.filter;

    // -----------------------------------------------------------------------
    // 1 + 2. Generate candidates by walking poly tiles, emitting one per
    // border edge offset inward by HULL_RADIUS + EDGE_PAD.
    // -----------------------------------------------------------------------
    std::vector<Cover_Candidate> candidates;
    candidates.reserve(512);
    int generated = 0;

    const int maxTiles = nav->getMaxTiles();
    for (int t = 0; t < maxTiles; ++t)
    {
        const dtMeshTile* tile = nav->getTile(t);
        if (!tile || !tile->header)
            continue;

        const int polyCount = tile->header->polyCount;
        for (int pi = 0; pi < polyCount; ++pi)
        {
            const dtPoly* p = &tile->polys[pi];
            if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                continue;

            // Inclusion: any vert within horizontal radius of origin (matches
            // NavMesh_QueryPolysInRadius's permissive criterion).
            bool inRange = false;
            for (int k = 0; k < p->vertCount; ++k)
            {
                const rdVec3D* vert = &tile->verts[p->verts[k]];
                const float dx = vert->x - origin.x;
                const float dy = vert->y - origin.y;
                if (dx * dx + dy * dy <= radius2DSq)
                {
                    inRange = true;
                    break;
                }
            }
            if (!inRange)
                continue;

            const int vertCount = p->vertCount;
            if (vertCount < 3)
                continue;

            // Centroid for inward-side discrimination.
            rdVec3D centroid(0.0f, 0.0f, 0.0f);
            for (int k = 0; k < vertCount; ++k)
            {
                const rdVec3D* vk = &tile->verts[p->verts[k]];
                centroid.x += vk->x;
                centroid.y += vk->y;
                centroid.z += vk->z;
            }
            const float invCount = 1.0f / (float)vertCount;
            centroid.x *= invCount;
            centroid.y *= invCount;
            centroid.z *= invCount;

            for (int i = 0; i < vertCount; ++i)
            {
                // Border edge: neighbor value 0 means no walkable neighbor in
                // this tile and not an external link — runs along obstacle.
                if (p->neis[i] != 0)
                    continue;

                const int nextIdx = (i + 1) % vertCount;
                const rdVec3D& a = tile->verts[p->verts[i]];
                const rdVec3D& b = tile->verts[p->verts[nextIdx]];

                const rdVec3D edgeMid(
                    (a.x + b.x) * 0.5f,
                    (a.y + b.y) * 0.5f,
                    (a.z + b.z) * 0.5f);

                // Edge perpendicular in XY, oriented toward poly interior.
                rdVec3D edgeNormal(-(b.y - a.y), (b.x - a.x), 0.0f);
                const float toCentroidX = centroid.x - edgeMid.x;
                const float toCentroidY = centroid.y - edgeMid.y;
                if (edgeNormal.x * toCentroidX + edgeNormal.y * toCentroidY < 0.0f)
                {
                    edgeNormal.x = -edgeNormal.x;
                    edgeNormal.y = -edgeNormal.y;
                }
                const float inwardLen = sqrtf(edgeNormal.x * edgeNormal.x + edgeNormal.y * edgeNormal.y);
                if (inwardLen < 0.001f)
                    continue;
                const float invInward = 1.0f / inwardLen;
                rdVec3D inwardDir(edgeNormal.x * invInward, edgeNormal.y * invInward, 0.0f);

                Cover_Candidate cand;
                cand.pos.x = edgeMid.x + inwardDir.x * (COVER_HULL_RADIUS + COVER_EDGE_PAD);
                cand.pos.y = edgeMid.y + inwardDir.y * (COVER_HULL_RADIUS + COVER_EDGE_PAD);
                cand.pos.z = edgeMid.z;
                cand.inwardDir = inwardDir;
                candidates.push_back(cand);
                ++generated;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 3. Retreat-direction filter — drop candidates "past" any threat from
    // origin's perspective (would force running into / past the threat).
    // -----------------------------------------------------------------------
    int rejectedByRetreat = 0;
    if (threatCount > 0)
    {
        std::vector<Cover_Candidate> survivors;
        survivors.reserve(candidates.size());
        for (const Cover_Candidate& cand : candidates)
        {
            bool reject = false;
            for (const Cover_Threat& threat : threats)
            {
                const float toEnemyX = threat.eyePos.x - origin.x;
                const float toEnemyY = threat.eyePos.y - origin.y;
                const float toEnemyZ = threat.eyePos.z - origin.z;
                const float enemyDist = sqrtf(toEnemyX*toEnemyX + toEnemyY*toEnemyY + toEnemyZ*toEnemyZ);
                if (enemyDist < 0.001f)
                    continue;
                const float invEnemyDist = 1.0f / enemyDist;
                const float toEnemyDirX = toEnemyX * invEnemyDist;
                const float toEnemyDirY = toEnemyY * invEnemyDist;
                const float toEnemyDirZ = toEnemyZ * invEnemyDist;

                const float toCandX = cand.pos.x - origin.x;
                const float toCandY = cand.pos.y - origin.y;
                const float toCandZ = cand.pos.z - origin.z;
                const float projection = toCandX * toEnemyDirX + toCandY * toEnemyDirY + toCandZ * toEnemyDirZ;
                if (projection >= enemyDist)
                {
                    reject = true;
                    break;
                }
            }
            if (reject)
                ++rejectedByRetreat;
            else
                survivors.push_back(cand);
        }
        candidates = std::move(survivors);
    }

    // -----------------------------------------------------------------------
    // 4. Phase 1: per-candidate alignment + FOV per threat. Skip blocked-state
    // traces unless at least one threat sees the candidate's wall as cover
    // (alignment gate) — matches script behavior.
    // -----------------------------------------------------------------------
    const float fovMinDot = cosf(COVER_BOT_FOV_DEGREES * 0.5f * COVER_DEG_TO_RAD);

    // Per-threat aligned/in-FOV scratch arrays — sized once per query.
    std::vector<bool> alignedArr(threatCount, false);
    std::vector<bool> inFovArr(threatCount, false);

    for (Cover_Candidate& cand : candidates)
    {
        const float dxOrig = cand.pos.x - origin.x;
        const float dyOrig = cand.pos.y - origin.y;
        const float dzOrig = cand.pos.z - origin.z;
        cand.euclideanDist = sqrtf(dxOrig*dxOrig + dyOrig*dyOrig + dzOrig*dzOrig);

        if (threatCount == 0)
        {
            // No threats — degenerate "everything is cover" path (script
            // initializes coverFraction=1 / coverScore=1 / minPerThreat=1).
            cand.coverScore = 1.0f;
            cand.minPerThreat = 1.0f;
            continue;
        }

        bool hasAnyAligned = false;
        for (int i = 0; i < threatCount; ++i)
        {
            const Cover_Threat& threat = threats[i];
            const float toCandX = cand.pos.x - threat.eyePos.x;
            const float toCandY = cand.pos.y - threat.eyePos.y;
            const float toCandZ = cand.pos.z - threat.eyePos.z;
            const float lenLen = sqrtf(toCandX*toCandX + toCandY*toCandY + toCandZ*toCandZ);

            bool aligned = false;
            bool inFov   = false;
            if (lenLen >= 0.001f)
            {
                const float invLen = 1.0f / lenLen;
                const float toCandDirX = toCandX * invLen;
                const float toCandDirY = toCandY * invLen;
                const float toCandDirZ = toCandZ * invLen;
                const float alignDot = cand.inwardDir.x * toCandDirX + cand.inwardDir.y * toCandDirY + cand.inwardDir.z * toCandDirZ;
                aligned = alignDot > COVER_EDGE_ALIGNMENT_MIN;
                const float fovDot = toCandDirX * threat.forward.x + toCandDirY * threat.forward.y + toCandDirZ * threat.forward.z;
                inFov = fovDot >= fovMinDot;
            }
            alignedArr[i] = aligned;
            inFovArr[i]   = inFov;
            if (aligned)
                hasAnyAligned = true;
        }

        if (!hasAnyAligned)
        {
            cand.coverScore   = 0.0f;
            cand.minPerThreat = 0.0f;
            continue;
        }

        // Survivor: per-threat blocked-state traces + aggregation.
        float minPerThreat = 1.0f;
        for (int i = 0; i < threatCount; ++i)
        {
            const CoverBlockedState st = CoverQuery_GetBlockedState(threats[i].eyePos, cand.pos);
            const bool blocked = (st != CoverBlockedState::Exposed);

            float pt = 0.0f;
            if (blocked && alignedArr[i])
                pt = 1.0f;
            else if (blocked && !alignedArr[i])
                pt = 0.0f;
            else if (inFovArr[i])
                pt = 0.0f;
            else
                pt = COVER_W_LOS_CLEAR_OUT_OF_FOV;

            if (pt < minPerThreat)
                minPerThreat = pt;
        }

        cand.coverScore   = minPerThreat;
        cand.minPerThreat = minPerThreat;
    }

    // -----------------------------------------------------------------------
    // 5. Sort by coverScore desc (tiebreak euclideanDist asc), trim to
    // COVER_PRESCORE_TOP_K.
    // -----------------------------------------------------------------------
    std::sort(candidates.begin(), candidates.end(),
        [](const Cover_Candidate& a, const Cover_Candidate& b) {
            if (a.coverScore != b.coverScore)
                return a.coverScore > b.coverScore;
            return a.euclideanDist < b.euclideanDist;
        });
    if ((int)candidates.size() > COVER_PRESCORE_TOP_K)
        candidates.resize(COVER_PRESCORE_TOP_K);

    // -----------------------------------------------------------------------
    // 6. Phase 2: NavDist + reach blend + peek classification on survivors.
    // -----------------------------------------------------------------------
    int safeFromAll     = 0;
    int exposedInFov    = 0;
    int exposedOutOfFov = 0;

    const rdVec3D peekNavBounds(COVER_PEEK_NAV_BOUNDS_XY, COVER_PEEK_NAV_BOUNDS_XY, COVER_PEEK_NAV_BOUNDS_Z);

    for (Cover_Candidate& cand : candidates)
    {
        const float dist = CoverQuery_NavDistance(coverNavFilter, halfExtents, origin, cand.pos);
        if (dist < 0.0f)
        {
            cand.navUnreachable = true;
            continue;
        }
        float reachScore = 1.0f - (dist / radius);
        if (reachScore < 0.0f) reachScore = 0.0f;
        cand.score = COVER_W_COVER * cand.coverScore + COVER_W_REACH * reachScore;

        if (hasTarget)
        {
            cand.hasPeekProbe = true;
            cand.standShotFrac = -1.0f;
            cand.peekDecision = "NOT_COVER_FROM_TARGET";

            const rdVec3D targetEye(targetPos.x, targetPos.y, targetPos.z + COVER_EYE_STAND_Z);
            const CoverBlockedState targetBlockedState = CoverQuery_GetBlockedState(targetEye, cand.pos);
            cand.targetBlockedState = targetBlockedState;

            if (targetBlockedState == CoverBlockedState::BlockedCrouchOnly)
            {
                // DUCK candidate — verify standing shot is clear.
                const rdVec3D standEye(cand.pos.x, cand.pos.y, cand.pos.z + COVER_EYE_STAND_Z);
                const rdVec3D targetTorso(targetPos.x, targetPos.y, targetPos.z + COVER_LOS_SAMPLE_CENTER_Z);
                const float standShotFrac = CoverQuery_TraceLOS(standEye, targetTorso);
                cand.standShotFrac = standShotFrac;
                if (standShotFrac >= COVER_LOS_CLEAR_FRACTION)
                {
                    cand.peeks[cand.peekCount++] = { cand.pos, COVER_PEEK_TYPE_DUCK_STR };
                    cand.peekDecision = "DUCK";
                }
                else
                {
                    cand.peekDecision = "DUCK_BLOCKED_STANDING_SHOT";
                }
            }
            else if (targetBlockedState == CoverBlockedState::BlockedStanding)
            {
                // SIDE peek — sweep ±tangent for a position with clear shot
                // and an unobstructed bot-eye reach line.
                const rdVec3D inward = cand.inwardDir;
                rdVec3D edgeTangent(-inward.y, inward.x, 0.0f);
                const float tlen = sqrtf(edgeTangent.x*edgeTangent.x + edgeTangent.y*edgeTangent.y);
                if (tlen >= 0.001f)
                {
                    const float invTlen = 1.0f / tlen;
                    edgeTangent.x *= invTlen;
                    edgeTangent.y *= invTlen;

                    const rdVec3D standEye(cand.pos.x, cand.pos.y, cand.pos.z + COVER_EYE_STAND_Z);
                    const rdVec3D targetTorso(targetPos.x, targetPos.y, targetPos.z + COVER_LOS_SAMPLE_CENTER_Z);

                    // Side label setup: rightVec relative to bot looking at target.
                    const float facingX = targetPos.x - cand.pos.x;
                    const float facingY = targetPos.y - cand.pos.y;
                    const float bflen = sqrtf(facingX*facingX + facingY*facingY);
                    rdVec3D rightVec(0.0f, 0.0f, 0.0f);
                    if (bflen >= 0.001f)
                    {
                        const float invBflen = 1.0f / bflen;
                        const float bf2DX = facingX * invBflen;
                        const float bf2DY = facingY * invBflen;
                        rightVec.x = bf2DY;
                        rightVec.y = -bf2DX;
                    }

                    rdVec3D posPlusFound(0.0f, 0.0f, 0.0f);
                    rdVec3D posMinusFound(0.0f, 0.0f, 0.0f);
                    bool foundPlus  = false;
                    bool foundMinus = false;

                    for (int k = 1; k <= COVER_PEEK_MAX_STEPS && (!foundPlus || !foundMinus); ++k)
                    {
                        const float stepDist = (float)k * COVER_PEEK_STEP;

                        if (!foundPlus)
                        {
                            const rdVec3D posPlus(
                                cand.pos.x + edgeTangent.x * stepDist,
                                cand.pos.y + edgeTangent.y * stepDist,
                                cand.pos.z);
                            // "Is there nav near this point?" — Internal_FindNearestPolyByHeight returns
                            // a poly when one exists within the bounds.
                            dtPolyRef navPolyPlus = 0;
                            rdVec3D navPosPlus;
                            const bool onNavPlus = Internal_FindNearestPolyByHeight(
                                &g_coverNavScratch.query, &posPlus, &peekNavBounds, &coverNavFilter,
                                &navPolyPlus, &navPosPlus);
                            if (onNavPlus)
                            {
                                const rdVec3D eyePlus(posPlus.x, posPlus.y, posPlus.z + COVER_EYE_STAND_Z);
                                const float shotFrac = CoverQuery_TraceLOS(eyePlus, targetTorso);
                                if (shotFrac >= COVER_LOS_CLEAR_FRACTION)
                                {
                                    const float reachFrac = CoverQuery_TraceLOS(standEye, eyePlus);
                                    if (reachFrac >= COVER_LOS_CLEAR_FRACTION)
                                    {
                                        posPlusFound = posPlus;
                                        foundPlus = true;
                                    }
                                }
                            }
                        }

                        if (!foundMinus)
                        {
                            const rdVec3D posMinus(
                                cand.pos.x - edgeTangent.x * stepDist,
                                cand.pos.y - edgeTangent.y * stepDist,
                                cand.pos.z);
                            dtPolyRef navPolyMinus = 0;
                            rdVec3D navPosMinus;
                            const bool onNavMinus = Internal_FindNearestPolyByHeight(
                                &g_coverNavScratch.query, &posMinus, &peekNavBounds, &coverNavFilter,
                                &navPolyMinus, &navPosMinus);
                            if (onNavMinus)
                            {
                                const rdVec3D eyeMinus(posMinus.x, posMinus.y, posMinus.z + COVER_EYE_STAND_Z);
                                const float shotFrac = CoverQuery_TraceLOS(eyeMinus, targetTorso);
                                if (shotFrac >= COVER_LOS_CLEAR_FRACTION)
                                {
                                    const float reachFrac = CoverQuery_TraceLOS(standEye, eyeMinus);
                                    if (reachFrac >= COVER_LOS_CLEAR_FRACTION)
                                    {
                                        posMinusFound = posMinus;
                                        foundMinus = true;
                                    }
                                }
                            }
                        }
                    }

                    if (foundPlus)
                    {
                        const char* sideStr = COVER_PEEK_TYPE_SIDE_LEFT_STR;
                        if (bflen >= 0.001f && (edgeTangent.x * rightVec.x + edgeTangent.y * rightVec.y) > 0.0f)
                            sideStr = COVER_PEEK_TYPE_SIDE_RIGHT_STR;
                        cand.peeks[cand.peekCount++] = { posPlusFound, sideStr };
                    }
                    if (foundMinus)
                    {
                        const char* sideStr = COVER_PEEK_TYPE_SIDE_LEFT_STR;
                        if (bflen >= 0.001f && ((-edgeTangent.x) * rightVec.x + (-edgeTangent.y) * rightVec.y) > 0.0f)
                            sideStr = COVER_PEEK_TYPE_SIDE_RIGHT_STR;
                        cand.peeks[cand.peekCount++] = { posMinusFound, sideStr };
                    }

                    if (foundPlus && foundMinus)        cand.peekDecision = "SIDE_BOTH";
                    else if (foundPlus)                  cand.peekDecision = "SIDE_PLUS_ONLY";
                    else if (foundMinus)                 cand.peekDecision = "SIDE_MINUS_ONLY";
                    else                                  cand.peekDecision = "FULL_COVER_NO_SIDE";
                }
                else
                {
                    cand.peekDecision = "FULL_COVER_NO_TANGENT";
                }
            }
            // Exposed → leave default "NOT_COVER_FROM_TARGET" + no peeks.
        }

        // Stat tally — matches script ordering.
        if (threatCount == 0 || cand.minPerThreat >= 1.0f)
            ++safeFromAll;
        else if (cand.minPerThreat <= 0.0f)
            ++exposedInFov;
        else
            ++exposedOutOfFov;
    }

    // -----------------------------------------------------------------------
    // 7. Drop nav-unreachable. Sort by score desc. Trim to COVER_MAX_RESULTS.
    // -----------------------------------------------------------------------
    int rejectedByUnreachable = 0;
    {
        std::vector<Cover_Candidate> reachable;
        reachable.reserve(candidates.size());
        for (const Cover_Candidate& cand : candidates)
        {
            if (cand.navUnreachable)
                ++rejectedByUnreachable;
            else
                reachable.push_back(cand);
        }
        candidates = std::move(reachable);
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Cover_Candidate& a, const Cover_Candidate& b) {
            return a.score > b.score;
        });
    if ((int)candidates.size() > COVER_MAX_RESULTS)
        candidates.resize(COVER_MAX_RESULTS);

    // -----------------------------------------------------------------------
    // 8. Build result table { candidates, best, stats }.
    // -----------------------------------------------------------------------
    sq_newtable(v);

    // candidates
    sq_pushstring(v, "candidates", -1);
    sq_newarray(v, 0);
    for (const Cover_Candidate& cand : candidates)
    {
        Internal_PushCandidate(v, cand, hasTarget);
        sq_arrayappend(v, -2);
    }
    sq_newslot(v, -3);

    // best
    sq_pushstring(v, "best", -1);
    bool haveBest = false;
    if (!candidates.empty())
    {
        const float topScore = candidates[0].score;
        if (topScore >= COVER_MIN_VIABLE)
        {
            sq_newtable(v);
            sq_pushstring(v, "pos", -1);
            Internal_PushVector(v, candidates[0].pos);
            sq_newslot(v, -3);
            sq_pushstring(v, "score", -1);
            sq_pushfloat(v, topScore);
            sq_newslot(v, -3);
            haveBest = true;
        }
    }
    if (!haveBest)
        sq_pushnull(v);
    sq_newslot(v, -3);

    // stats
    sq_pushstring(v, "stats", -1);
    sq_newtable(v);
    sq_pushstring(v, "generated", -1);
    sq_pushinteger(v, generated);
    sq_newslot(v, -3);
    sq_pushstring(v, "rejectedByRetreat", -1);
    sq_pushinteger(v, rejectedByRetreat);
    sq_newslot(v, -3);
    sq_pushstring(v, "rejectedByUnreachable", -1);
    sq_pushinteger(v, rejectedByUnreachable);
    sq_newslot(v, -3);
    sq_pushstring(v, "safeFromAll", -1);
    sq_pushinteger(v, safeFromAll);
    sq_newslot(v, -3);
    sq_pushstring(v, "exposedInFov", -1);
    sq_pushinteger(v, exposedInFov);
    sq_newslot(v, -3);
    sq_pushstring(v, "exposedOutOfFov", -1);
    sq_pushinteger(v, exposedOutOfFov);
    sq_newslot(v, -3);
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
        "Sets the path in the corridor from start to target. Returns true on success. For long paths, the corridor target may be an intermediate position toward the original target; brain re-evaluates on arrival",
        "bool",
        "int handle, vector startPos, vector targetPos", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_CorridorSetAngleFilter,
        "Enables edge-angle filtering for traverse type(s). traverseTypes can be int or array<int>",
        "void",
        "int handle, var traverseTypes, float maxAngleDeg", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_CorridorClearAngleFilter,
        "Disables edge-angle filtering for traverse type(s). traverseTypes can be int or array<int>",
        "void",
        "int handle, var traverseTypes", false);

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

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetPortalsFromPoly,
        "Gets traversal portals from a specific polygon reference (from corridor corner). Returns array of portal info",
        "array",
        "int polyRef, int hullType", false);

    // NavMesh Traverse Link Ban API
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_BanTraverseLink,
        "Bans a traverse link between two polygons. Pathfinding will skip it. Returns true on success",
        "bool",
        "int sourcePolyRef, int targetPolyRef, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_UnbanTraverseLink,
        "Restores a previously banned traverse link. Returns true on success",
        "bool",
        "int sourcePolyRef, int targetPolyRef, int hullType", false);

    // NavMesh Polygon Geometry API (for cover queries and spatial walks)
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_QueryPolysInRadius,
        "Returns polygon refs within a horizontal radius around a point. Z uses the hull's canonical height.",
        "array",
        "vector center, float radius, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_FindClosestReachableInRing,
        "Returns the closest position inside a horizontal circle that is on a walkable poly, at least inwardBuffer units inside the boundary, and reachable from searchOrigin's poly under the PILOT traverse table. Returns null when no candidate satisfies all three.",
        "vector ornull",
        "vector searchOrigin, vector center, float radius, float inwardBuffer, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetPolyVerts,
        "Returns the world-space vertices of a polygon in vertex order. Pair with NavMesh_GetPolyEdgeNeighbors to walk edges.",
        "array",
        "int polyRef, int hullType", false);

    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_GetPolyEdgeNeighbors,
        "Returns per-edge neighbor values. A value of 0 means the edge is a border edge (runs along obstacle geometry).",
        "array",
        "int polyRef, int hullType", false);

    // Cover/peek query — full pipeline in C++ (replaces script-side CoverQuery_Find).
    // Returns { candidates, best, stats } matching the original script result shape.
    DEFINE_SERVER_SCRIPTFUNC_NAMED(s, NavMesh_QueryCoverCandidates,
        "Builds a sorted list of cover candidates around originPos against the supplied threats. Replaces the script-side CoverQuery_Find pipeline. Threat eyePos and forward arrays must be parallel. targetPos = <0,0,0> disables peek classification. radius = 0 uses the default.",
        "table",
        "vector originPos, array threatEyePositions, array threatForwards, vector targetPos, float radius, int hullType", false);

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
