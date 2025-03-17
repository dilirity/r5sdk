//=============================================================================//
//
// Purpose: Public interfaces to vphysics DLL
//
// $NoKeywords: $
//=============================================================================//
#ifndef VPHYSICS_INTERFACE_H
#define VPHYSICS_INTERFACE_H
#include "public/vphysics/vcollide.h"

#define VPHYSICS_COLLISION_INTERFACE_VERSION	"VPhysicsCollision007"

abstract_class IPhysicsCollision
{
public:
	virtual ~IPhysicsCollision(void) {}

private:
	// TODO: reverse these:
	virtual void sub_14058C3B0() = 0;
	virtual void sub_14058C3F0() = 0;
	virtual void sub_14058CD80() = 0;
	virtual void sub_14058C6E0() = 0;
	virtual void sub_14058C6F0() = 0;
	virtual void sub_14058CDD0() = 0;
	virtual void sub_14058CB50() = 0;
	virtual void sub_14058C980() = 0;
	virtual void sub_14058D3D0() = 0;
	virtual void sub_14058D400() = 0;
	virtual void sub_14058C0D0() = 0;
	virtual void sub_14058C060() = 0;

public:
	virtual void VCollideLoad(vcollide_t* const pOutput, const int numSolids, const char* const pBuffer) = 0;
	virtual void VCollideUnload(vcollide_t* const pVCollide) = 0;

	// TODO: there is more past this, see r5apex.exe @1413A9420
};

abstract_class IVPhysicsDebugOverlay
{
public:
	virtual void AddPhysicsEntityTextOverlay(int ent_index, int line_offset, float duration, int r, int g, int b, int a, PRINTF_FORMAT_STRING const char* format, ...) = 0;
	virtual void AddPhysicsBoxOverlay(const Vector3D& origin, const Vector3D& mins, const Vector3D& max, QAngle const& orientation, int r, int g, int b, int a, float duration) = 0;
	virtual void AddPhysicsTriangleOverlay(const Vector3D& p1, const Vector3D& p2, const Vector3D& p3, int r, int g, int b, int a, bool noDepthTest, float duration) = 0;
	virtual void AddPhysicsLineOverlay(const Vector3D& origin, const Vector3D& dest, int r, int g, int b,bool noDepthTest, float duration) = 0;
	virtual void AddPhysicsTextOverlay(const Vector3D& origin, float duration, PRINTF_FORMAT_STRING const char* format, ...) = 0;
	virtual void AddPhysicsTextOverlay(const Vector3D& origin, int line_offset, float duration, PRINTF_FORMAT_STRING const char* format, ...) = 0;
	virtual void AddPhysicsScreenTextOverlay(float flXPos, float flYPos,float flDuration, int r, int g, int b, int a, const char* text) = 0;
	virtual void AddPhysicsSweptBoxOverlay(const Vector3D& start, const Vector3D& end, const Vector3D& mins, const Vector3D& max, const QAngle& angles, int r, int g, int b, int a, float flDuration) = 0;
	virtual void AddPhysicsTextOverlayRGB(const Vector3D& origin, int line_offset, float duration, float r, float g, float b, float alpha, PRINTF_FORMAT_STRING const char* format, ...) = 0;
};

#endif // VPHYSICS_INTERFACE_H
