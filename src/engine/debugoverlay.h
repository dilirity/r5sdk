#pragma once
#include "mathlib/vector.h"
#include "mathlib/vector4d.h"
#include "mathlib/color.h"
#include "mathlib/ssemath.h"
#include "public/idebugoverlay.h"
#include "public/vphysics/vphysics_interface.h"

constexpr auto NDEBUG_PERSIST_TILL_NEXT_SERVER = (0.01023f);
extern ConVar r_debug_draw_depth_test;

enum class OverlayType_t
{
	OVERLAY_BOX = 0,
	OVERLAY_SPHERE,
	OVERLAY_LINE,
	OVERLAY_CUSTOM_MESH,
	OVERLAY_SPLINE,
	OVERLAY_TRIANGLE,
	OVERLAY_SWEPT_BOX,
	OVERLAY_UNKNOWN, // see 0x140209440, possibly a tetrahedron or quadrilateral?
	OVERLAY_DESTROYED, // see 0x140208230, DestroyOverlay sets all destroyed overlays to this.

	// Custom SDK overlays start from here.
	OVERLAY_CAPSULE,
};

struct OverlayBase_t
{
	OverlayBase_t(void)
	{
		m_Type          = OverlayType_t::OVERLAY_BOX;
		m_nCreationTick = -1;
		m_flEndTime     = 0.0f;
		m_nServerCount  = -1;
		m_pNextOverlay  = nullptr;
		m_nOverlayTick  = -1;
	}
	bool IsDead(void) const;
	void SetEndTime(const float duration);

	OverlayType_t   m_Type;          // What type of overlay is it?
	int             m_nCreationTick; // Duration -1 means go away after this frame #
	float           m_flEndTime;     // When does this box go away
	int             m_nServerCount;  // Latch server count, too
	OverlayBase_t*  m_pNextOverlay;  // 16
	int             m_nOverlayTick;  // 24
	// There is 4 bytes padding here.
};

struct OverlayBox_t : public OverlayBase_t
{
	OverlayBox_t(void) { m_Type = OverlayType_t::OVERLAY_BOX; }

	struct Transforms
	{
		Transforms()
		{
			xmm[0] = LoadZeroSIMD();
			xmm[1] = LoadZeroSIMD();
			xmm[2] = LoadZeroSIMD();
		};
		union
		{
			fltx4 xmm[3];
			matrix3x4a_t mat;
		};
	};

	Transforms transforms;
	Vector3D mins;
	Vector3D maxs;
	int             r;
	int             g;
	int             b;
	int             a;
	bool            noDepthTest;
};

struct OverlaySphere_t : public OverlayBase_t
{
	OverlaySphere_t(void) { m_Type = OverlayType_t::OVERLAY_SPHERE; }

	Vector3D        vOrigin;
	float           flRadius;
	int             nTheta;
	int             nPhi;
	int             r;
	int             g;
	int             b;
	int             a;
};

struct OverlayLine_t : public OverlayBase_t
{
	OverlayLine_t(void) { m_Type = OverlayType_t::OVERLAY_LINE; }

	Vector3D        origin;
	Vector3D        dest;
	int             r;
	int             g;
	int             b;
	int             a;
	bool            noDepthTest;
};

struct OverlayCustomMesh_t : public OverlayBase_t
{
	OverlayCustomMesh_t(void) { m_Type = OverlayType_t::OVERLAY_CUSTOM_MESH; }

	matrix3x4_t     matrices[128];
	int             r;
	int             g;
	int             b;
	int             a;
	bool            noDepthTest;
};

struct OverlayTriangle_t : public OverlayBase_t
{
	OverlayTriangle_t() { m_Type = OverlayType_t::OVERLAY_TRIANGLE; }

	Vector3D        p1;
	Vector3D        p2;
	Vector3D        p3;
	int             r;
	int             g;
	int             b;
	int             a;
	bool            noDepthTest;
};

struct OverlaySweptBox_t : public OverlayBase_t
{
	OverlaySweptBox_t() { m_Type = OverlayType_t::OVERLAY_SWEPT_BOX; }

	Vector3D        start;
	Vector3D        end;
	Vector3D        mins;
	Vector3D        maxs;
	QAngle          angles;
	int             r;
	int             g;
	int             b;
	int             a;
};

struct OverlayCapsule_t : public OverlayBase_t
{
	OverlayCapsule_t() { m_Type = OverlayType_t::OVERLAY_CAPSULE; }

	Vector3D        start;
	Vector3D        end;
	float           radius;
	int             r;
	int             g;
	int             b;
	int             a;
	bool            noDepthTest;
};

class CIVDebugOverlay : public IVDebugOverlay, public IVPhysicsDebugOverlay
{
public:
	void AddCapsuleOverlay(const Vector3D& vStart, const Vector3D& vEnd, const float flRadius, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration);

private:
	char m_text[1024];
	va_list m_argptr;
};

inline CIVDebugOverlay* g_pDebugOverlay = nullptr;

void DestroyOverlay(OverlayBase_t* pOverlay);
void DrawOverlay(OverlayBase_t* pOverlay);

inline void(*v_DrawAllOverlays)(bool bDraw);
inline void(*v_DestroyOverlay)(OverlayBase_t* pOverlay);

inline OverlayBase_t** s_pOverlays = nullptr;
inline CThreadMutex* s_OverlayMutex = nullptr;

inline int* g_nRenderTickCount = nullptr;
inline int* g_nOverlayTickCount = nullptr;

///////////////////////////////////////////////////////////////////////////////
class VDebugOverlay : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("DrawAllOverlays", v_DrawAllOverlays);
		LogFunAdr("DestroyOverlay", v_DestroyOverlay);
		LogVarAdr("s_Overlays", s_pOverlays);
		LogVarAdr("s_OverlayMutex", s_OverlayMutex);
		LogVarAdr("g_nOverlayTickCount", g_nOverlayTickCount);
		LogVarAdr("g_nRenderTickCount", g_nRenderTickCount);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 55 48 83 EC 30 48 8B 05 ?? ?? ?? ?? 0F B6 E9").GetPtr(v_DrawAllOverlays);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 20 48 8B D9 48 8D 0D ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 48 63 03").GetPtr(v_DestroyOverlay);
	}
	virtual void GetVar(void) const
	{
		s_pOverlays = CMemory(v_DrawAllOverlays).Offset(0x10).FindPatternSelf("48 8B 3D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<OverlayBase_t**>();
		s_OverlayMutex = CMemory(v_DrawAllOverlays).Offset(0x10).FindPatternSelf("48 8D 0D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CThreadMutex*>();

		g_nRenderTickCount = CMemory(v_DrawAllOverlays).Offset(0x50).FindPatternSelf("3B 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x6).RCast<int*>();
		g_nOverlayTickCount = CMemory(v_DrawAllOverlays).Offset(0x70).FindPatternSelf("3B 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x6).RCast<int*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
