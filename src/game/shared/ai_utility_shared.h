#ifndef AI_UTILITY_SHARED_H
#define AI_UTILITY_SHARED_H

#include "navmesh_debug_draw.h"
#include "mathlib/vector.h"
#include "mathlib/vplane.h"
#include "mathlib/fltx4.h"

//------------------------------------------------------------------------------
// Forward declarations
//------------------------------------------------------------------------------
struct dtMeshTile;
class dtNavMesh;
class CAI_Network;
class Vector3D;
class Color;

//------------------------------------------------------------------------------
// 
//------------------------------------------------------------------------------
class CAI_Utility
{
public:
	CAI_Utility(void);

	void RunRenderFrame(void);
	void DrawNavMesh(const dtNavMesh& mesh, const u32 flags);

	void DrawAIScriptNetwork(const CAI_Network* pNetwork,
		const Vector3D& vCameraPos,
		const int iNodeIndex,
		const float flCameraRange,
		const bool bDepthBuffer) const;

	static shortx8 PackNodeLink(i32 a, i32 b, i32 c = 0, i32 d = 0);
	int GetNearestNodeToPos(const CAI_Network* pAINetwork, const Vector3D* vec) const;
	bool IsTileWithinRange(const dtMeshTile* pTile, const VPlane* vPlane, const Vector3D& vCamera, const float flCameraRadius) const;

private:
	rdNavMeshDebugDraw m_navMeshDebugDraw;
	dtNavMeshQuery m_navMeshQuery;

	VPlane m_cullPlane;
};

extern CAI_Utility g_AIUtility;
#endif // AI_UTILITY_SHARED_H
