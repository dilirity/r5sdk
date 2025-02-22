//=============================================================================//
//
// Purpose: defines the agent types for AI
//
//=============================================================================//
#ifndef AI_AGENT_H
#define AI_AGENT_H

// For the constant DT_MAX_TRAVERSE_TYPES.
#include "Detour/Include/DetourNavMesh.h"

// The traverse types determine the animations, they also determine the jump
// links that can be taken (e.g. ANIMTYPE_FRAG_DRONE can take far more jumps
// than ANIMTYPE_STALKER. This is determined during the creation of the static
// pathing data in the NavMesh.
enum TraverseAnimType_e
{
	// NAVMESH_SMALL
	ANIMTYPE_HUMAN = 0,
	ANIMTYPE_SPECTRE,
	ANIMTYPE_STALKER,
	ANIMTYPE_FRAG_DRONE,
	ANIMTYPE_PILOT,

	// NAVMESH_MED_SHORT
	ANIMTYPE_PROWLER,

	// NAVMESH_MEDIUM
	ANIMTYPE_SUPER_SPECTRE,

	// NAVMESH_LARGE
	ANIMTYPE_TITAN,

	// NAVMESH_EXTRA_LARGE
	ANIMTYPE_GOLIATH,

	// Not an anim type!
	ANIMTYPE_COUNT,
	ANIMTYPE_NONE = -1 // No Animtype (appears after animtype count as we don't want to count it)
};

static inline const char* g_traverseAnimTypeNames[ANIMTYPE_COUNT] = {
	"human",
	"spectre",
	"stalker",
	"frag_drone",
	"pilot",
	"prowler",
	"super_spectre",
	"titan",
	"goliath",
};

// Taken from the assignment to dtQueryFilter::m_traverseCost at [r5apex_ds + 0xF2B180].
// Each value in a slot is mapped to a traverse type, see dtQueryFilter::m_traverseCost.
static inline const float g_traverseAnimDefaultCosts[ANIMTYPE_COUNT][DT_MAX_TRAVERSE_TYPES] = {
	{ // human
		 218.663f,  322.24f,  322.24f,   184.137f,
		 351.011f,  391.291f, 351.011f,  443.079f,
		 351.011f,  391.291f, 431.571f,  327.994f,
		 327.994f,  443.079f, 351.011f,  391.291f,
		 218.663f,  218.663f, 0.f,       0.f,
		 391.291f,  431.571f, -100.8f,   1.7188f,
		 -38.2473f, 125.722f, -5.90619f, -38.2473f,
		 -100.8f,   1.7188f,  -37.2473f, 125.722f
	},
	{ // spectre
		647.033f, 508.383f, 677.844f, 462.166f,
		724.061f, 847.305f, 0.f,      0.f,
		1294.07f, 847.305f, 0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		847.305f, 0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f
	},
	{ // stalker
		647.033f,  508.383f,       677.844f,      462.166f,
		724.061f,  847.305f,       0.f,           0.f,
		1294.07f,  847.305f,       0.f,           0.f,
		0.f,       0.f,            0.f,           0.f,
		0.f,       0.f,            0.f,           0.f,
		847.305f,  0.f,           -187.995f,      2.03727e-010f,
		-37.9984f, 185.586f,      -1.34605e-010f, -37.9984f,
		-187.995f, 2.03727e-010f, -36.9984f,      185.586f
	},
	{ // npc_frag_drone //
		218.663f, 322.24f,  322.24f,  184.137f,
		351.011f, 391.291f, 351.011f, 443.079f,
		351.011f, 391.291f, 431.571f, 327.994f,
		327.994f, 443.079f, 351.011f, 391.291f,
		218.663f, 218.663f, 0.f,      0.f,
		391.291f, 431.571f, 0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f
	},
	{ // pilot
		647.033f, 508.383f, 677.844f, 462.166f,
		724.061f, 847.305f, 0.f,      0.f,
		1294.07f, 847.305f, 0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		847.305f, 0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f
	},
	{ // prowler
		647.033f, 508.383f, 677.844f, 462.166f,
		724.061f, 847.305f, 0.f,      0.f,
		1294.07f, 847.305f, 0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		847.305f, 0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f
	},
	{ // super_spectre
		647.033f, 508.383f, 677.844f, 462.166f,
		724.061f, 847.305f, 0.f,      0.f,
		1294.07f, 847.305f, 0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		847.305f, 0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f
	},
	{ // titan
		647.033f, 508.383f, 677.844f, 462.166f,
		724.061f, 847.305f, 0.f,      0.f,
		1294.07f, 847.305f, 0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		847.305f, 0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f,
		0.f,      0.f,      0.f,      0.f
	},
	{ // goliath
		647.033f,     508.383f, 677.844f,     462.166f,
		724.061f,     847.305f, 0.f,          0.f,
		1294.07f,     847.305f, 0.f,          0.f,
		0.f,          0.f,      0.f,          0.f,
		0.f,          0.f,      0.f,          0.f,
		847.305f,     0.f,      1.65958e+10f, 7.17465e-43f,
		5.60519e-45f, 0.f,      0.f,          0.f,
		5.60519e-45f, 0.f,      0.f,          0.f
	}
};

#endif // AI_AGENT_H
