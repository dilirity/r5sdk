#ifndef RENDERUTILS_H
#define RENDERUTILS_H
#include "mathlib/vector.h"

void DebugDrawBox(const Vector3D& vOrigin, const QAngle& vAngles, const Vector3D& vMins, const Vector3D& vMaxs, Color color, bool bZBuffer = true);
void DebugDrawCylinder(const Vector3D& vOrigin, const QAngle& vAngles, float flRadius, float flHeight, Color color, int nSides = 16, bool bZBuffer = true);
void DebugDrawCapsule(const Vector3D& vStart, const QAngle& vAngles, const Vector3D& vRadius, float flHeight, Color color, bool bZBuffer = true);
void DebugDrawSphere(const Vector3D& vOrigin, float flRadius, Color color, int nSegments = 16, bool bZBuffer = true);
void DebugDrawHemiSphere(const Vector3D& vOrigin, const QAngle& vAngles, const Vector3D& vRadius, Color color, int nSegments = 8, bool bZBuffer = true);
void DebugDrawCircle(const Vector3D& vOrigin, const QAngle& vAngles, float flRadius, Color color, int nSegments = 16, bool bZBuffer = true);
void DebugDrawSquare(const Vector3D& vOrigin, const QAngle& vAngles, float flSquareSize, Color color, bool bZBuffer = true);
void DebugDrawTriangle(const Vector3D& vOrigin, const QAngle& vAngles, float flTriangleSize, Color color, bool bZBuffer = true);
void DebugDrawMark(const Vector3D& vOrigin, float flRadius, const vector<int>& vColor, bool bZBuffer = true);
void DrawStar(const Vector3D& vRrigin, float flRadius, bool bZBuffer = true);
void DebugDrawArrow(const Vector3D& vOrigin, const Vector3D& vEnd, float flArraySize, Color color, bool bZBuffer = true);
void DebugDrawAxis(const Vector3D& vOrigin, const QAngle& vAngles = { 0, 0, 0 }, float flScale = 50.f, bool bZBuffer = true);

///////////////////////////////////////////////////////////////////////////////
void RenderBox(const matrix3x4_t& vTransforms, const Vector3D& vMins, const Vector3D& vMaxs, Color color, bool bZBuffer);
void RenderWireframeSweptBox(const Vector3D& vStart, const Vector3D& vEnd, const QAngle& angles,
	const Vector3D& vMins, const Vector3D& vMaxs, const Color c, const bool bZBuffer);
void RenderTriangle(const Vector3D& p1, const Vector3D& p2, const Vector3D& p3, const Color c, const bool bZBuffer);
void RenderCapsule(const Vector3D& vStart, const Vector3D& vEnd, const float flRadius, const Color c, const bool bZBuffer);

///////////////////////////////////////////////////////////////////////////////
inline void(*v_InitializeStandardMaterials)();

inline void* (*v_RenderWireFrameBox)(const matrix3x4_t& vTransforms, const Vector3D& vMins, const Vector3D& vMaxs, Color color, bool bZBuffer);
inline void* (*v_RenderWireframeSphere)(const Vector3D& vCenter, float flRadius, int nTheta, int nPhi, Color color, bool bZBuffer);
inline void* (*v_RenderLine)(const Vector3D& vOrigin, const Vector3D& vDest, Color color, bool bZBuffer);

///////////////////////////////////////////////////////////////////////////////
class V_RenderUtils : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("InitializeStandardMaterials", v_InitializeStandardMaterials);
		LogFunAdr("RenderWireFrameBox", v_RenderWireFrameBox);
		LogFunAdr("RenderWireframeSphere", v_RenderWireframeSphere);
		LogFunAdr("RenderLine", v_RenderLine);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 83 EC ? 65 48 8B 04 25 ? ? ? ? BA ? ? ? ? 48 8B 08 8B 04 0A 39 05 ? ? ? ? 0F 8F ? ? ? ? 48 8D 0D").GetPtr(v_InitializeStandardMaterials);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 44 89 4C 24 ??").GetPtr(v_RenderWireFrameBox);
		Module_FindPattern(g_GameDll, "40 56 41 54 41 55 48 81 EC ?? ?? ?? ??").GetPtr(v_RenderWireframeSphere);
		Module_FindPattern(g_GameDll, "48 89 74 24 ?? 44 89 44 24 ?? 57 41 56").GetPtr(v_RenderLine);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // RENDERUTILS_H
