#pragma once
#include "mathlib/vector.h"
#include "mathlib/vector4d.h"
#include "mathlib/color.h"
#include "mathlib/ssemath.h"
#include "public/idebugoverlay.h"
#include "public/vphysics/vphysics_interface.h"

// When used as a duration by a server-side NDebugOverlay:: call,
// causes the overlay to persist until the next server update.
constexpr auto NDEBUG_PERSIST_TILL_NEXT_SERVER = (0.01023f);

// When used as a duration by a client-side script function call,
// causes the overlay to persist until the next client update.
constexpr auto NDEBUG_PERSIST_TILL_NEXT_CLIENT = (0.02046f);

extern ConVar enable_debug_text_overlays;

enum class OverlayType_t
{
	OVERLAY_BOX = 0,
	OVERLAY_SPHERE,
	OVERLAY_LINE,
	OVERLAY_CUSTOM_MESH,
	OVERLAY_SPLINE,
	OVERLAY_TRIANGLE,
	OVERLAY_SWEPT_BOX,
	OVERLAY_CAPSULE, // see 0x140209440, possibly a tetrahedron or quadrilateral? Never used. Now replaced with capsule.
	OVERLAY_DESTROYED, // see 0x140208230, DestroyOverlay sets all destroyed overlays to this.
};

struct OverlayBase_t
{
	OverlayBase_t(void)
	{
		m_Type          = OverlayType_t::OVERLAY_BOX;
		m_nCreationTick = -1;
		m_flEndTime     = 0.0f;
		m_pNextOverlay  = nullptr;
		m_nOverlayTick  = -1;
	}
	bool IsDead(void) const;
	void SetEndTime(const float duration);

	OverlayType_t   m_Type;          // What type of overlay is it?
	int             m_nCreationTick; // Duration -1 means go away after this frame #
	float           m_flEndTime;     // When does this box go away
	// There is 4 bytes padding here.
	OverlayBase_t*  m_pNextOverlay;  // The next overlay
	int             m_nOverlayTick;  // 24
	// There is 4 bytes padding here.
};

struct OverlayBox_t : public OverlayBase_t
{
	OverlayBox_t(void) { m_Type = OverlayType_t::OVERLAY_BOX; }

	matrix3x4a_t    transforms;
	Vector3D        mins;
	Vector3D        maxs;
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
	bool            noDepthTest;
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
	bool            noDepthTest;
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

class OverlayText_t
{
public:
	OverlayText_t()
	{
		origin.Init();
		bUseOrigin = false;
		lineOffset = 0;
		screenPos.Init();
		m_nServerCount = -1;
		textLen = 0;
		textBuf = nullptr;
		m_flEndTime = 0.0f;
		m_nCreationTick = -1;
		m_nOverlayTick = -1;
		r = g = b = a = 255;
		nextOverlayText = 0;
	}

	~OverlayText_t()
	{
		if (textBuf)
		{
			delete[] textBuf;
			textBuf = nullptr;
		}
	}

	void SetEndTime(const float duration);

	Vector3D origin;
	bool bUseOrigin;
	int lineOffset;
	Vector2D screenPos;
	int m_nServerCount;
	char unk[24];
	ssize_t textLen;
	char* textBuf;
	float m_flEndTime;
	int m_nCreationTick;
	int m_nOverlayTick;
	int r;
	int g;
	int b;
	int a;
	OverlayText_t* nextOverlayText;
};

class CIVDebugOverlay : public IVDebugOverlay, public IVPhysicsDebugOverlay
{
public: // Hook statics:
	static void AddEntityTextOverlay(CIVDebugOverlay* const thisptr, const int entIndex, const int lineOffset, const float duration, const int r, const int g, const int b, const int a, const char* const format, ...);

	static void AddTextOverlay(CIVDebugOverlay* const thisptr, const Vector3D& origin, const float duration, const char* const format, ...);
	static void AddTextOverlayAtOffset(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration, const char* const format, ...);

	static void AddScreenTextOverlayAtOffset(CIVDebugOverlay* const thisptr, const Vector2D& screenPos, const int lineOffset, const float flDuration, const int r, const int g, const int b, const int a, const char* const text);
	static void AddScreenTextOverlayAtCenter(CIVDebugOverlay* const thisptr, IVDebugOverlay* const unused1, const char* const text, const void* unused2, const int unk1, const int unk2);

	static void AddTextOverlayRGBu32(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration,
		const int r, const int g, const int b, const int a, PRINTF_FORMAT_STRING const char* const format, ...) FMTFUNCTION(9, 10);

	static void AddTextOverlayRGBf32(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration,
		const float r, const float g, const float b, const float a, PRINTF_FORMAT_STRING const char* const format, ...) FMTFUNCTION(9, 10);

public:
	static void AddSphereOverlayInternal(CIVDebugOverlay* const thisptr, const Vector3D& vOrigin, const float flRadius, const int nTheta, const int nPhi, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration);
	static void AddSweptBoxInternal(CIVDebugOverlay* const thisptr, const Vector3D& start, const Vector3D& end, const Vector3D& mins, const Vector3D& max, const QAngle& angles, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration);
	static void AddCapsuleOverlayInternal(CIVDebugOverlay* const thisptr, const Vector3D& vStart, const Vector3D& vEnd, const float flRadius, const int r, const int g, const int b, const int a, const bool noDepthTest, const float flDuration);

	static void AddPhysicsEntityTextOverlay(CIVDebugOverlay* const thisptr, const int entIndex, const int lineOffset, const float duration, const int r, const int g, const int b, const int a, const char* const format, ...);
	static void AddPhysicsTextOverlay(CIVDebugOverlay* const thisptr, const Vector3D& origin, const float duration, const char* const format, ...);
	static void AddPhysicsTextOverlayAtOffset(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration, const char* const format, ...);
	static void AddPhysicsTextOverlayRGBf32(CIVDebugOverlay* const thisptr, const Vector3D& origin, const int lineOffset, const float duration,
		const float r, const float g, const float b, const float a, PRINTF_FORMAT_STRING const char* const format, ...) FMTFUNCTION(9, 10);

private:
	char m_text[1024];
	va_list m_argptr;
};

inline CIVDebugOverlay* g_pDebugOverlay = nullptr;

extern void DebugOverlay_HandleDecayed();

inline void(*v_DebugOverlay_DrawAllOverlays)(bool bDraw);
inline void(*v_DebugOverlay_ClearAllOverlays)(void);
inline void(*v_DebugOverlay_DebugDebugOverlays)(void* unk1, unsigned short unk2, unsigned int unk3, float unk4);

inline void (*v_DebugOverlay_AddEntityTextOverlay)(CIVDebugOverlay* const thisptr, const int entIndex, const int lineOffset, const float duration,
	const int r, const int g, const int b, const int a, const char* const format, ...);

inline void(*v_DebugOverlay_SetEndTime)(OverlayBase_t* const pOverlay, const float flDuration);

inline OverlayBase_t** s_pOverlays = nullptr;
inline OverlayText_t** s_pOverlayText = nullptr;
inline CThreadMutex* s_OverlayMutex = nullptr;
inline bool* s_bDrawGrid = nullptr;

inline int* g_nRenderTickCount = nullptr;
inline int* g_nOverlayTickCount = nullptr;

inline int* g_nOverlayStage = nullptr;

inline int* g_nNewOtherOverlays = nullptr;
inline int* g_nNewTextOverlays = nullptr;

inline void* g_pIVPhysicsDebugOverlay_VFTable = nullptr;
inline void* g_pIVDebugOverlay_VFTable = nullptr;

///////////////////////////////////////////////////////////////////////////////
class VDebugOverlay : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("DebugOverlay_DrawAllOverlays", v_DebugOverlay_DrawAllOverlays);
		LogFunAdr("DebugOverlay_ClearAllOverlays", v_DebugOverlay_ClearAllOverlays);
		LogFunAdr("DebugOverlay_DebugDebugOverlays", v_DebugOverlay_DebugDebugOverlays);
		LogFunAdr("DebugOverlay_AddEntityTextOverlay", v_DebugOverlay_AddEntityTextOverlay);
		LogFunAdr("DebugOverlay_SetEndTime", v_DebugOverlay_SetEndTime);
		LogVarAdr("s_pOverlays", s_pOverlays);
		LogVarAdr("s_pOverlayText", s_pOverlayText);
		LogVarAdr("s_OverlayMutex", s_OverlayMutex);
		LogVarAdr("s_bDrawGrid", s_bDrawGrid);
		LogVarAdr("g_nOverlayTickCount", g_nOverlayTickCount);
		LogVarAdr("g_nRenderTickCount", g_nRenderTickCount);
		LogVarAdr("g_nNewOtherOverlays", g_nNewOtherOverlays);
		LogVarAdr("g_nNewTextOverlays", g_nNewTextOverlays);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 55 48 83 EC 30 48 8B 05 ?? ?? ?? ?? 0F B6 E9").GetPtr(v_DebugOverlay_DrawAllOverlays);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ?? 48 8D 0D ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 48 8B 0D").GetPtr(v_DebugOverlay_ClearAllOverlays);
		Module_FindPattern(g_GameDll, "4C 8B DC 45 89 43 ?? 66 89 54 24").GetPtr(v_DebugOverlay_DebugDebugOverlays);
		Module_FindPattern(g_GameDll, "40 53 56 57 48 83 EC ?? 48 8D B4 24").GetPtr(v_DebugOverlay_AddEntityTextOverlay);
		Module_FindPattern(g_GameDll, "48 83 EC ?? FF 05 ?? ?? ?? ?? 48 8B D1").GetPtr(v_DebugOverlay_SetEndTime);
	}
	virtual void GetVar(void) const
	{
		s_pOverlays = CMemory(v_DebugOverlay_DrawAllOverlays).Offset(0x10).FindPatternSelf("48 8B 3D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<OverlayBase_t**>();
		s_pOverlayText = CMemory(v_DebugOverlay_ClearAllOverlays).Offset(0x3A).FindPatternSelf("48 8B 1D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<OverlayText_t**>();
		s_OverlayMutex = CMemory(v_DebugOverlay_DrawAllOverlays).Offset(0x10).FindPatternSelf("48 8D 0D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CThreadMutex*>();
		s_bDrawGrid = CMemory(v_DebugOverlay_ClearAllOverlays).Offset(0xC0).FindPatternSelf("C6 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x7).RCast<bool*>();

		g_nRenderTickCount = CMemory(v_DebugOverlay_DrawAllOverlays).Offset(0x50).FindPatternSelf("3B 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x6).RCast<int*>();
		g_nOverlayTickCount = CMemory(v_DebugOverlay_DrawAllOverlays).Offset(0x70).FindPatternSelf("3B 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x6).RCast<int*>();

		g_nOverlayStage = CMemory(v_DebugOverlay_DebugDebugOverlays).Offset(0x70).FindPatternSelf("8B 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x6).RCast<int*>();

		g_nNewOtherOverlays = CMemory(v_DebugOverlay_DebugDebugOverlays).Offset(0x1100).FindPatternSelf("44 89", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<int*>();
		g_nNewTextOverlays = CMemory(v_DebugOverlay_DebugDebugOverlays).Offset(0x1104).FindPatternSelf("44 89", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<int*>();

		g_GameDll.GetVirtualMethodTable(".?AVCIVDebugOverlay@@", 1).GetPtr(g_pIVPhysicsDebugOverlay_VFTable);
		g_GameDll.GetVirtualMethodTable(".?AVCIVDebugOverlay@@", 2).GetPtr(g_pIVDebugOverlay_VFTable);
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
