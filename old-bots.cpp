//====== Copyright � 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: Physics simulation for non-havok/ipion objects
//
// $NoKeywords: $
//=============================================================================//
#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "player.h"
#include "physics_main.h"
#include "engine/server/server.h"
#include "engine/client/client.h"
#include "game/server/util_server.h"
#include "game/server/detour_impl.h"
#include "thirdparty/recast/DetourCrowd/Include/DetourPathCorridor.h"
#include "game/server/movehelper_server.h"
#include "r1/weapon_x.h"
#include "game/server/entitylist.h"
#include "game/shared/usercmd.h"
#include "public/game/shared/in_buttons.h"
#include "mathlib/mathlib.h"
#include "engine/debugoverlay.h"
#include "engine/enginetrace.h"
#include "public/gametrace.h"
#include "public/bspflags.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include <cstdlib>
#include <cmath>

// R5 uses bit 18 for melee (in_buttons.h has it wrong as IN_WALK)
#define IN_MELEE_R5 (1 << 18)

static ConVar sv_simulateBots("sv_simulateBots", "1", FCVAR_RELEASE, "Simulate user commands for bots on the server.");
static ConVar bot_nav_enable("bot_nav_enable", "1", FCVAR_RELEASE, "Enable bot navigation using navmesh.");
static ConVar bot_nav_goal_distance("bot_nav_goal_distance", "50.0", FCVAR_RELEASE, "Distance at which bot considers waypoint reached.");
static ConVar bot_nav_stuck_threshold("bot_nav_stuck_threshold", "15.0", FCVAR_RELEASE, "Distance bot must move to not be considered stuck.");
static ConVar bot_nav_turn_speed("bot_nav_turn_speed", "10.0", FCVAR_RELEASE, "How fast bots turn (degrees per frame multiplier).");
static ConVar bot_nav_use_marker("bot_nav_use_marker", "1", FCVAR_RELEASE, "Bots navigate to player-placed markers instead of random points.");
static ConVar bot_nav_halfextents("bot_nav_halfextents", "100.0", FCVAR_RELEASE, "Search box size for findNearestPoly (XY). Larger = finds polygons farther away.");
static ConVar bot_nav_path_options("bot_nav_path_options", "2", FCVAR_RELEASE, "findStraightPath options: 0=none, 1=AREA_CROSSINGS, 2=ALL_CROSSINGS (default), 3=both");
static ConVar bot_nav_traversal_jump("bot_nav_traversal_jump", "0", FCVAR_RELEASE, "0=smart (jump when climbing, walk when falling), 1=always jump in traversal mode");
static ConVar bot_nav_traverse_flags("bot_nav_traverse_flags", "0x13F", FCVAR_RELEASE, "Bitmask of allowed traverse types. 0x13F=human, 0x8013F=pilot, 0x33FFFF=max. 0=disable traversal.");

//-----------------------------------------------------------------------------
// Bot Navmesh Query - dedicated query object for NAVMESH_SMALL
//-----------------------------------------------------------------------------
static dtNavMeshQuery* g_botNavQuery = nullptr;

//-----------------------------------------------------------------------------
// Bot Data - stores pathfinding and combat state per bot
//-----------------------------------------------------------------------------
struct BotData
{
	// Navigation - using dtPathCorridor for proper navmesh following
	dtPathCorridor* corridor = nullptr;  // Path corridor - handles polygon traversal properly
	bool hasPath = false;
	Vector3D goalPosition;       // Final destination
	float nextPathfindTime;      // When to recalculate path

	// Steering corners from corridor (updated each frame)
	rdVec3D steerTarget;         // Current steering target
	unsigned char steerJumpType; // Jump type for steering target (0 = walk, else = traverse)
	float nextOptimizeTime;      // When to next optimize the path (visibility/topology)
	
	// Stuck detection
	Vector3D lastPosition;       // Position at last stuck check
	float stuckCheckTime;        // When to next check if stuck
	int stuckCounter;            // How many consecutive stuck checks

	// Smooth turning
	QAngle currentAngles;        // Current view angles (for interpolation)
	bool anglesInitialized;      // Whether we've grabbed initial angles

	// Jump/climb tracking
	float lastJumpTime;          // When we last pressed jump
	float lastJumpPosZ;          // Z position when we last jumped

	// Traversal mode - for falling/climbing when goal is lower
	bool inTraversalMode;        // Set when goal is lower and we need to walk off edge or climb
	bool hasJumpedForTraversal;  // Track if we already jumped in traversal mode

	// Active traverse tracking - prevents waypoint advancing mid-traverse
	bool inActiveTraverse;       // Currently executing a traverse (climb/gap)
	int activeTraverseType;      // Which traverse type (1=gap_small, 2=climb_obj, 3=gap_med, 8=climb_wall)
	float traverseStartTime;     // When traverse began (for timeout)
	Vector3D traverseTargetPos;  // The waypoint we're traversing to
	float traverseStartPosZ;     // Z position when traverse began (for climb verification)
	dtPolyRef traversePolyRef;   // The traverse link polygon we're crossing (for corridor update)

	// Door interaction
	enum DoorState { DOOR_IDLE = 0, DOOR_APPROACHING, DOOR_PRESSING_USE, DOOR_WAITING };
	DoorState doorState;
	float doorDetectTime;        // When we first detected this door (for timeout)
	float doorNextCheckTime;     // Throttle: next time to scan for doors
	float doorUseStartTime;      // When we started pressing USE
	float doorWaitUntilTime;     // When waiting period ends
	Vector3D doorPosition;       // Position of the detected door entity
	int doorAttemptCount;        // How many times we've tried this door (max 1 before giving up)
	edict_t doorEdict;           // Edict of the door entity for network-dirty marking

	// Loot bin interaction
	enum LootBinState { LOOTBIN_IDLE = 0, LOOTBIN_APPROACHING, LOOTBIN_USING, LOOTBIN_WAITING };
	LootBinState lootbinState;
	float lootbinDetectTime;     // When we started this loot bin interaction (for timeout)
	float lootbinWaitUntilTime;  // When waiting period ends
	Vector3D lootbinPosition;    // Position of the detected loot bin
	edict_t lootbinEdict;        // Edict of the loot bin entity
	bool lootbinCommandActive;   // True if bot was commanded to open a loot bin

	// Combat
	int targetPlayerIndex;       // Index of current enemy target (-1 = none)
	float targetAcquiredTime;    // When we first saw the target
	float lastFireTime;          // When we last fired
	bool hasTarget;              // Do we have a valid target?
	
	// Aim wobble/error
	float aimErrorX;             // Current random aim offset X
	float aimErrorY;             // Current random aim offset Y
	float nextAimErrorTime;      // When to recalculate aim error
	
	// Burst fire
	int burstShotsFired;         // Shots fired in current burst
	float burstPauseUntil;       // Time when burst pause ends
	
	void InitCorridor()
	{
		if (!corridor)
		{
			corridor = new dtPathCorridor();
			corridor->init(256);  // Max path length
		}
	}

	void Reset()
	{
		hasPath = false;
		nextPathfindTime = 0.0f;
		stuckCounter = 0;
		steerTarget = rdVec3D(0, 0, 0);
		steerJumpType = 0;
		nextOptimizeTime = 0.0f;
		inTraversalMode = false;
		hasJumpedForTraversal = false;
		inActiveTraverse = false;
		activeTraverseType = 0;
		traverseStartTime = 0.0f;
		traverseTargetPos = Vector3D(0, 0, 0);
		traverseStartPosZ = 0.0f;
		traversePolyRef = 0;
		doorState = DOOR_IDLE;
		doorDetectTime = 0.0f;
		doorNextCheckTime = 0.0f;
		doorUseStartTime = 0.0f;
		doorWaitUntilTime = 0.0f;
		doorPosition = Vector3D(0, 0, 0);
		doorEdict = FL_EDICT_INVALID;
		lootbinState = LOOTBIN_IDLE;
		lootbinDetectTime = 0.0f;
		lootbinWaitUntilTime = 0.0f;
		lootbinPosition = Vector3D(0, 0, 0);
		lootbinEdict = FL_EDICT_INVALID;
		lootbinCommandActive = false;
	}
	
	void ResetCombat()
	{
		targetPlayerIndex = -1;
		targetAcquiredTime = 0.0f;
		lastFireTime = 0.0f;
		hasTarget = false;
		aimErrorX = 0.0f;
		aimErrorY = 0.0f;
		nextAimErrorTime = 0.0f;
		burstShotsFired = 0;
		burstPauseUntil = 0.0f;
	}
	
	void FullReset()
	{
		Reset();
		ResetCombat();
		lastPosition = Vector3D(0, 0, 0);
		stuckCheckTime = 0.0f;
		currentAngles = QAngle(0, 0, 0);
		anglesInitialized = false;
		lastJumpTime = 0.0f;
		lastJumpPosZ = 0.0f;
		inTraversalMode = false;
		hasJumpedForTraversal = false;
		doorAttemptCount = 0;
	}
};

// Store bot data for up to 32 bots (indexed by client slot)
static BotData g_BotData[32];

//-----------------------------------------------------------------------------
// Marker system - player can place a marker for bots to navigate to
//-----------------------------------------------------------------------------
static Vector3D g_MarkerPosition(0, 0, 0);
static bool g_HasMarker = false;
static Vector3D g_SnappedGoalPosition(0, 0, 0);  // Where findNearestPoly actually snapped the goal
static bool g_HasSnappedGoal = false;

//-----------------------------------------------------------------------------
// Purpose: Normalize angle to -180 to 180 range
//-----------------------------------------------------------------------------
static float NormalizeAngle(float angle)
{
	while (angle > 180.0f) angle -= 360.0f;
	while (angle < -180.0f) angle += 360.0f;
	return angle;
}

//-----------------------------------------------------------------------------
// Purpose: Interpolate angles smoothly
//-----------------------------------------------------------------------------
static QAngle LerpAngles(const QAngle& from, const QAngle& to, float t)
{
	QAngle result;
	
	// Clamp t
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	
	// Interpolate each component, handling wraparound
	float deltaYaw = NormalizeAngle(to.y - from.y);
	float deltaPitch = NormalizeAngle(to.x - from.x);
	
	result.y = NormalizeAngle(from.y + deltaYaw * t);
	result.x = from.x + deltaPitch * t;
	result.z = 0.0f;
	
	return result;
}

//-----------------------------------------------------------------------------
// Purpose: Calculate angle to look at a target position
//-----------------------------------------------------------------------------
static QAngle CalcAngleToTarget(const Vector3D& from, const Vector3D& to)
{
	Vector3D delta = to - from;
	QAngle angles;
	VectorAngles(delta, angles);
	return angles;
}


//-----------------------------------------------------------------------------
// Purpose: Check if bot has line of sight to a target position
// Returns: true if there's a clear line of sight (no world geometry blocking)
//-----------------------------------------------------------------------------
static bool CanSeePosition(CPlayer* pPlayer, const Vector3D& targetPos)
{
	if (!g_pEngineTraceServer)
		return true; // Assume visible if we can't check

	// Start from bot's eye position
	Vector3D eyePos = pPlayer->GetAbsOrigin();
	eyePos.z += 60.0f; // Approximate eye height

	// Target the center of the loot bin (offset up a bit)
	Vector3D endPos = targetPos;
	endPos.z += 30.0f;

	Ray_t ray(eyePos, endPos);

	trace_t trace;
	memset(&trace, 0, sizeof(trace));

	g_pEngineTraceServer->TraceRay(ray, TRACE_MASK_NPCWORLDSTATIC, &trace);

	// If we hit something before reaching the target, no line of sight
	// fraction 1.0 = hit nothing, < 1.0 = hit something
	return trace.fraction >= 0.95f;
}


//-----------------------------------------------------------------------------
// Purpose: Ensure bot navmesh query is initialized for NAVMESH_SMALL
//-----------------------------------------------------------------------------
static void EnsureBotNavQueryInit()
{
	if (g_botNavQuery)
		return; // Already initialized

	dtNavMesh* nav = Detour_GetNavMeshByType(NAVMESH_SMALL);
	if (!nav)
	{
		DevMsg(eDLL_T::SERVER, "EnsureBotNavQueryInit: NAVMESH_SMALL not loaded\n");
		return;
	}

	g_botNavQuery = dtAllocNavMeshQuery();
	if (!g_botNavQuery)
	{
		DevMsg(eDLL_T::SERVER, "EnsureBotNavQueryInit: Failed to allocate query\n");
		return;
	}

	dtStatus status = g_botNavQuery->init(nav, 2048);
	if (dtStatusFailed(status))
	{
		DevMsg(eDLL_T::SERVER, "EnsureBotNavQueryInit: Failed to init query, status=0x%x\n", status);
		dtFreeNavMeshQuery(g_botNavQuery);
		g_botNavQuery = nullptr;
		return;
	}

	DevMsg(eDLL_T::SERVER, "Bot navmesh query initialized for NAVMESH_SMALL\n");
}

//-----------------------------------------------------------------------------
// Purpose: Find a random point on the navmesh
//-----------------------------------------------------------------------------
static bool FindRandomNavmeshPoint(const Vector3D& nearPos, Vector3D& outPos)
{
	EnsureBotNavQueryInit();

	dtNavMesh* nav = Detour_GetNavMeshByType(NAVMESH_SMALL);
	if (!nav || !g_botNavQuery)
		return false;
	
	// Find the polygon we're currently on
	dtQueryFilter filter;
	filter.setIncludeFlags(0xFFFF);
	filter.setExcludeFlags(0);
	
	rdVec3D center(nearPos.x, nearPos.y, nearPos.z);
	rdVec3D halfExtents(500.0f, 500.0f, 500.0f);  // Search radius
	
	dtPolyRef nearestRef = 0;
	rdVec3D nearestPt;
	
	dtStatus status = g_botNavQuery->findNearestPoly(&center, &halfExtents, &filter, &nearestRef, &nearestPt);
	
	if (dtStatusFailed(status) || nearestRef == 0)
		return false;
	
	// Pick a random direction and distance
	float angle = (float)(rand() % 360) * (3.14159265f / 180.0f);
	float distance = 500.0f + (float)(rand() % 1500);  // 500-2000 units away
	
	rdVec3D targetPos;
	targetPos.x = nearPos.x + cosf(angle) * distance;
	targetPos.y = nearPos.y + sinf(angle) * distance;
	targetPos.z = nearPos.z;
	
	// Find the nearest valid point on the navmesh
	dtPolyRef targetRef = 0;
	rdVec3D targetPt;
	
	status = g_botNavQuery->findNearestPoly(&targetPos, &halfExtents, &filter, &targetRef, &targetPt);
	
	if (dtStatusFailed(status) || targetRef == 0)
		return false;
	
	outPos.x = targetPt.x;
	outPos.y = targetPt.y;
	outPos.z = targetPt.z;
	
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Draw marker visualization using debug lines
//-----------------------------------------------------------------------------
static void DrawMarker()
{
	if (!g_HasMarker)
		return;

	if (!g_pDebugOverlay)
		return;

	// Draw a vertical line at REQUESTED marker position (CYAN)
	Vector3D markerBase = g_MarkerPosition;
	markerBase.z -= 10.0f;
	Vector3D markerTop = g_MarkerPosition;
	markerTop.z += 100.0f;

	g_pDebugOverlay->AddLineOverlay(markerBase, markerTop, 0, 255, 255, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

	// Draw a small cross at the base for visibility
	Vector3D crossH1 = g_MarkerPosition;
	crossH1.x -= 30.0f;
	Vector3D crossH2 = g_MarkerPosition;
	crossH2.x += 30.0f;
	g_pDebugOverlay->AddLineOverlay(crossH1, crossH2, 0, 255, 255, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

	Vector3D crossV1 = g_MarkerPosition;
	crossV1.y -= 30.0f;
	Vector3D crossV2 = g_MarkerPosition;
	crossV2.y += 30.0f;
	g_pDebugOverlay->AddLineOverlay(crossV1, crossV2, 0, 255, 255, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

	// Draw SNAPPED goal position if different (RED/ORANGE)
	if (g_HasSnappedGoal)
	{
		// Calculate distance between requested and snapped
		float snapDist = sqrtf(
			(g_SnappedGoalPosition.x - g_MarkerPosition.x) * (g_SnappedGoalPosition.x - g_MarkerPosition.x) +
			(g_SnappedGoalPosition.y - g_MarkerPosition.y) * (g_SnappedGoalPosition.y - g_MarkerPosition.y) +
			(g_SnappedGoalPosition.z - g_MarkerPosition.z) * (g_SnappedGoalPosition.z - g_MarkerPosition.z)
		);

		// Only draw if significantly different (more than 10 units apart)
		if (snapDist > 10.0f)
		{
			// Draw line from requested to snapped position
			g_pDebugOverlay->AddLineOverlay(g_MarkerPosition, g_SnappedGoalPosition, 255, 128, 0, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

			// Draw vertical line at snapped position (ORANGE)
			Vector3D snappedBase = g_SnappedGoalPosition;
			snappedBase.z -= 10.0f;
			Vector3D snappedTop = g_SnappedGoalPosition;
			snappedTop.z += 100.0f;
			g_pDebugOverlay->AddLineOverlay(snappedBase, snappedTop, 255, 128, 0, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

			// Draw cross at snapped position
			Vector3D snapCrossH1 = g_SnappedGoalPosition;
			snapCrossH1.x -= 30.0f;
			Vector3D snapCrossH2 = g_SnappedGoalPosition;
			snapCrossH2.x += 30.0f;
			g_pDebugOverlay->AddLineOverlay(snapCrossH1, snapCrossH2, 255, 128, 0, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

			Vector3D snapCrossV1 = g_SnappedGoalPosition;
			snapCrossV1.y -= 30.0f;
			Vector3D snapCrossV2 = g_SnappedGoalPosition;
			snapCrossV2.y += 30.0f;
			g_pDebugOverlay->AddLineOverlay(snapCrossV1, snapCrossV2, 255, 128, 0, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw bot paths for debugging
//-----------------------------------------------------------------------------
static void DrawBotPaths()
{
	if (!g_pDebugOverlay)
		return;

	// Draw paths for all bots
	for (int i = 0; i < g_ServerGlobalVariables->maxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient && pClient->IsActive() && pClient->IsFakeClient())
		{
			BotData& botData = g_BotData[i];

			if (!botData.hasPath || !botData.corridor || botData.corridor->getPathCount() < 1)
				continue;

			CPlayer* pPlayer = UTIL_PlayerByIndex(pClient->GetHandle());
			if (!pPlayer)
				continue;

			Vector3D botPos = pPlayer->GetAbsOrigin();

			// Skip if nav query not initialized
			if (!g_botNavQuery)
				continue;

			// Validate corridor state
			int pathCount = botData.corridor->getPathCount();
			if (pathCount <= 0 || pathCount > 256)
				continue;

			// Get steering corners from corridor for visualization
			dtQueryFilter vizFilter;
			vizFilter.setIncludeFlags(0xFFFF);
			vizFilter.setExcludeFlags(0);
			vizFilter.setTraverseFlags((unsigned int)bot_nav_traverse_flags.GetInt());

			rdVec3D cornerVerts[8];
			unsigned char cornerFlags[8];
			dtPolyRef cornerPolys[8];
			unsigned char cornerJumps[8];
			int numCorners = botData.corridor->findCorners(cornerVerts, cornerFlags, cornerPolys, cornerJumps, 8, g_botNavQuery, &vizFilter);

			// Draw line from bot to first corner (steering target)
			if (numCorners > 0)
			{
				Vector3D firstCorner(cornerVerts[0].x, cornerVerts[0].y, cornerVerts[0].z);
				g_pDebugOverlay->AddLineOverlay(botPos, firstCorner, 0, 255, 0, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

				// Draw lines between corners
				for (int j = 0; j < numCorners - 1; j++)
				{
					Vector3D start(cornerVerts[j].x, cornerVerts[j].y, cornerVerts[j].z);
					Vector3D end(cornerVerts[j + 1].x, cornerVerts[j + 1].y, cornerVerts[j + 1].z);
					g_pDebugOverlay->AddLineOverlay(start, end, 0, 255, 0, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);
				}

				// Draw spheres at corners - yellow for first (steering target), green for others
				for (int j = 0; j < numCorners; j++)
				{
					Vector3D corner(cornerVerts[j].x, cornerVerts[j].y, cornerVerts[j].z);
					if (j == 0)
						g_pDebugOverlay->AddSphereOverlay(corner, 20.0f, 8, 6, 255, 255, 0, 255, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);
					else
						g_pDebugOverlay->AddSphereOverlay(corner, 15.0f, 8, 6, 0, 255, 0, 255, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);
				}
			}

			// Draw goal in red
			g_pDebugOverlay->AddSphereOverlay(botData.goalPosition, 25.0f, 8, 6, 255, 0, 0, 255, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw bot view direction for debugging
//-----------------------------------------------------------------------------
static void DrawBotViewDirection()
{
	if (!g_pDebugOverlay)
		return;

	for (int i = 0; i < g_ServerGlobalVariables->maxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient && pClient->IsActive() && pClient->IsFakeClient())
		{
			BotData& botData = g_BotData[i];

			CPlayer* pPlayer = UTIL_PlayerByIndex(pClient->GetHandle());
			if (!pPlayer)
				continue;

			// Get eye position (origin + view offset)
			Vector3D eyePos = pPlayer->GetAbsOrigin() + pPlayer->GetViewOffset();

			// Convert current angles to forward vector
			Vector3D forward;
			AngleVectors(botData.currentAngles, &forward);

			// Draw line from eye position 200 units in look direction (RED)
			Vector3D lookEnd = eyePos + forward * 200.0f;
			g_pDebugOverlay->AddLineOverlay(eyePos, lookEnd, 255, 0, 0, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

			// Also get actual eye angles from player and draw in BLUE
			QAngle actualAngles;
			pPlayer->EyeAngles(&actualAngles);
			Vector3D actualForward;
			AngleVectors(actualAngles, &actualForward);
			Vector3D actualLookEnd = eyePos + actualForward * 200.0f;
			g_pDebugOverlay->AddLineOverlay(eyePos, actualLookEnd, 0, 0, 255, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

			// Debug: print actual vs internal angles
			static float nextAngleDebug = 0.0f;
			if (gpGlobals->curTime >= nextAngleDebug)
			{
				nextAngleDebug = gpGlobals->curTime + 0.5f;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Console command: Clear the navigation marker
//-----------------------------------------------------------------------------
void CC_ClearMarker_f(const CCommand& args)
{
	g_HasMarker = false;
	DevMsg(eDLL_T::SERVER, "Navigation marker cleared.\n");
}
static ConCommand bot_clear_marker("bot_clear_marker", CC_ClearMarker_f, "Clear the navigation marker", FCVAR_RELEASE);

//-----------------------------------------------------------------------------
// Purpose: Find a path from start to goal using the navmesh
//-----------------------------------------------------------------------------
static bool FindPath(const Vector3D& start, const Vector3D& goal, BotData& botData)
{
	EnsureBotNavQueryInit();

	dtNavMesh* nav = Detour_GetNavMeshByType(NAVMESH_SMALL);
	if (!nav || !g_botNavQuery)
	{
		static float nextDbg = 0.0f;
		if (gpGlobals->curTime >= nextDbg)
		{
			DevMsg(eDLL_T::SERVER, "FindPath: nav=%p, query=%p\n", nav, g_botNavQuery);
			nextDbg = gpGlobals->curTime + 2.0f;
		}
		return false;
	}

	dtQueryFilter filter;
	filter.setIncludeFlags(0xFFFF);
	filter.setExcludeFlags(0);

	// Enable traverse types for bot navigation (jumping, climbing, etc.)
	// Default 0x13F = ANIMTYPE_HUMAN (types 0-5, 8: small gaps, climbs, medium walls)
	// Higher values: 0x8013F = PILOT, 0x33FFFF = FRAG_DRONE (maximum traversal)
	// See docs/TRAVERSAL_PORTALS.md for details
	unsigned int traverseFlags = (unsigned int)bot_nav_traverse_flags.GetInt();
	filter.setTraverseFlags(traverseFlags);

	// Use configurable halfExtents - controls how far from target position to search for navmesh polygon
	float extentXY = bot_nav_halfextents.GetFloat();

	// FIX #11: Use consistent base extents, with fallback for edge cases
	// Start with reasonable vertical extent, increase if needed
	float extentZ = 100.0f;  // Reasonable default for both start and goal

	rdVec3D startExtents(extentXY, extentXY, extentZ);
	rdVec3D goalExtents(extentXY, extentXY, extentZ);

	// Find start polygon
	rdVec3D startPos(start.x, start.y, start.z);
	dtPolyRef startRef = 0;
	rdVec3D startNearest;

	dtStatus startStatus = g_botNavQuery->findNearestPoly(&startPos, &startExtents, &filter, &startRef, &startNearest);

	// FIX #11: If start fails, try with larger vertical extent (for slopes, ledges, etc.)
	if (dtStatusFailed(startStatus) || startRef == 0)
	{
		rdVec3D largerExtents(extentXY, extentXY, 200.0f);
		startStatus = g_botNavQuery->findNearestPoly(&startPos, &largerExtents, &filter, &startRef, &startNearest);
		if (dtStatusSucceed(startStatus) && startRef != 0)
		{
			DevMsg(eDLL_T::SERVER, "FindPath: start poly found with larger extent (200 units Z)\n");
		}
	}

	if (dtStatusFailed(startStatus) || startRef == 0)
	{
		static float nextDbg = 0.0f;
		if (gpGlobals->curTime >= nextDbg)
		{
			DevMsg(eDLL_T::SERVER, "FindPath FAILED: start poly not found\n");
			DevMsg(eDLL_T::SERVER, "  Requested: (%.1f, %.1f, %.1f)\n", start.x, start.y, start.z);
			DevMsg(eDLL_T::SERVER, "  Search extents: (%.1f, %.1f, %.1f)\n", startExtents.x, startExtents.y, startExtents.z);
			DevMsg(eDLL_T::SERVER, "  Status: 0x%x, PolyRef: %llu\n", startStatus, (unsigned long long)startRef);
			nextDbg = gpGlobals->curTime + 2.0f;
		}
		return false;
	}

	// Calculate distance from requested to snapped position
	float startSnapDist = sqrtf(
		(startNearest.x - start.x) * (startNearest.x - start.x) +
		(startNearest.y - start.y) * (startNearest.y - start.y) +
		(startNearest.z - start.z) * (startNearest.z - start.z)
	);

	// Find goal polygon
	rdVec3D goalPos(goal.x, goal.y, goal.z);
	dtPolyRef goalRef = 0;
	rdVec3D goalNearest;

	dtStatus goalStatus = g_botNavQuery->findNearestPoly(&goalPos, &goalExtents, &filter, &goalRef, &goalNearest);
	if (dtStatusFailed(goalStatus) || goalRef == 0)
	{
		static float nextDbg = 0.0f;
		if (gpGlobals->curTime >= nextDbg)
		{
			DevMsg(eDLL_T::SERVER, "FindPath FAILED: goal poly not found\n");
			DevMsg(eDLL_T::SERVER, "  Requested: (%.1f, %.1f, %.1f)\n", goal.x, goal.y, goal.z);
			DevMsg(eDLL_T::SERVER, "  Search extents: (%.1f, %.1f, %.1f)\n", goalExtents.x, goalExtents.y, goalExtents.z);
			DevMsg(eDLL_T::SERVER, "  Status: 0x%x, PolyRef: %llu\n", goalStatus, (unsigned long long)goalRef);
			DevMsg(eDLL_T::SERVER, "  TIP: Marker might be off navmesh. Try bot_nav_halfextents 150 or 200\n");
			nextDbg = gpGlobals->curTime + 2.0f;
		}
		return false;
	}

	// Calculate distance from requested to snapped position
	float goalSnapDist = sqrtf(
		(goalNearest.x - goal.x) * (goalNearest.x - goal.x) +
		(goalNearest.y - goal.y) * (goalNearest.y - goal.y) +
		(goalNearest.z - goal.z) * (goalNearest.z - goal.z)
	);

	// Store snapped goal position for debug visualization
	g_SnappedGoalPosition.x = goalNearest.x;
	g_SnappedGoalPosition.y = goalNearest.y;
	g_SnappedGoalPosition.z = goalNearest.z;
	g_HasSnappedGoal = true;

	// Log if snap distance is large (indicates marker far from navmesh)
	if (startSnapDist > 50.0f || goalSnapDist > 50.0f)
	{
		static float nextDbg = 0.0f;
		if (gpGlobals->curTime >= nextDbg)
		{
			DevMsg(eDLL_T::SERVER, "FindPath WARNING: Large snap distance\n");
			if (startSnapDist > 50.0f)
				DevMsg(eDLL_T::SERVER, "  Start snapped %.1f units from (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n",
					startSnapDist, start.x, start.y, start.z, startNearest.x, startNearest.y, startNearest.z);
			if (goalSnapDist > 50.0f)
				DevMsg(eDLL_T::SERVER, "  Goal snapped %.1f units from (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n",
					goalSnapDist, goal.x, goal.y, goal.z, goalNearest.x, goalNearest.y, goalNearest.z);
			nextDbg = gpGlobals->curTime + 2.0f;
		}
	}

	// Find path (polygon corridor)
	dtPolyRef pathPolys[256];
	unsigned char pathJumps[256];
	int pathPolyCount = 0;

	dtStatus status = g_botNavQuery->findPath(startRef, goalRef, &startNearest, &goalNearest, &filter, pathPolys, pathJumps, &pathPolyCount, 256);

	if (dtStatusFailed(status) || pathPolyCount == 0)
	{
		static float nextDbg = 0.0f;
		if (gpGlobals->curTime >= nextDbg)
		{
			DevMsg(eDLL_T::SERVER, "FindPath: findPath failed, status=0x%x, polyCount=%d\n", status, pathPolyCount);
			nextDbg = gpGlobals->curTime + 2.0f;
		}
		return false;
	}

	// Debug: log if path is suspiciously short
	if (pathPolyCount == 1)
	{
		float dist = (goal - start).Length();
		DevMsg(eDLL_T::SERVER, "FindPath WARNING: Only 1 polygon but dist=%.1f\n", dist);
		DevMsg(eDLL_T::SERVER, "  startRef=%llu, goalRef=%llu, same=%d\n",
			(unsigned long long)startRef, (unsigned long long)goalRef, startRef == goalRef ? 1 : 0);
		DevMsg(eDLL_T::SERVER, "  status=0x%x (partial=%d)\n", status, dtStatusInProgress(status) ? 1 : 0);
	}

	// Initialize corridor if needed
	botData.InitCorridor();

	// Set up the corridor properly:
	// 1. reset() sets m_pos (current position) - this gets overwritten path-wise but position is kept
	// 2. setCorridor() sets m_target and m_path[], overwriting the single-polygon from reset
	botData.corridor->reset(startRef, &startNearest);
	botData.corridor->setCorridor(&goalNearest, pathPolys, pathJumps, pathPolyCount);

	botData.goalPosition = goal;
	botData.hasPath = true;

	// Log path details
	const rdVec3D* corridorPos = botData.corridor->getPos();
	const rdVec3D* corridorTarget = botData.corridor->getTarget();
	DevMsg(eDLL_T::SERVER, "FindPath: %d polys in corridor, pathCount=%d\n", pathPolyCount, botData.corridor->getPathCount());
	DevMsg(eDLL_T::SERVER, "  Start: (%.1f, %.1f, %.1f) -> startNearest: (%.1f, %.1f, %.1f)\n",
		start.x, start.y, start.z, startNearest.x, startNearest.y, startNearest.z);
	DevMsg(eDLL_T::SERVER, "  Goal: (%.1f, %.1f, %.1f) -> goalNearest: (%.1f, %.1f, %.1f)\n",
		goal.x, goal.y, goal.z, goalNearest.x, goalNearest.y, goalNearest.z);
	DevMsg(eDLL_T::SERVER, "  Corridor pos: (%.1f, %.1f, %.1f), target: (%.1f, %.1f, %.1f)\n",
		corridorPos->x, corridorPos->y, corridorPos->z, corridorTarget->x, corridorTarget->y, corridorTarget->z);

	// Debug: show path polygon refs and jump types
	DevMsg(eDLL_T::SERVER, "  Path polys (startRef=%llu, goalRef=%llu):\n",
		(unsigned long long)startRef, (unsigned long long)goalRef);
	for (int i = 0; i < pathPolyCount && i < 10; i++)
	{
		DevMsg(eDLL_T::SERVER, "    [%d] poly=%llu, jump=%d\n",
			i, (unsigned long long)pathPolys[i], pathJumps[i]);
	}
	if (pathPolyCount > 10)
		DevMsg(eDLL_T::SERVER, "    ... (%d more)\n", pathPolyCount - 10);

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Get eye position for a player (origin + view offset)
//-----------------------------------------------------------------------------
static Vector3D GetPlayerEyePosition(CPlayer* pPlayer)
{
	return pPlayer->GetAbsOrigin() + pPlayer->GetViewOffset();
}

//-----------------------------------------------------------------------------
// Purpose: Get chest/center mass position for a player (for aiming)
//-----------------------------------------------------------------------------
static Vector3D GetPlayerChestPosition(CPlayer* pPlayer)
{
	Vector3D viewOffset = pPlayer->GetViewOffset();
	// Chest is roughly 70% of eye height
	Vector3D chestOffset(viewOffset.x, viewOffset.y, viewOffset.z * 0.7f);
	return pPlayer->GetAbsOrigin() + chestOffset;
}

//-----------------------------------------------------------------------------
// Console command: Place a marker where the player is looking
//-----------------------------------------------------------------------------
void CC_PlaceMarker_f(const CCommand& args)
{
	EnsureBotNavQueryInit();

	// Find the first real player (not a bot)
	CPlayer* pPlayer = nullptr;
	for (int i = 0; i < g_ServerGlobalVariables->maxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);
		if (pClient && pClient->IsActive() && !pClient->IsFakeClient())
		{
			pPlayer = UTIL_PlayerByIndex(pClient->GetHandle());
			break;
		}
	}

	if (!pPlayer)
	{
		DevMsg(eDLL_T::SERVER, "No player found to place marker.\n");
		return;
	}

	// Ensure the engine trace server vtable was resolved
	if (!g_pEngineTraceServerVFTable)
	{
		DevMsg(eDLL_T::SERVER, "Engine trace server not available.\n");
		return;
	}

	// Get player's eye position and view direction
	Vector3D eyePos = GetPlayerEyePosition(pPlayer);
	QAngle viewAngles;
	pPlayer->EyeAngles(&viewAngles);

	// Convert view angles to forward vector
	Vector3D forward;
	AngleVectors(viewAngles, &forward);

	// Create ray from eye position extending 10000 units forward
	Vector3D traceEnd = eyePos + forward * 10000.0f;
	Ray_t ray(eyePos, traceEnd);

	// Trace the ray against world geometry
	trace_t trace;
	memset(&trace, 0, sizeof(trace));
	g_pEngineTraceServer->TraceRay(ray, TRACE_MASK_NPCWORLDSTATIC, &trace);

	// Check if we hit something (fraction 0 = started solid, 1 = hit nothing)
	if (trace.fraction > 0.0f && trace.fraction < 1.0f)
	{
		// Compute hit position from ray: start + delta * fraction
		Vector3D hitPos;
		hitPos.x = eyePos.x + (traceEnd.x - eyePos.x) * trace.fraction;
		hitPos.y = eyePos.y + (traceEnd.y - eyePos.y) * trace.fraction;
		hitPos.z = eyePos.z + (traceEnd.z - eyePos.z) * trace.fraction;

		// IMPORTANT: Snap the hit position to nearest navmesh polygon
		// This ensures the marker is placed ON the navmesh, not on geometry that might be at a different height
		dtNavMesh* nav = Detour_GetNavMeshByType(NAVMESH_SMALL);
		if (nav && g_botNavQuery)
		{
			dtQueryFilter filter;
			filter.setIncludeFlags(0xFFFF);
			filter.setExcludeFlags(0);

			// Use VERY large search extents for marker placement - we want to find navmesh even if it's far from geometry
			// (Pathfinding uses smaller extents, but marker placement should be forgiving)
			rdVec3D halfExtents(500.0f, 500.0f, 500.0f);

			rdVec3D searchPos(hitPos.x, hitPos.y, hitPos.z);
			dtPolyRef nearestRef = 0;
			rdVec3D nearestPos;

			dtStatus status = g_botNavQuery->findNearestPoly(&searchPos, &halfExtents, &filter, &nearestRef, &nearestPos);

			if (!dtStatusFailed(status) && nearestRef != 0)
			{
				// Success - use the snapped navmesh position
				g_MarkerPosition.x = nearestPos.x;
				g_MarkerPosition.y = nearestPos.y;
				g_MarkerPosition.z = nearestPos.z;

				float snapDist = sqrtf(
					(nearestPos.x - hitPos.x) * (nearestPos.x - hitPos.x) +
					(nearestPos.y - hitPos.y) * (nearestPos.y - hitPos.y) +
					(nearestPos.z - hitPos.z) * (nearestPos.z - hitPos.z)
				);

				DevMsg(eDLL_T::SERVER, "Marker placed at (%.1f, %.1f, %.1f)\n", g_MarkerPosition.x, g_MarkerPosition.y, g_MarkerPosition.z);
				if (snapDist > 10.0f)
				{
					DevMsg(eDLL_T::SERVER, "  Snapped %.1f units from geometry (%.1f, %.1f, %.1f) to navmesh\n",
						snapDist, hitPos.x, hitPos.y, hitPos.z);
				}
			}
			else
			{
				// Failed to find navmesh - use geometry position anyway but warn user
				g_MarkerPosition = hitPos;
				DevMsg(eDLL_T::SERVER, "WARNING: Marker placed at (%.1f, %.1f, %.1f) but no navmesh found nearby!\n",
					g_MarkerPosition.x, g_MarkerPosition.y, g_MarkerPosition.z);
				DevMsg(eDLL_T::SERVER, "  Pathfinding will likely fail. Try placing marker on flat walkable ground.\n");
			}
		}
		else
		{
			// No navmesh available, use geometry position
			g_MarkerPosition = hitPos;
			DevMsg(eDLL_T::SERVER, "Marker placed at (%.1f, %.1f, %.1f) (navmesh not loaded)\n",
				g_MarkerPosition.x, g_MarkerPosition.y, g_MarkerPosition.z);
		}

		g_HasMarker = true;

		// Reset all bot paths so they immediately navigate to the new marker
		for (int i = 0; i < 32; i++)
		{
			g_BotData[i].Reset();
			g_BotData[i].nextPathfindTime = 0.0f;
		}
	}
	else
	{
		DevMsg(eDLL_T::SERVER, "Trace didn't hit anything.\n");
	}
}
static ConCommand bot_place_marker("bot_place_marker", CC_PlaceMarker_f, "Place a navigation marker where you are looking", FCVAR_RELEASE);

//-----------------------------------------------------------------------------
// Purpose: Run bot AI and generate movement command
//-----------------------------------------------------------------------------
static void RunBotNavigation(CPlayer* pPlayer, int clientIndex, CUserCmd& cmd)
{
	if (!bot_nav_enable.GetBool())
	{
		// Just walk forward if nav disabled
		cmd.forwardmove = 1.0f;
		cmd.buttons |= IN_FORWARD | IN_SPEED;
		return;
	}
	
	BotData& botData = g_BotData[clientIndex];
	Vector3D botPos = pPlayer->GetAbsOrigin();
	float currentTime = gpGlobals->curTime;
	float frameTime = gpGlobals->frameTime;
	
	// Initialize angles if needed
	if (!botData.anglesInitialized)
	{
		pPlayer->EyeAngles(&botData.currentAngles);
		botData.anglesInitialized = true;
		botData.lastPosition = botPos;
		botData.stuckCheckTime = currentTime + 0.5f;
	}
	
	// =========================================================================
	// STUCK DETECTION - Check if bot hasn't moved much
	// =========================================================================
	if (currentTime >= botData.stuckCheckTime)
	{
		float distMoved = (botPos - botData.lastPosition).Length2D();

		// Don't consider bot stuck if:
		// 1. Already climbing/wallclimbing (will naturally not move much horizontally)
		// 2. Close to goal (will naturally slow down near destination)
		float distToGoal = (botData.goalPosition - botPos).Length();
		bool isClimbing = pPlayer->IsWallClimbSetUp() || pPlayer->IsWallHanging();
		bool nearGoal = distToGoal < bot_nav_goal_distance.GetFloat() * 2.0f;  // Within 2x goal distance

		// Also skip stuck detection during active traverse
		bool inActiveTraverse = botData.inActiveTraverse;

		if (distMoved < bot_nav_stuck_threshold.GetFloat() && botData.hasPath && !isClimbing && !nearGoal
			&& botData.doorState == BotData::DOOR_IDLE && !inActiveTraverse)
		{
			botData.stuckCounter++;

			// Changed from >= 5 to >= 8 - give bot more time before resetting
			if (botData.stuckCounter >= 8)
			{
				// Very stuck - force a completely new path
				DevMsg(eDLL_T::SERVER, "BOT STUCK: Counter reached 8, forcing repath\n");
				botData.Reset();
				botData.nextPathfindTime = currentTime + 0.1f;  // Get new path immediately
				botData.stuckCounter = 0;
				botData.doorAttemptCount = 0;  // Allow re-attempting doors after full reset
			}
			// FIX #10: Smarter stuck recovery - only jump if it makes sense
			else if (botData.stuckCounter >= 5)
			{
				// Only jump if:
				// 1. Current steering target has a climb/traverse jump type, OR
				// 2. Goal is significantly above us
				bool shouldJump = false;
				bool shouldStrafe = false;

				// Check if we have a traverse waypoint
				if (botData.steerJumpType == 2 || botData.steerJumpType == 8)
				{
					// It's a climb waypoint - jumping might help
					shouldJump = true;
					DevMsg(eDLL_T::SERVER, "BOT STUCK: Jump to climb (counter=%d, jumpType=%d)\n",
						botData.stuckCounter, botData.steerJumpType);
				}
				else
				{
					// No climb waypoint - check height difference
					float heightDiffToGoal = botData.goalPosition.z - botPos.z;
					if (heightDiffToGoal > 40.0f)
					{
						shouldJump = true;
						DevMsg(eDLL_T::SERVER, "BOT STUCK: Jump toward higher goal (counter=%d, heightDiff=%.1f)\n",
							botData.stuckCounter, heightDiffToGoal);
					}
					else
					{
						// Goal isn't above us - maybe we're stuck on geometry
						// Try strafing to the side instead
						shouldStrafe = true;
						DevMsg(eDLL_T::SERVER, "BOT STUCK: Strafing to unstick (counter=%d)\n", botData.stuckCounter);
					}
				}

				if (shouldJump)
				{
					cmd.buttons |= IN_JUMP;
				}
				else if (shouldStrafe)
				{
					// Alternate strafe direction each stuck check
					cmd.sidemove = (botData.stuckCounter % 2 == 0) ? 1.0f : -1.0f;
				}
			}
		}
		else
		{
			// Not stuck, reset counter
			botData.stuckCounter = 0;
		}

		botData.lastPosition = botPos;
		botData.stuckCheckTime = currentTime + 0.5f;  // Check every 0.5 seconds
	}

	// Flag to skip normal view angle update (e.g., when looking at loot bin)
	bool overrideViewAngles = false;


	// =========================================================================
	// PATHFINDING - Get a new path if needed
	// =========================================================================
	// In traversal mode: only pathfind when timer expires (ignore hasPath check)
	// Not in traversal mode: pathfind if no path OR timer expired
	bool shouldPathfind = botData.inTraversalMode
		? (currentTime >= botData.nextPathfindTime)
		: (!botData.hasPath || currentTime >= botData.nextPathfindTime);

	if (shouldPathfind)
	{
		// Debug: Log why pathfinding is triggering
		static float nextDebugLog = 0.0f;
		if (currentTime >= nextDebugLog)
		{
			const char* reason = !botData.hasPath ? "no path" : "time expired";
			DevMsg(eDLL_T::SERVER, "Pathfinding triggered: %s (hasPath=%d, nextTime=%.1f, curTime=%.1f)\n",
				reason, botData.hasPath, botData.nextPathfindTime, currentTime);
			nextDebugLog = currentTime + 2.0f;  // Throttle debug spam
		}

		Vector3D goalPos;
		bool hasGoal = false;

		// Use marker position if enabled and available
		if (bot_nav_use_marker.GetBool())
		{
			if (g_HasMarker)
			{
				goalPos = g_MarkerPosition;
				hasGoal = true;
			}
			// else: marker mode is on but no marker placed - bot does nothing
		}
		else
		{
			// Marker mode disabled - use random navigation
			hasGoal = FindRandomNavmeshPoint(botPos, goalPos);
		}

		if (hasGoal)
		{
			// Check if we're already at the goal - don't pathfind if we're close enough
			float distToGoal = (goalPos - botPos).Length2D();
			if (distToGoal < bot_nav_goal_distance.GetFloat() * 1.5f)  // 1.5x tolerance
			{
				// Already at goal - stop pathfinding, exit traversal mode, and stay put
				// Only reset pathfinding state if we weren't already in this "at goal" state
				bool wasAtGoal = !botData.hasPath && !botData.inTraversalMode && (botData.nextPathfindTime > currentTime);

				botData.hasPath = false;
				botData.inTraversalMode = false;
				botData.hasJumpedForTraversal = false;
				botData.nextPathfindTime = currentTime + 5.0f;  // Check again in 5 seconds in case goal moves

				// Only log when first reaching goal, not every frame
				if (!wasAtGoal)
				{
					DevMsg(eDLL_T::SERVER, "Bot reached goal (dist=%.1f), stopping pathfinding.\n", distToGoal);
				}
			}
			else if (FindPath(botPos, goalPos, botData))
			{
				botData.nextPathfindTime = currentTime + 15.0f;  // Recalc every 15 seconds
				botData.stuckCounter = 0;  // Reset stuck counter on new path
				botData.doorAttemptCount = 0;  // Allow door attempts on new path
				// Exit traversal mode when we get a new valid path
				botData.inTraversalMode = false;
				botData.hasJumpedForTraversal = false;
			}
			else
			{
				// Pathfinding failed - if in traversal mode, stay in it, otherwise reset
				if (!botData.inTraversalMode)
				{
					botData.Reset();
				}
				botData.nextPathfindTime = currentTime + 1.0f;  // Try again in 1 second
			}
		}
		else
		{
			botData.Reset();
			botData.nextPathfindTime = currentTime + 1.0f;
		}
	}
	
	// =========================================================================
	// PATH FOLLOWING - Using dtPathCorridor for proper navmesh navigation
	// =========================================================================
	if (botData.hasPath && botData.corridor && botData.corridor->getPathCount() > 0)
	{
		// Create filter for corridor operations
		dtQueryFilter corridorFilter;
		corridorFilter.setIncludeFlags(0xFFFF);
		corridorFilter.setExcludeFlags(0);
		corridorFilter.setTraverseFlags((unsigned int)bot_nav_traverse_flags.GetInt());

		// Update corridor with current bot position
		rdVec3D botPosRd(botPos.x, botPos.y, botPos.z);

		// Check if we just completed a traverse (climb/jump)
		// This is needed because movePosition can't follow traverse links - it only moves along surfaces
		if (botData.inActiveTraverse)
		{
			bool isClimbing = pPlayer->IsWallClimbSetUp() || pPlayer->IsWallHanging();
			bool onGround = pPlayer->IsOnGround();
			Vector3D velocity = pPlayer->GetVelocity();

			float distToTraverseTarget = (botData.traverseTargetPos - botPos).Length2D();
			float heightToTraverseTarget = botPos.z - botData.traverseTargetPos.z;
			float traverseTime = currentTime - botData.traverseStartTime;

			// FIX #3: Improved traverse completion detection
			// Use multiple criteria for more reliable detection
			bool traverseComplete = false;

			// Minimum time before checking completion (prevents early trigger)
			if (traverseTime > 0.3f)
			{
				// Stable on ground: not just touching, but actually settled
				bool stableOnGround = onGround && fabsf(velocity.z) < 50.0f;

				// Close to destination
				bool atDestination = distToTraverseTarget < 80.0f && heightToTraverseTarget > -20.0f;

				// For wall climbs, verify we actually gained height
				bool isWallClimbType = (botData.activeTraverseType == 2 || botData.activeTraverseType == 8);
				if (isWallClimbType)
				{
					float heightGained = botPos.z - botData.traverseStartPosZ;
					bool gainedEnoughHeight = heightGained > 30.0f;

					// Wall climb complete if: stable, at destination, and gained height
					// For fallback climbs, the target height is much higher than a single jump
					// So require gaining most of the target height, not just 30 units
					float targetHeight = botData.traverseTargetPos.z - botData.traverseStartPosZ;
					bool gainedMostHeight = (targetHeight < 50.0f) || (heightGained > targetHeight * 0.7f);

					traverseComplete = stableOnGround && !isClimbing && atDestination && gainedEnoughHeight && gainedMostHeight;

					// Debug: why did/didn't it complete?
					static float nextCompleteDbg = 0.0f;
					if (currentTime >= nextCompleteDbg)
					{
						DevMsg(eDLL_T::SERVER, "TRAVERSE CHECK: time=%.1f, onGround=%d, climbing=%d, dist=%.1f, heightGained=%.1f/%.1f, atDest=%d, complete=%d\n",
							traverseTime, stableOnGround ? 1 : 0, isClimbing ? 1 : 0,
							distToTraverseTarget, heightGained, targetHeight, atDestination ? 1 : 0, traverseComplete ? 1 : 0);
						nextCompleteDbg = currentTime + 0.3f;
					}

					if (!traverseComplete && traverseTime > 1.0f)
					{
						// Alternative: if we're on ground and close, even without height gain (ledge grab)
						traverseComplete = stableOnGround && !isClimbing && distToTraverseTarget < 60.0f;
					}
				}
				else
				{
					// Gap crossing: need to be stable and at destination
					// For drops (target below start), verify we actually descended
					float heightDropped = botData.traverseStartPosZ - botPos.z;
					float targetDrop = botData.traverseStartPosZ - botData.traverseTargetPos.z;

					// If this is a drop (target significantly below start), require we dropped most of the way
					bool isDropTraverse = targetDrop > 50.0f;
					bool droppedEnough = !isDropTraverse || heightDropped > targetDrop * 0.5f;

					traverseComplete = stableOnGround && atDestination && droppedEnough;
				}
			}

			// Timeout fallback (5 seconds)
			if (traverseTime > 5.0f)
			{
				DevMsg(eDLL_T::SERVER, "TRAVERSE TIMEOUT: forcing complete after %.1fs\n", traverseTime);
				traverseComplete = true;
			}

			if (traverseComplete)
			{
				DevMsg(eDLL_T::SERVER, "TRAVERSE COMPLETE: dist=%.1f, height=%.1f, time=%.1f, heightGained=%.1f\n",
					distToTraverseTarget, heightToTraverseTarget, traverseTime,
					botPos.z - botData.traverseStartPosZ);

				// FIX #1/#2: Use stored traversePolyRef instead of getFirstPoly()
				// getFirstPoly() returns the current first polygon, which may have changed
				// during the traverse. We stored the traverse link polygon when starting.
				dtPolyRef traversePoly = botData.traversePolyRef;

				// Validate that the stored poly ref is still in the corridor
				bool polyStillValid = false;
				if (traversePoly != 0)
				{
					const dtPolyRef* path = botData.corridor->getPath();
					int pathCount = botData.corridor->getPathCount();
					for (int i = 0; i < pathCount; i++)
					{
						if (path[i] == traversePoly)
						{
							polyStillValid = true;
							break;
						}
					}
				}

				if (polyStillValid)
				{
					dtPolyRef refs[2];
					rdVec3D startPos, endPos;
					bool moved = botData.corridor->moveOverTraversePortal(traversePoly, &botPosRd, refs, &startPos, &endPos, g_botNavQuery);
					if (moved)
					{
						DevMsg(eDLL_T::SERVER, "  Corridor updated via moveOverTraversePortal (storedRef=%llu), polys now: %d\n",
							(unsigned long long)traversePoly, botData.corridor->getPathCount());
					}
					else
					{
						DevMsg(eDLL_T::SERVER, "  moveOverTraversePortal failed for storedRef=%llu, using fallback\n",
							(unsigned long long)traversePoly);

						// Fallback: fix the corridor start to current position
						rdVec3D extents(200.0f, 200.0f, 200.0f);
						dtPolyRef nearestRef = 0;
						rdVec3D nearestPos;
						dtStatus findStatus = g_botNavQuery->findNearestPoly(&botPosRd, &extents, &corridorFilter, &nearestRef, &nearestPos);
						if (dtStatusSucceed(findStatus) && nearestRef != 0)
						{
							botData.corridor->fixPathStart(nearestRef, 0, &botPosRd);
							DevMsg(eDLL_T::SERVER, "  Corridor fixed via fixPathStart to poly %llu\n", (unsigned long long)nearestRef);
						}
					}
				}
				else
				{
					// No stored poly ref or stale ref - fix corridor to current position
					if (traversePoly != 0)
						DevMsg(eDLL_T::SERVER, "  Stored traversePolyRef=%llu no longer in corridor, using fallback\n", (unsigned long long)traversePoly);
					else
						DevMsg(eDLL_T::SERVER, "  No stored traversePolyRef, using fixPathStart fallback\n");

					rdVec3D extents(200.0f, 200.0f, 200.0f);
					dtPolyRef nearestRef = 0;
					rdVec3D nearestPos;
					dtStatus findStatus = g_botNavQuery->findNearestPoly(&botPosRd, &extents, &corridorFilter, &nearestRef, &nearestPos);
					if (dtStatusSucceed(findStatus) && nearestRef != 0)
					{
						botData.corridor->fixPathStart(nearestRef, 0, &botPosRd);
						DevMsg(eDLL_T::SERVER, "  Corridor fixed via fixPathStart to poly %llu\n", (unsigned long long)nearestRef);
					}
				}

				botData.inActiveTraverse = false;
				botData.traversePolyRef = 0;  // Clear stored ref
			}
		}

		int pathCountBefore = botData.corridor->getPathCount();
		bool moveResult = botData.corridor->movePosition(&botPosRd, g_botNavQuery, &corridorFilter);
		int pathCountAfter = botData.corridor->getPathCount();

		// If movePosition failed, the bot might be off the navmesh - try to recover
		if (!moveResult)
		{
			// Find nearest polygon to bot's current position
			rdVec3D extents(200.0f, 200.0f, 200.0f);
			dtPolyRef nearestRef = 0;
			rdVec3D nearestPos;
			dtStatus findStatus = g_botNavQuery->findNearestPoly(&botPosRd, &extents, &corridorFilter, &nearestRef, &nearestPos);

			if (dtStatusSucceed(findStatus) && nearestRef != 0)
			{
				// Fix the corridor start to this polygon
				botData.corridor->fixPathStart(nearestRef, 0, &nearestPos);
			}
		}

		// FIX #9: Periodically optimize path for smoother navigation
		// This allows bots to cut corners via line-of-sight instead of following exact polygon edges
		if (currentTime >= botData.nextOptimizeTime && !botData.inActiveTraverse)
		{
			botData.nextOptimizeTime = currentTime + 0.5f;  // Every 0.5 seconds

			// Visibility optimization: skip corners we can see past
			float optimizationRange = bot_nav_goal_distance.GetFloat() * 4.0f;
			const rdVec3D* target = botData.corridor->getTarget();
			botData.corridor->optimizePathVisibility(target, optimizationRange, g_botNavQuery, &corridorFilter);

			// Topology optimization: local area replanning
			botData.corridor->optimizePathTopology(g_botNavQuery, &corridorFilter);
		}

		// Validate corridor state before using
		int corridorPathCount = botData.corridor->getPathCount();
		if (corridorPathCount <= 0 || corridorPathCount > 256)  // 256 is our init size
		{
			Warning(eDLL_T::SERVER, "Corridor in invalid state (pathCount=%d), resetting\n", corridorPathCount);
			botData.hasPath = false;
			botData.inActiveTraverse = false;
			return;
		}

		// Get steering corners from corridor
		static const int MAX_CORNERS = 8;
		rdVec3D cornerVerts[MAX_CORNERS];
		unsigned char cornerFlags[MAX_CORNERS];
		dtPolyRef cornerPolys[MAX_CORNERS];
		unsigned char cornerJumps[MAX_CORNERS];
		int numCorners = botData.corridor->findCorners(cornerVerts, cornerFlags, cornerPolys, cornerJumps, MAX_CORNERS, g_botNavQuery, &corridorFilter);

		// Debug: log corridor state
		const rdVec3D* corridorPos = botData.corridor->getPos();
		const rdVec3D* corridorTarget = botData.corridor->getTarget();
		static float nextCorridorDbg = 0.0f;
		if (currentTime >= nextCorridorDbg)
		{
			DevMsg(eDLL_T::SERVER, "Corridor: movePos=%d, polys %d->%d, corners=%d\n",
				moveResult ? 1 : 0, pathCountBefore, pathCountAfter, numCorners);
			DevMsg(eDLL_T::SERVER, "  Bot pos: (%.1f, %.1f, %.1f)\n", botPos.x, botPos.y, botPos.z);
			DevMsg(eDLL_T::SERVER, "  Corridor pos: (%.1f, %.1f, %.1f)\n", corridorPos->x, corridorPos->y, corridorPos->z);
			DevMsg(eDLL_T::SERVER, "  Corridor target: (%.1f, %.1f, %.1f)\n", corridorTarget->x, corridorTarget->y, corridorTarget->z);
			if (numCorners > 0)
			{
				DevMsg(eDLL_T::SERVER, "  Corner[0]: (%.1f, %.1f, %.1f) jump=%d\n",
					cornerVerts[0].x, cornerVerts[0].y, cornerVerts[0].z, cornerJumps[0]);
			}
			nextCorridorDbg = currentTime + 1.0f;
		}

		// Default: steer toward goal if no corners
		Vector3D steerTarget = botData.goalPosition;
		float goalDist = bot_nav_goal_distance.GetFloat();

		// Traverse info from LAST corner (traverse links are pruned to end of corner list)
		unsigned char traverseJumpType = 0;
		Vector3D traverseTarget = botData.goalPosition;
		dtPolyRef traverseCornerPoly = 0;

		if (numCorners > 0)
		{
			// First corner is the immediate steering target (where to walk)
			steerTarget = Vector3D(cornerVerts[0].x, cornerVerts[0].y, cornerVerts[0].z);

			// LAST corner has the traverse info (findCorners prunes after traverse links)
			int lastCornerIdx = numCorners - 1;
			traverseJumpType = cornerJumps[lastCornerIdx];
			traverseTarget = Vector3D(cornerVerts[lastCornerIdx].x, cornerVerts[lastCornerIdx].y, cornerVerts[lastCornerIdx].z);
			traverseCornerPoly = cornerPolys[lastCornerIdx];
		}

		// Distances for steering
		float distToSteer = (steerTarget - botPos).Length2D();
		float heightToSteer = steerTarget.z - botPos.z;

		// Distances for traverse detection (to the traverse link, not steering target)
		float distToTraverse = (traverseTarget - botPos).Length2D();
		float heightToTraverse = traverseTarget.z - botPos.z;

		// FIX #7: Corner skipping - kept as safety fallback
		// With fixes #1-3 for corridor tracking, this should rarely trigger.
		// If we're very close to a corner but can't reach it (geometry blocking),
		// skip to the next corner to prevent getting stuck.
		if (numCorners > 1 && distToSteer < goalDist && fabsf(heightToSteer) < 50.0f)
		{
			static float nextSkipDbg = 0.0f;
			if (currentTime >= nextSkipDbg)
			{
				DevMsg(eDLL_T::SERVER, "CORNER SKIP: Close but unreachable (dist=%.1f, height=%.1f)\n",
					distToSteer, heightToSteer);
				nextSkipDbg = currentTime + 1.0f;
			}
			// Move to next corner for steering
			steerTarget = Vector3D(cornerVerts[1].x, cornerVerts[1].y, cornerVerts[1].z);
			distToSteer = (steerTarget - botPos).Length2D();
			heightToSteer = steerTarget.z - botPos.z;
		}

		// Check if we reached the goal
		float distToGoal = (botData.goalPosition - botPos).Length();
		if (distToGoal < goalDist * 1.5f)
		{
			DevMsg(eDLL_T::SERVER, "Bot reached goal (dist=%.1f)\n", distToGoal);
			botData.hasPath = false;
			botData.inActiveTraverse = false;
			botData.nextPathfindTime = currentTime + 5.0f;
			return;
		}

		// Check if corridor path is exhausted but we're not at goal
		// Note: pathCount == 1 is valid if bot and goal are on the same polygon - just walk to goal
		// Only consider "exhausted" if we have no corners AND we're far from goal
		bool corridorExhausted = (numCorners == 0 && botData.corridor->getPathCount() == 0);

		// Also exhausted if pathCount is 1, no corners, and we're still far from goal
		// (This catches the case where findCorners failed to return the goal as a corner)
		if (!corridorExhausted && numCorners == 0 && botData.corridor->getPathCount() == 1 && distToGoal > goalDist * 3.0f)
		{
			corridorExhausted = true;
		}

		if (corridorExhausted)
		{
			float goalHeightDiff = botData.goalPosition.z - botPos.z;

			if (fabsf(goalHeightDiff) > 20.0f && distToGoal > 50.0f)
			{
				// FIX #8: This is a fallback mode - log as warning since it indicates
				// the corridor failed to properly track a traverse link
				Warning(eDLL_T::SERVER, "BOT FALLBACK: Entering traversal mode (dist=%.1f, height=%.1f)\n",
					distToGoal, goalHeightDiff);
				Warning(eDLL_T::SERVER, "  This may indicate a traverse link wasn't properly handled\n");
				botData.hasPath = false;
				botData.inActiveTraverse = false;
				botData.inTraversalMode = true;
				botData.hasJumpedForTraversal = false;
				botData.nextPathfindTime = currentTime + 1.0f;
				return;
			}
			else
			{
				DevMsg(eDLL_T::SERVER, "Corridor exhausted but not at goal (dist=%.1f), repathfinding\n", distToGoal);
				botData.hasPath = false;
				botData.inActiveTraverse = false;
				botData.nextPathfindTime = currentTime + 0.5f;
				return;
			}
		}

		// Calculate desired angle to steering target
		QAngle targetAngles = CalcAngleToTarget(botPos, steerTarget);
		
		// =====================================================================
		// SMOOTH TURNING - Interpolate towards target angle
		// =====================================================================
		float turnSpeed = bot_nav_turn_speed.GetFloat() * frameTime;
		
		// Faster turning when target is far off
		float angleDiff = fabsf(NormalizeAngle(targetAngles.y - botData.currentAngles.y));
		if (angleDiff > 45.0f)
		{
			turnSpeed *= 2.0f;  // Turn faster when way off course
		}

		// Only update view angles if not overridden (e.g., by loot bin look-at)
		// Also skip smooth turning if actively climbing - we don't want to turn while on a wall
		bool isActivelyClimbing = pPlayer->IsWallClimbSetUp() || pPlayer->IsWallHanging();
		if (!overrideViewAngles && !isActivelyClimbing)
		{
			botData.currentAngles = LerpAngles(botData.currentAngles, targetAngles, turnSpeed);
			cmd.viewangles = botData.currentAngles;
		}
		
		// =====================================================================
		// TRAVERSE HANDLING - Type-aware jump/climb logic
		// =====================================================================
		// Supported HUMAN traverse types (0x13F):
		//   Type 1: CROSS_GAP_SMALL    (0-120 dist, 0-48 elev)   - small hop
		//   Type 2: CLIMB_OBJECT_SMALL (120-160 dist, 48-96 elev) - climb crate/rock
		//   Type 3: CROSS_GAP_MEDIUM   (160-220 dist, 0-128 elev) - medium hop
		//   Type 8: CLIMB_WALL_MEDIUM  (70-220 dist, 48-220 elev) - wall climb
		// =====================================================================
		bool needsTraverse = false;  // Does the target require special movement?
		bool isWallClimb = false;    // Is this a wall climb (vs gap crossing)?
		bool shouldJumpNow = false;  // Should we press jump THIS frame?

		// FIX #4: If already in an active traverse, don't re-evaluate conditions
		// This prevents state flicker when distances change during the traverse
		if (botData.inActiveTraverse)
		{
			// Already executing a traverse - maintain state until completion
			needsTraverse = true;
			isWallClimb = (botData.activeTraverseType == 2 || botData.activeTraverseType == 8);
		}
		// FIX #13: Check LAST corner for traverse type (findCorners prunes after traverse links)
		// traverseJumpType is from cornerJumps[numCorners-1], not cornerJumps[0]
		else if (traverseJumpType != 0 && traverseJumpType != 255)
		{
			// Type-specific handling based on traverse table parameters
			switch (traverseJumpType)
			{
			case 1:  // CROSS_GAP_SMALL - 0-120 dist, 0-48 elev
				// Small gaps - trigger close to edge, works even if flat
				if (distToTraverse < 130.0f)
				{
					DevMsg(eDLL_T::SERVER, "TRAVERSE: Gap small (type=%d, dist=%.1f, height=%.1f)\n",
						traverseJumpType, distToTraverse, heightToTraverse);
					needsTraverse = true;
					isWallClimb = false;
				}
				break;

			case 2:  // CLIMB_OBJECT_SMALL - 120-160 dist, 48-96 elev
				// Climbing objects - need some height difference
				if (distToTraverse < 180.0f && heightToTraverse > 20.0f)
				{
					DevMsg(eDLL_T::SERVER, "TRAVERSE: Object climb (type=%d, dist=%.1f, height=%.1f)\n",
						traverseJumpType, distToTraverse, heightToTraverse);
					needsTraverse = true;
					isWallClimb = true;
				}
				break;

			case 3:  // CROSS_GAP_MEDIUM - 160-220 dist, 0-128 elev
				// Medium gaps - trigger at appropriate distance
				if (distToTraverse < 240.0f)
				{
					DevMsg(eDLL_T::SERVER, "TRAVERSE: Gap medium (type=%d, dist=%.1f, height=%.1f)\n",
						traverseJumpType, distToTraverse, heightToTraverse);
					needsTraverse = true;
					isWallClimb = false;
				}
				break;

			case 8:  // CLIMB_WALL_MEDIUM - 70-220 dist, 48-220 elev
				// Wall climbs - need height difference and be close to wall
				if (distToTraverse < 240.0f && heightToTraverse > 30.0f)
				{
					DevMsg(eDLL_T::SERVER, "TRAVERSE: Wall climb (type=%d, dist=%.1f, height=%.1f)\n",
						traverseJumpType, distToTraverse, heightToTraverse);
					needsTraverse = true;
					isWallClimb = true;
				}
				break;

			default:
				// Unknown type - use conservative fallback (only if going up)
				if (distToTraverse < 200.0f && heightToTraverse > 30.0f)
				{
					DevMsg(eDLL_T::SERVER, "TRAVERSE: Unknown type (type=%d, dist=%.1f, height=%.1f)\n",
						traverseJumpType, distToTraverse, heightToTraverse);
					needsTraverse = true;
					isWallClimb = (heightToTraverse > 60.0f);  // Guess based on height
				}
				break;
			}
		}
		// FIX #6: Fallback height-based climbing - only trigger when actually blocked
		// This prevents false positives on stairs and ramps
		// Note: When traverseJumpType is 255 (DT_NULL_TRAVERSE_TYPE), there's no traverse link
		// in the navmesh, so we need a more aggressive fallback for wall climbs
		else if (!botData.inActiveTraverse)
		{
			// Only use fallback if we seem blocked (not making horizontal progress)
			Vector3D velocity = pPlayer->GetVelocity();
			float horizontalSpeed = Vector3D(velocity.x, velocity.y, 0).Length();
			bool seemsBlocked = horizontalSpeed < 20.0f && botData.stuckCounter >= 2;

			// Height difference to goal (for fallback when no traverse link)
			float heightDiffToGoal = botData.goalPosition.z - botPos.z;

			if (seemsBlocked && heightDiffToGoal > 40.0f)
			{
				// No traverse link but goal is above and we're stuck - try wall climb
				// This handles cases where navmesh is missing traverse links
				DevMsg(eDLL_T::SERVER, "TRAVERSE: Fallback wall climb (no traverse link, goalHeight=%.1f, blocked)\n", heightDiffToGoal);
				needsTraverse = true;
				isWallClimb = true;
				traverseJumpType = 8;  // Treat as CLIMB_WALL_MEDIUM
				// Set traverse target to where we're trying to go (above current pos)
				traverseTarget = Vector3D(botPos.x, botPos.y, botPos.z + 100.0f);
				distToTraverse = 50.0f;  // Pretend we're close to trigger jump
				heightToTraverse = heightDiffToGoal;
			}
			// If not blocked, just walk normally - it's probably a ramp/stairs
		}

		// Store traverse type for stuck detection (which runs earlier in frame)
		botData.steerJumpType = traverseJumpType;

		// Decide if we should press jump this frame
		if (needsTraverse)
		{
			bool isClimbing = pPlayer->IsWallClimbSetUp() || pPlayer->IsWallHanging();
			float verticalVelocity = pPlayer->GetVelocity().z;
			bool movingUp = verticalVelocity > 50.0f;
			float timeSinceJump = currentTime - botData.lastJumpTime;

			// FIX #5: Use different cooldowns based on traverse type and state
			// Wall climbs take 1-2 seconds, so use longer cooldown to prevent double-jump
			float jumpCooldown = 0.5f;  // Default
			if (botData.inActiveTraverse && (botData.activeTraverseType == 2 || botData.activeTraverseType == 8))
			{
				jumpCooldown = 2.5f;  // Wall climbs - don't jump again until traverse completes
			}

			bool recentlyJumped = timeSinceJump < jumpCooldown;

			// FIX #5: Never try to jump again if actively climbing
			if (isClimbing)
			{
				recentlyJumped = true;  // Effectively blocks jump
			}

			if (isWallClimb)
			{
				// Wall climb: jump when close to the wall
				// Distance to traverse target is horizontal - target is above the wall
				// Use 100 units as trigger since target may be past the wall top
				float jumpTriggerDist = 100.0f;

				// Check if blocked (not moving despite trying) - indicates at wall
				Vector3D velocity = pPlayer->GetVelocity();
				float horizontalSpeed = Vector3D(velocity.x, velocity.y, 0).Length();
				bool seemsBlocked = horizontalSpeed < 30.0f && !isClimbing;

				// Jump if: close enough AND (very close OR seems blocked at wall)
				bool closeEnough = distToTraverse < jumpTriggerDist;
				bool shouldTryJump = closeEnough && (distToTraverse < 60.0f || seemsBlocked);

				if (!isClimbing && !movingUp && !recentlyJumped && shouldTryJump)
				{
					DevMsg(eDLL_T::SERVER, "TRAVERSE JUMP: Wall climb (dist=%.1f, blocked=%d, hspeed=%.1f)\n",
						distToTraverse, seemsBlocked ? 1 : 0, horizontalSpeed);
					shouldJumpNow = true;
					botData.lastJumpTime = currentTime;
					botData.lastJumpPosZ = botPos.z;

					// Start tracking this traverse
					botData.inActiveTraverse = true;
					botData.activeTraverseType = traverseJumpType;
					botData.traverseStartTime = currentTime;
					botData.traverseTargetPos = traverseTarget;

					// FIX #1/#2/#13: Store traverse polygon ref from LAST corner
					botData.traverseStartPosZ = botPos.z;
					botData.traversePolyRef = traverseCornerPoly;
					DevMsg(eDLL_T::SERVER, "  Stored traversePolyRef=%llu, startZ=%.1f\n",
						(unsigned long long)botData.traversePolyRef, botData.traverseStartPosZ);
				}
				else if (needsTraverse && !isClimbing && !recentlyJumped)
				{
					// Debug: show why we're not jumping yet
					static float nextWallDebug = 0.0f;
					if (currentTime >= nextWallDebug)
					{
						DevMsg(eDLL_T::SERVER, "WALL APPROACH: dist=%.1f (need <%.1f), hspeed=%.1f, blocked=%d\n",
							distToTraverse, jumpTriggerDist, horizontalSpeed, seemsBlocked ? 1 : 0);
						nextWallDebug = currentTime + 0.3f;
					}
				}
			}
			else
			{
				// Gap crossing: jump when close to edge
				// Use shorter cooldown for gaps since we might need to hop again
				bool recentlyJumpedGap = timeSinceJump < 0.3f;
				if (!movingUp && !recentlyJumpedGap && distToTraverse < 100.0f)
				{
					DevMsg(eDLL_T::SERVER, "TRAVERSE JUMP: Gap cross (dist=%.1f, movingUp=%d)\n", distToTraverse, movingUp);
					shouldJumpNow = true;
					botData.lastJumpTime = currentTime;
					botData.lastJumpPosZ = botPos.z;

					// Start tracking this traverse
					botData.inActiveTraverse = true;
					botData.activeTraverseType = traverseJumpType;
					botData.traverseStartTime = currentTime;
					botData.traverseTargetPos = traverseTarget;

					// FIX #1/#2/#13: Store traverse polygon ref from LAST corner
					botData.traverseStartPosZ = botPos.z;
					botData.traversePolyRef = traverseCornerPoly;
					DevMsg(eDLL_T::SERVER, "  Stored traversePolyRef=%llu, startZ=%.1f\n",
						(unsigned long long)botData.traversePolyRef, botData.traverseStartPosZ);
				}
			}
		}

		// =====================================================================
		// MOVEMENT - Move forward, with special handling for traversals
		// =====================================================================
		if (needsTraverse)
		{
			if (isWallClimb)
			{
				// WALL CLIMB: Face the wall direction with LEVEL pitch
				// Apex wall climbing requires looking AT the wall (level or slightly up), not up at the destination
				// Use yaw toward target but flatten the pitch

				// Create climb angles: use target yaw but level pitch (0 or slightly up)
				QAngle climbAngles;
				climbAngles.y = targetAngles.y;  // Face toward target horizontally
				climbAngles.x = 0.0f;            // Level pitch - look at wall, not up
				climbAngles.z = 0.0f;

				DevMsg(eDLL_T::SERVER, "WALL CLIMB MOVE: climbing=%d, targetYaw=%.1f, currentYaw=%.1f, pitch=%.1f->0\n",
					isActivelyClimbing ? 1 : 0, targetAngles.y, botData.currentAngles.y, botData.currentAngles.x);

				if (isActivelyClimbing)
				{
					// DURING CLIMB: Maintain current angles, don't turn
					cmd.viewangles = botData.currentAngles;
				}
				else
				{
					// INITIATING/ATTEMPTING CLIMB: Use climb angles with level pitch
					cmd.viewangles = climbAngles;
					botData.currentAngles = climbAngles;
				}

				cmd.forwardmove = 1.0f;
				cmd.sidemove = 0.0f;  // Don't strafe - go straight into wall
				cmd.buttons |= IN_FORWARD | IN_SPEED;
			}
			else
			{
				// GAP CROSSING: Keep smooth movement, just add jump
				// Don't snap view - let normal turning handle it (turnSpeed already calculated above)
				botData.currentAngles = LerpAngles(botData.currentAngles, targetAngles, turnSpeed);
				cmd.viewangles = botData.currentAngles;
				cmd.forwardmove = 1.0f;
				cmd.buttons |= IN_FORWARD | IN_SPEED;
			}

			// Press jump if needed
			if (shouldJumpNow)
			{
				cmd.buttons |= IN_JUMP;
			}
		}
		else if (angleDiff < 60.0f)
		{
			// Facing roughly the right way, move forward normally
			cmd.forwardmove = 1.0f;
			cmd.buttons |= IN_FORWARD | IN_SPEED;
		}
		else
		{
			// Need to turn more first, slow down
			cmd.forwardmove = 0.3f;
			cmd.buttons |= IN_FORWARD;
		}
	}
	// =========================================================================
	// TRAVERSAL MODE - When goal is higher/lower and path ended
	// =========================================================================
	else if (botData.inTraversalMode)
	{
		// Calculate angle to goal
		QAngle targetAngles = CalcAngleToTarget(botPos, botData.goalPosition);

		// Smooth turn towards goal (unless overridden by loot bin look-at)
		if (!overrideViewAngles)
		{
			float turnSpeed = bot_nav_turn_speed.GetFloat() * frameTime * 2.0f;  // Turn faster in traversal mode
			botData.currentAngles = LerpAngles(botData.currentAngles, targetAngles, turnSpeed);
			cmd.viewangles = botData.currentAngles;
		}

		// Move forward at full speed
		cmd.forwardmove = 1.0f;
		cmd.buttons |= IN_FORWARD | IN_SPEED;

		// Jump ONCE when entering traversal mode (behavior controlled by bot_nav_traversal_jump)
		// Mode 0 (default): Smart - jump when climbing up, walk when falling down
		// Mode 1: Always jump regardless of height difference
		if (!botData.hasJumpedForTraversal)
		{
			float distToGoal = (botData.goalPosition - botPos).Length2D();
			float heightDiff = botData.goalPosition.z - botPos.z;

			bool shouldJump = false;

			if (bot_nav_traversal_jump.GetBool())
			{
				// Always-jump mode: press jump regardless of height difference
				shouldJump = true;
				DevMsg(eDLL_T::SERVER, "TRAVERSAL MODE: Always-jump enabled (dist=%.1f, height=%.1f)\n", distToGoal, heightDiff);
			}
			else
			{
				// Smart mode: only jump when climbing up, walk off edges when falling
				if (heightDiff > 0.0f)
				{
					shouldJump = true;
					DevMsg(eDLL_T::SERVER, "TRAVERSAL MODE: Jumping to climb/grab ledge (dist=%.1f, height=%.1f)\n", distToGoal, heightDiff);
				}
				else
				{
					DevMsg(eDLL_T::SERVER, "TRAVERSAL MODE: Walking forward to fall (dist=%.1f, height=%.1f)\n", distToGoal, heightDiff);
					// Don't jump - forward momentum carries bot off the edge
				}
			}

			if (shouldJump)
			{
				cmd.buttons |= IN_JUMP;
			}

			botData.hasJumpedForTraversal = true;
		}

		// Debug: Log traversal mode status occasionally
		static float nextTraversalDebug = 0.0f;
		if (currentTime >= nextTraversalDebug)
		{
			float distToGoal = (botData.goalPosition - botPos).Length2D();
			float heightDiff = botData.goalPosition.z - botPos.z;
			DevMsg(eDLL_T::SERVER, "TRAVERSAL MODE ACTIVE: dist=%.1f, height=%.1f, hasJumped=%d\n",
				distToGoal, heightDiff, botData.hasJumpedForTraversal ? 1 : 0);
			nextTraversalDebug = currentTime + 1.0f;
		}

		// Traversal mode will exit automatically when FindPath succeeds and creates new path
	}
	else
	{
		// No path and not in traversal mode - stand still but keep current angles
		cmd.forwardmove = 0.0f;
		if (!overrideViewAngles)
		{
			cmd.viewangles = botData.currentAngles;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Runs the command simulation for fake players
//-----------------------------------------------------------------------------
void Physics_RunBotSimulation(bool bSimulating)
{
	if (!sv_simulateBots.GetBool())
		return;

	// Draw marker visualization
	DrawMarker();

	// Draw bot paths for debugging
	DrawBotPaths();

	// Draw bot view direction (RED = botData.currentAngles, BLUE = actual player eye angles)
	DrawBotViewDirection();

	for (int i = 0; i < g_ServerGlobalVariables->maxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient->IsActive() && pClient->IsFakeClient())
		{
			CPlayer* const pPlayer = UTIL_PlayerByIndex(pClient->GetHandle());
			if (!pPlayer)
				continue;

			// Skip dead bots - don't run AI or generate commands
			if (pPlayer->GetLifeState() != 0)
			{
				// Reset bot data when dead so navigation doesn't continue from death position
				g_BotData[i].Reset();
				continue;
			}

			// Create command
			CUserCmd cmd;

			float flOldFrameTime = gpGlobals->frameTime;
			float flOldCurTime = gpGlobals->curTime;

			cmd.frametime = flOldFrameTime;
			cmd.command_time = flOldCurTime;

			// Set command number - incrementing counter for each bot command
			static int s_botCommandNumber = 1;
			cmd.command_number = s_botCommandNumber++;
			cmd.tick_count = gpGlobals->tickCount;

			// Set head/camera positions - needed for wall climb detection
			Vector3D eyePos = GetPlayerEyePosition(pPlayer);
			cmd.headposition = eyePos;
			cmd.camerapos = eyePos;

			// Get current eye angles
			pPlayer->EyeAngles(&cmd.viewangles);

			RunBotNavigation(pPlayer, i, cmd);

			// Execute the command
			pPlayer->SetTimeBase(gpGlobals->curTime);
			MoveHelperServer()->SetHost(pPlayer);

			// Debug: log viewangles before PlayerRunCommand
			static float nextViewDebug = 0.0f;
			if (gpGlobals->curTime >= nextViewDebug && g_BotData[i].lootbinState == BotData::LOOTBIN_APPROACHING)
			{
				DevMsg(eDLL_T::SERVER, "PRE-RUN cmd.viewangles: pitch=%.1f, yaw=%.1f\n",
					cmd.viewangles.x, cmd.viewangles.y);
				nextViewDebug = gpGlobals->curTime + 0.2f;
			}

			pPlayer->PlayerRunCommand(&cmd, MoveHelperServer());

			pPlayer->SetLastUserCommand(&cmd);

			gpGlobals->frameTime = flOldFrameTime;
			gpGlobals->curTime = flOldCurTime;

			MoveHelperServer()->SetHost(NULL);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Runs the main physics simulation loop against all entities ( except players )
//-----------------------------------------------------------------------------
void Physics_RunThinkFunctions(bool bSimulating)
{
	Physics_RunBotSimulation(bSimulating);
	v_Physics_RunThinkFunctions(bSimulating);
}

///////////////////////////////////////////////////////////////////////////////
void VPhysics_Main::Detour(const bool bAttach) const
{
	DetourSetup(&v_Physics_RunThinkFunctions, &Physics_RunThinkFunctions, bAttach);
}
