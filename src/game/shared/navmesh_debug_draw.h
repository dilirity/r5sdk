//============================================================================//
// 
// Purpose: NavMesh debug draw implementation
// 
//============================================================================//
#ifndef NAVMESH_DEBUG_DRAW_H
#define NAVMESH_DEBUG_DRAW_H

#include "recast/Shared/Include/SharedCommon.h"
#include "recast/Detour/Include/DetourNavMeshQuery.h"
#include "recast/DebugUtils/Include/DebugDraw.h"

#include "mathlib/vector.h"
#include "mathlib/color.h"

//------------------------------------------------------------------------------
// NavMesh debug draw implementation.
//------------------------------------------------------------------------------
class rdNavMeshDebugDraw : public duDebugDraw
{
public:
	rdNavMeshDebugDraw();

	virtual void depthMask(bool state) override;
	virtual void texture(bool state) override;

	virtual void begin(const duDebugDrawPrimitives prim, const float size = 1.0f, const rdVec3D* offset = 0) override;

	virtual void vertex(const rdVec3D* pos, unsigned int color) override;
	virtual void vertex(const float x, const float y, const float z, unsigned int color) override;
	virtual void vertex(const rdVec3D* pos, unsigned int color, const rdVec2D* uv) override;
	virtual void vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v) override;

	void handleAppend(const float x, const float y, const float z, unsigned int color);

	virtual unsigned int areaToFaceCol(const unsigned int area) const override;
	virtual unsigned int areaToEdgeCol(const unsigned int area) const override;

	virtual void end() override;

private:
	int m_currentVertCount;
	int m_expectedVertCount;
	float m_primitiveSize;

	Vector3D m_drawOffset;
	Vector3D m_primitiveVertices[4];

	duDebugDrawPrimitives m_currentPrimitive;
	Color m_primitiveColor;
};

#endif // NAVMESH_DEBUG_DRAW_H
