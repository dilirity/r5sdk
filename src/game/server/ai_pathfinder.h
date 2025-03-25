//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: AI Pathfinder
//
// $NoKeywords: $
//=============================================================================//
#ifndef AI_PATHFINDER_H
#define AI_PATHFINDER_H

#if defined( _WIN32 )
#pragma once
#endif

#include "tier1/utlvector.h"
#include "engine/debugoverlay.h"

#include "ai_component.h"
#include "ai_navtypes.h"
#include "ai_network.h"
#include "ai_movetypes.h"

#include "recast/Detour/Include/DetourNavMeshQuery.h"

// Value remained unchanged for R5, see [r5apex_ds + 0xF31AF0].
#define NUM_NPC_DEBUG_OVERLAYS 50

class CAI_Pathfinder : public CAI_Component
{
public:
	CAI_Network* GetNetwork() { return m_pNetwork; }
	const CAI_Network* GetNetwork() const { return m_pNetwork; }

	dtNavMeshQuery* GetNavMeshQuery() { return &m_navQuery; }
	const dtNavMeshQuery* GetNavMeshQuery() const { return &m_navQuery; }

	dtQueryFilter* GetNavMeshFilter() { return &m_navFilter; }
	const dtQueryFilter* GetNavMeshFilter() const { return &m_navFilter; }

private:
	class CTriDebugOverlay
	{
	public:
		void AddTriOverlayLines(const Vector3D& vecStart, const Vector3D& vecApex, const Vector3D& vecEnd, const AIMoveTrace_t& startTrace, const AIMoveTrace_t& endTrace, const bool bPathClear);
		void ClearTriOverlayLines(void);
		void FadeTriOverlayLines(void);

	private:
		void AddTriOverlayLine(const Vector3D& origin, const Vector3D& dest, const int r, const int g, const int b, const bool noDepthTest);

		struct OverlayLine_s
		{
			Vector3D origin;
			Vector3D dest;
			int r;
			int g;
			int b;
			bool noDepthTest;
			bool draw;
		};

		OverlayLine_s** m_debugTriOverlayLine;
	};

	CTriDebugOverlay m_TriDebugOverlay;
	float m_flLimitDistFactor;
	float m_flLastStaleLinkCheckTime;
	char m_bIgnoreStaleLinks;
	char gap_21[7];
	CAI_Network* m_pNetwork;
	dtNavMeshQuery m_navQuery;
	dtQueryFilter m_navFilter;
	bool m_useClusterExclusions;
	char gap_119[3];
	float m_clusterPathMaxDetourBase;
	float m_clusterPathMaxDetourMultiplier;
	char gap_124[4];
	CUtlVector<CAI_Cluster*> m_excludedClusterNodes;     // TODO: verify template type!
	CUtlVector<CAI_ClusterLink*> m_excludedClusterLinks; // TODO: verify template type!
	void* m_pClusterPath;
	void* m_pClusterNoExclusionPath;
	int m_buildFlags;
	int m_failReason;
};
static_assert(sizeof(CAI_Pathfinder) == 0x180);

//-----------------------------------------------------------------------------

#endif // AI_PATHFINDER_H
