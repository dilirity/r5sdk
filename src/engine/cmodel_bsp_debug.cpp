//=============================================================================//
//
// Purpose: BSP collision debug rendering
//
// $NoKeywords: $
//=============================================================================//
#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "tier1/cmd.h"
#include "engine/cmodel_bsp_debug.h"

#ifndef DEDICATED
#include "tier0/memstd.h"
#include "mathlib/mathlib.h"
#include "tier2/renderutils.h"
#include "engine/debugoverlay.h"
#include "engine/client/clientstate.h"
#include "engine/client/vengineclient_impl.h"
#include "game/client/cliententitylist.h"
#endif // !DEDICATED

//-----------------------------------------------------------------------------
// ConVars - defined outside DEDICATED block so they're available on server too
//-----------------------------------------------------------------------------
ConVar bsp_collision_debug("bsp_collision_debug", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, 
	"Enable BSP collision debug rendering (draws collision triangles)");

ConVar bsp_collision_debug_mode("bsp_collision_debug_mode", "3", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT,
	"Debug draw mode: 1=wireframe, 2=solid, 3=both");

ConVar bsp_collision_debug_radius("bsp_collision_debug_radius", "2048", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT,
	"Radius around player to render collision triangles", true, 64.f, true, 16384.f);

ConVar bsp_collision_debug_alpha("bsp_collision_debug_alpha", "32", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT,
	"Alpha value for solid debug rendering", true, 0.f, true, 255.f);

ConVar bsp_trigger_debug("bsp_trigger_debug", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT,
	"Draw trigger collision volumes: 0=off, 1=OOB, 2=slip, 3=hurt, 4=soundscape, 5=no_zipline, 6=no_grapple, 7=warp_gate, 8=skydive, 9=multiple_other, 10=other, 11=non-trigger, -1=all");

ConVar bsp_trigger_debug_radius("bsp_trigger_debug_radius", "8192", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT,
	"Radius around player to render trigger volumes", true, 64.f, true, 65536.f);

#ifndef DEDICATED
//-----------------------------------------------------------------------------
// Triangle colors for per-triangle visualization (more vibrant)
//-----------------------------------------------------------------------------
static const Color s_TriangleColors[] = {
	Color(255, 50, 50, 255),    // Bright red
	Color(255, 150, 50, 255),   // Orange
	Color(255, 220, 50, 255),   // Gold
	Color(180, 255, 50, 255),   // Lime
	Color(50, 255, 100, 255),   // Green
	Color(50, 255, 200, 255),   // Teal
	Color(50, 220, 255, 255),   // Cyan
	Color(50, 150, 255, 255),   // Sky blue
	Color(80, 80, 255, 255),    // Blue
	Color(150, 50, 255, 255),   // Purple
	Color(220, 50, 255, 255),   // Magenta
	Color(255, 50, 180, 255),   // Pink
	Color(255, 100, 100, 255),  // Light red
	Color(100, 255, 150, 255),  // Mint
	Color(150, 200, 255, 255),  // Light blue
	Color(255, 180, 100, 255),  // Peach
};

//-----------------------------------------------------------------------------
// Trigger volume colors
//-----------------------------------------------------------------------------
// Current entity origin and color for type 8 leaf decoding (set by DrawBrushModelBVH)
static Vector3D s_currentEntityOrigin(0, 0, 0);
static Color s_currentTriggerColor(255, 255, 255, 255);

static Color GetTriggerColor(TriggerType_e type, int alpha)
{
	switch (type)
	{
	case TriggerType_e::OUT_OF_BOUNDS:  return Color(255, 50, 50, alpha);    // Red — you'll die if you stay
	case TriggerType_e::SLIP:           return Color(255, 220, 50, alpha);   // Yellow — slide surface
	case TriggerType_e::HURT:           return Color(200, 0, 100, alpha);    // Dark magenta — instant kill
	case TriggerType_e::SOUNDSCAPE:     return Color(100, 180, 255, alpha);  // Light blue — just audio
	case TriggerType_e::NO_ZIPLINE:     return Color(255, 150, 50, alpha);   // Orange — can't zipline
	case TriggerType_e::NO_GRAPPLE:     return Color(200, 100, 30, alpha);   // Dark orange — can't grapple
	case TriggerType_e::WARP_GATE:      return Color(160, 50, 220, alpha);   // Purple — phase runner teleporter
	case TriggerType_e::SKYDIVE:        return Color(50, 220, 160, alpha);   // Teal — skydive/rift exit
	case TriggerType_e::MULTIPLE_OTHER: return Color(180, 100, 220, alpha);  // Light purple — other trigger_multiple
	case TriggerType_e::OTHER_TRIGGER:  return Color(128, 128, 128, alpha);  // Grey — unknown
	case TriggerType_e::NON_TRIGGER:    return Color(220, 220, 220, alpha);  // White — structural
	default:                           return Color(255, 255, 255, alpha);  // White
	}
}

//-----------------------------------------------------------------------------
// Purpose: Parse entity lump text to build trigger model index map
// Called from our CM_ParseStarCollFromEntities detour before the original
//-----------------------------------------------------------------------------
static void ParseTriggerMappings(const char* entityText, int length)
{
	// Accumulate across multiple entity lump calls during map load
	// g_triggerVolumes is zero-initialized at startup, and reset in RenderTriggerVolumes on map change

	// Simple parser: scan for { } entity blocks, extract classname and model
	const char* p = entityText;
	const char* end = entityText + length;

	while (p < end)
	{
		// Find next '{'
		while (p < end && *p != '{') p++;
		if (p >= end) break;
		p++; // skip '{'

		// Parse key-value pairs until '}'
		char classname[128] = {};
		char editorclass[128] = {};
		int modelIndex = -1;
		float entOrigin[3] = {0, 0, 0};
		bool hasColl = false;

		while (p < end && *p != '}')
		{
			// Skip whitespace
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
			if (p >= end || *p == '}') break;

			// Expect quoted key
			if (*p != '"') { p++; continue; }
			p++; // skip opening quote

			const char* keyStart = p;
			while (p < end && *p != '"') p++;
			int keyLen = (int)(p - keyStart);
			if (p < end) p++; // skip closing quote

			// Skip whitespace between key and value
			while (p < end && (*p == ' ' || *p == '\t')) p++;

			// Expect quoted value
			if (p >= end || *p != '"') continue;
			p++; // skip opening quote

			const char* valStart = p;
			while (p < end && *p != '"') p++;
			int valLen = (int)(p - valStart);
			if (p < end) p++; // skip closing quote

			// Check key
			if (keyLen >= 5 && memcmp(keyStart, "*coll", 5) == 0)
			{
				hasColl = true;
			}
			else if (keyLen == 9 && memcmp(keyStart, "classname", 9) == 0)
			{
				int copyLen = (valLen < 127) ? valLen : 127;
				memcpy(classname, valStart, copyLen);
				classname[copyLen] = '\0';
			}
			else if (keyLen == 5 && memcmp(keyStart, "model", 5) == 0)
			{
				if (valLen > 1 && valStart[0] == '*')
				{
					// Parse integer after '*'
					modelIndex = 0;
					for (int i = 1; i < valLen; i++)
					{
						if (valStart[i] >= '0' && valStart[i] <= '9')
							modelIndex = modelIndex * 10 + (valStart[i] - '0');
					}
				}
			}
			else if (keyLen == 11 && memcmp(keyStart, "editorclass", 11) == 0)
			{
				int copyLen = (valLen < 127) ? valLen : 127;
				memcpy(editorclass, valStart, copyLen);
				editorclass[copyLen] = '\0';
			}
			else if (keyLen == 6 && memcmp(keyStart, "origin", 6) == 0)
			{
				// Parse "x y z" origin string
				char originBuf[128] = {};
				int copyLen = (valLen < 127) ? valLen : 127;
				memcpy(originBuf, valStart, copyLen);
				originBuf[copyLen] = '\0';
				// Simple float parse for 3 values
				char* cursor = originBuf;
				for (int oi = 0; oi < 3; oi++)
				{
					while (*cursor == ' ') cursor++;
					entOrigin[oi] = (float)atof(cursor);
					while (*cursor && *cursor != ' ') cursor++;
				}
			}
		}

		if (p < end) p++; // skip '}'

		// Record trigger mapping
		if (modelIndex > 0 && modelIndex < MAX_TRIGGER_VOLUMES && classname[0] && hasColl)
		{
			TriggerType_e type = TriggerType_e::NON_TRIGGER;

			if (strstr(classname, "trigger_out_of_bounds"))
				type = TriggerType_e::OUT_OF_BOUNDS;
			else if (strstr(classname, "trigger_slip"))
				type = TriggerType_e::SLIP;
			else if (strstr(classname, "trigger_hurt"))
				type = TriggerType_e::HURT;
			else if (strstr(classname, "trigger_soundscape"))
				type = TriggerType_e::SOUNDSCAPE;
			else if (strstr(classname, "trigger_no_zipline"))
				type = TriggerType_e::NO_ZIPLINE;
			else if (strstr(classname, "trigger_no_grapple"))
				type = TriggerType_e::NO_GRAPPLE;
			else if (strstr(classname, "trigger_multiple"))
			{
				if (strstr(editorclass, "trigger_warp_gate"))
					type = TriggerType_e::WARP_GATE;
				else if (strstr(editorclass, "trigger_skydive"))
					type = TriggerType_e::SKYDIVE;
				else
					type = TriggerType_e::MULTIPLE_OTHER;
			}
			else if (strncmp(classname, "trigger_", 8) == 0)
				type = TriggerType_e::OTHER_TRIGGER;

			g_triggerVolumes[modelIndex].type = type;
			g_triggerVolumes[modelIndex].originX = entOrigin[0];
			g_triggerVolumes[modelIndex].originY = entOrigin[1];
			g_triggerVolumes[modelIndex].originZ = entOrigin[2];

			if (modelIndex >= g_numTriggerVolumes)
				g_numTriggerVolumes = modelIndex + 1;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Detour for CM_ParseStarCollFromEntities
// Parses trigger mappings before calling the original function
//-----------------------------------------------------------------------------
static bool s_collisionMarked = false;

static int64_t CM_ParseStarCollFromEntities_Detour(char* entityLumpText, int entityLumpLength)
{
	// Reset trigger data on first entity lump of a new map load
	// The detour fires multiple times (once per entity lump), but s_collisionMarked
	// being true means we rendered a previous map — time to reset
	if (s_collisionMarked)
	{
		s_collisionMarked = false;
		memset(g_triggerVolumes, 0, sizeof(g_triggerVolumes));
		g_numTriggerVolumes = 0;
	}

	// Parse entity text to build trigger type mappings before the original
	// strips out *coll keys
	ParseTriggerMappings(entityLumpText, entityLumpLength);

	// Call original — this populates CollisionModelContext_t for each brush model
	return CM_ParseStarCollFromEntities(entityLumpText, entityLumpLength);
}

//-----------------------------------------------------------------------------
// Purpose: Get a unique color for a triangle based on hash
//-----------------------------------------------------------------------------
static Color GetTriangleColor(uint32_t leafIdx, int triIdx, int alpha)
{
	// Simple hash to get varied colors per triangle
	uint32_t hash = leafIdx * 31 + triIdx * 17;
	const int numColors = V_ARRAYSIZE(s_TriangleColors);
	Color c = s_TriangleColors[hash % numColors];
	c[3] = (unsigned char)alpha;
	return c;
}

//-----------------------------------------------------------------------------
// Purpose: Initialize the BSP collision debug system
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::Init()
{
	// Nothing to do here yet
}

//-----------------------------------------------------------------------------
// Purpose: Shutdown the BSP collision debug system
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::Shutdown()
{
	// Nothing to do here
}

//-----------------------------------------------------------------------------
// Purpose: Decode a packed int16 vertex from the vertex buffer
// Packed format: 6 bytes (int16 x, int16 y, int16 z)
// Decode: world = origin + int16 * scale (where scale already includes *65536)
// The SIMD code does: cvtepi32_ps(unpacklo_epi16(0, int16)) * scale + origin
// unpacklo_epi16(0, int16) puts int16 in high 16 bits = int16 << 16
// So effective decode is: origin + (int16 << 16) * scale = origin + int16 * 65536 * scale
//-----------------------------------------------------------------------------
Vector3D CBSPCollisionDebug::DecodePackedVertex(const int16_t* packedVerts, int vertexIndex,
	const Vector3D& origin, float quantScale)
{
	const int16_t* v = &packedVerts[vertexIndex * 3];
	const float effectiveScale = quantScale * 65536.0f;
	
	return Vector3D(
		origin.x + (float)v[0] * effectiveScale,
		origin.y + (float)v[1] * effectiveScale,
		origin.z + (float)v[2] * effectiveScale
	);
}

//-----------------------------------------------------------------------------
// Purpose: Decode a float vertex from the vertex buffer
// Float format: 12 bytes (float x, float y, float z)
// Type 4 float vertices are ABSOLUTE world coordinates, NOT relative to origin!
// This differs from Type 5 packed vertices which are origin-relative.
//-----------------------------------------------------------------------------
Vector3D CBSPCollisionDebug::DecodeFloatVertex(const float* floatVerts, int vertexIndex,
	const Vector3D& origin)
{
	// Float vertices are stored as absolute world coordinates
	// The origin parameter is not used for Type 4 (kept for API compatibility)
	const float* v = &floatVerts[vertexIndex * 3];
	return Vector3D(v[0], v[1], v[2]);
}

//-----------------------------------------------------------------------------
// Purpose: Draw a triangle with the specified render mode
// Mode 1 = wireframe, Mode 2 = solid, Mode 3 = both
// Uses IgnoreZ materials (bZBuffer=false) to render through geometry and avoid Z-fighting
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawTriangle(const Vector3D& v0, const Vector3D& v1, const Vector3D& v2, const Color& color, int renderMode)
{
	if (renderMode == 1 || renderMode == 3)
	{
		// Wireframe edges - use brighter color for better visibility
		Color wireColor = color;
		wireColor[3] = 255; // Full alpha for wireframe
		RenderLine(v0, v1, wireColor, false);  // false = IgnoreZ, renders through geometry
		RenderLine(v1, v2, wireColor, false);
		RenderLine(v2, v0, wireColor, false);
	}
	if (renderMode == 2 || renderMode == 3)
	{
		// Solid fill - also use IgnoreZ to avoid Z-fighting
		RenderTriangle(v0, v1, v2, color, false);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw leaf geometry (actual triangles) for a leaf node
// Child types from remap tool:
//   0 = Internal node
//   1 = None/empty
//   2 = Empty leaf
//   3 = Bundle (references other nodes)
//   4 = Poly3f (float, triangle)
//   5 = Poly3 (packed int16, triangle)
//   6 = Poly4f (float, quad - parallelogram)
//   7 = Poly4 (packed int16, quad - parallelogram)
//   8 = ConvexHull
//   9 = StaticProp
//   10 = Heightfield
//
// Leaf data format for Poly3 (Type 5):
//   Header (4 bytes): bits 0-11 = surfPropIdx, bits 12-15 = (numPolys-1), bits 16-31 = baseVertex
//   Per-triangle (4 bytes): bits 0-10 = v0_offset, bits 11-19 = v1_delta, bits 20-28 = v2_delta
//   Vertex indexing: running_base = baseVertex << 10, v0 = running_base + offset
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawLeafTriangles(const CollisionModelContext_t* ctx, uint32_t childIdx,
	int childType, const Vector3D& origin, float quantScale, int depth)
{
	if (!ctx || !ctx->leafDataStream)
	{
		return;
	}
	// Type 8 has its own embedded vertices, other types need ctx->verts
	if (childType != 8 && (!ctx->verts || childIdx == 0))
	{
		return;
	}

	// Cast verts pointer appropriately based on type
	// Type 4/6 use float vertices (12 bytes per vertex)
	// Type 5/7 use packed int16 vertices (6 bytes per vertex)

	const int renderMode = bsp_collision_debug_mode.GetInt();
	const int alpha = bsp_collision_debug_alpha.GetInt();

	// Leaf data format (verified via IDA reverse engineering of sub_1402DD4A0 / sub_1402DDC50):
	//
	// Header (uint32, read as two uint16):
	//   a3[0] bits 0-11  = surfPropIdx (12 bits)
	//   a3[0] bits 12-15 = numPolys - 1 (4 bits)
	//   a3[1]            = baseVertex (16 bits)
	//   runningBase = baseVertex << 10
	//
	// Per-polygon (uint32):
	//   Type 4/5 (triangles): 11-bit v0_offset | 9-bit v1_delta | 9-bit v2_delta | 3-bit flags
	//   Type 6/7 (quads):     10-bit v0_offset | 9-bit v1_delta | 9-bit v2_delta | 4-bit flags
	//
	// Vertex indexing (same for all types):
	//   runningBase += v0_offset
	//   idx0 = runningBase
	//   idx1 = idx0 + v1_delta + 1
	//   idx2 = idx0 + v2_delta + 1
	//   (runningBase carries forward to next polygon)
	//
	// Type 4: float vertices, absolute world coordinates. One triangle per polygon.
	// Type 5: packed int16 vertices, origin-relative. One triangle per polygon.
	// Type 6: float vertices, absolute world coordinates. Quad as parallelogram:
	//         v3 = v1 + v2 - v0, draws two triangles: (v0,v1,v2) and (v2,v1,v3)
	// Type 7: packed int16 vertices, origin-relative. Same quad format as type 6.

	if (childType == 5)
	{
		// Type 5 - packed int16 triangles (11/9/9/3 bit layout)
		const uint32_t* leafData = &ctx->leafDataStream[childIdx];
		const int16_t* packedVerts = reinterpret_cast<const int16_t*>(ctx->verts);

		const uint32_t header = leafData[0];
		const int numPolys = ((header >> 12) & 0xF) + 1;
		const int baseVertex = (header >> 16) & 0xFFFF;

		int runningBase = baseVertex << 10;

		for (int i = 0; i < numPolys && i < 16; i++)
		{
			const uint32_t triData = leafData[1 + i];
			const int v0_offset = triData & 0x7FF;         // 11 bits
			const int v1_delta = (triData >> 11) & 0x1FF;  // 9 bits
			const int v2_delta = (triData >> 20) & 0x1FF;  // 9 bits

			runningBase += v0_offset;
			const int idx0 = runningBase;
			const int idx1 = idx0 + v1_delta + 1;
			const int idx2 = idx0 + v2_delta + 1;

			const Vector3D v0 = DecodePackedVertex(packedVerts, idx0, origin, quantScale);
			const Vector3D v1 = DecodePackedVertex(packedVerts, idx1, origin, quantScale);
			const Vector3D v2 = DecodePackedVertex(packedVerts, idx2, origin, quantScale);

			const Color triColor = GetTriangleColor(childIdx, i, alpha);
			DrawTriangle(v0, v1, v2, triColor, renderMode);
		}
	}
	else if (childType == 6)
	{
		// Type 6 - float quads (10/9/9/4 bit layout, parallelogram construction)
		const uint32_t* leafData = &ctx->leafDataStream[childIdx];
		const float* floatVerts = reinterpret_cast<const float*>(ctx->verts);

		const uint32_t header = leafData[0];
		const int numPolys = ((header >> 12) & 0xF) + 1;
		const int baseVertex = (header >> 16) & 0xFFFF;

		int runningBase = baseVertex << 10;

		for (int i = 0; i < numPolys && i < 16; i++)
		{
			const uint32_t triData = leafData[1 + i];
			const int v0_offset = triData & 0x3FF;         // 10 bits
			const int v1_delta = (triData >> 10) & 0x1FF;  // 9 bits
			const int v2_delta = (triData >> 19) & 0x1FF;  // 9 bits

			runningBase += v0_offset;
			const int idx0 = runningBase;
			const int idx1 = idx0 + v1_delta + 1;
			const int idx2 = idx0 + v2_delta + 1;

			const Vector3D v0 = DecodeFloatVertex(floatVerts, idx0, origin);
			const Vector3D v1 = DecodeFloatVertex(floatVerts, idx1, origin);
			const Vector3D v2 = DecodeFloatVertex(floatVerts, idx2, origin);
			// 4th vertex: parallelogram v3 = v1 + v2 - v0
			const Vector3D v3(v1.x + v2.x - v0.x, v1.y + v2.y - v0.y, v1.z + v2.z - v0.z);

			const Color triColor = GetTriangleColor(childIdx, i * 2, alpha);
			const Color triColor2 = GetTriangleColor(childIdx, i * 2 + 1, alpha);
			DrawTriangle(v0, v1, v2, triColor, renderMode);
			DrawTriangle(v2, v1, v3, triColor2, renderMode);
		}
	}
	else if (childType == 7)
	{
		// Type 7 - packed int16 quads (10/9/9/4 bit layout, parallelogram construction)
		const uint32_t* leafData = &ctx->leafDataStream[childIdx];
		const int16_t* packedVerts = reinterpret_cast<const int16_t*>(ctx->verts);

		const uint32_t header = leafData[0];
		const int numPolys = ((header >> 12) & 0xF) + 1;
		const int baseVertex = (header >> 16) & 0xFFFF;

		int runningBase = baseVertex << 10;

		for (int i = 0; i < numPolys && i < 16; i++)
		{
			const uint32_t triData = leafData[1 + i];
			const int v0_offset = triData & 0x3FF;         // 10 bits
			const int v1_delta = (triData >> 10) & 0x1FF;  // 9 bits
			const int v2_delta = (triData >> 19) & 0x1FF;  // 9 bits

			runningBase += v0_offset;
			const int idx0 = runningBase;
			const int idx1 = idx0 + v1_delta + 1;
			const int idx2 = idx0 + v2_delta + 1;

			const Vector3D v0 = DecodePackedVertex(packedVerts, idx0, origin, quantScale);
			const Vector3D v1 = DecodePackedVertex(packedVerts, idx1, origin, quantScale);
			const Vector3D v2 = DecodePackedVertex(packedVerts, idx2, origin, quantScale);
			// 4th vertex: parallelogram v3 = v1 + v2 - v0
			const Vector3D v3(v1.x + v2.x - v0.x, v1.y + v2.y - v0.y, v1.z + v2.z - v0.z);

			const Color triColor = GetTriangleColor(childIdx, i * 2, alpha);
			const Color triColor2 = GetTriangleColor(childIdx, i * 2 + 1, alpha);
			DrawTriangle(v0, v1, v2, triColor, renderMode);
			DrawTriangle(v2, v1, v3, triColor2, renderMode);
		}
	}
	else if (childType == 4)
	{
		// Type 4 - float vertices (triangle format, not strip)
		// Uses same leaf data format as Type 5 but with float vertices
		// Float vertices are ABSOLUTE world coordinates (not origin-relative)
		const uint32_t* leafData = &ctx->leafDataStream[childIdx];
		const float* floatVerts = reinterpret_cast<const float*>(ctx->verts);
		
		const uint32_t header = leafData[0];
		const int numPolys = ((header >> 12) & 0xF) + 1;
		const int baseVertex = (header >> 16) & 0xFFFF;
		
		int runningBase = baseVertex << 10;
		
		for (int i = 0; i < numPolys && i < 16; i++)
		{
			const uint32_t triData = leafData[1 + i];
			const int v0_offset = triData & 0x7FF;
			const int v1_delta = (triData >> 11) & 0x1FF;
			const int v2_delta = (triData >> 20) & 0x1FF;
			
			runningBase += v0_offset;
			const int idx0 = runningBase;
			const int idx1 = idx0 + v1_delta + 1;
			const int idx2 = idx0 + v2_delta + 1;
			
			const Vector3D v0 = DecodeFloatVertex(floatVerts, idx0, origin);
			const Vector3D v1 = DecodeFloatVertex(floatVerts, idx1, origin);
			const Vector3D v2 = DecodeFloatVertex(floatVerts, idx2, origin);
			
			// Per-triangle color for variety
			const Color triColor = GetTriangleColor(childIdx, i, alpha);
			DrawTriangle(v0, v1, v2, triColor, renderMode);
		}
	}
	else if (childType == 8)
	{
		// Type 8 - ConvexHull
		// Leaf data layout:
		//   byte[0] = vertexCount
		//   byte[1] = edge/face count 1
		//   byte[2] = face data offset 1
		//   byte[3] = face data offset 2
		//   bytes[4..19] = per-hull origin/scale as 4 floats (x, y, z, scale)
		//   bytes[20..] = packed int16 vertices (6 bytes each: x, y, z)
		//   After vertices: triangle strip data using type-4 format

		const uint8_t* leafBytes = reinterpret_cast<const uint8_t*>(&ctx->leafDataStream[childIdx]);
		const int vertCount = leafBytes[0];

		if (vertCount == 0 || (leafBytes[2] == 0 && leafBytes[3] == 0))
			return;

		// Per-hull origin and scale at offset 4 (16 bytes = 4 floats)
		const float* hullParams = reinterpret_cast<const float*>(leafBytes + 4);
		const float hullScale = hullParams[3]; // w component = scale broadcast

		// Decode packed int16 vertices to world space
		// Engine formula: world = int16_unpack_to_high16 * scale + origin_vec
		// Which is: world = int16 * 65536.0 * scale + origin (for each component)
		// But from the decompiled code (line 82): mul by v8 (scale broadcast) + v4 (origin vec)
		// v8 = shuffle(v4, 0xFF) = broadcast v4.w = scale
		// So: world = int16_as_high16 * v4.w + v4.xyz
		// int16_as_high16 = int16 << 16 as float = int16 * 65536.0
		const float effectiveScale = hullScale * 65536.0f;

		Vector3D decodedVerts[256]; // max 256 vertices
		const int16_t* packedVerts = reinterpret_cast<const int16_t*>(leafBytes + 20);
		const int maxVerts = (vertCount < 256) ? vertCount : 256;

		for (int vi = 0; vi < maxVerts; vi++)
		{
			decodedVerts[vi].x = s_currentEntityOrigin.x + hullParams[0] + (float)packedVerts[vi * 3 + 0] * effectiveScale;
			decodedVerts[vi].y = s_currentEntityOrigin.y + hullParams[1] + (float)packedVerts[vi * 3 + 1] * effectiveScale;
			decodedVerts[vi].z = s_currentEntityOrigin.z + hullParams[2] + (float)packedVerts[vi * 3 + 2] * effectiveScale;
		}

		// Triangle data offset calculation from engine (sub_1402DBB20):
		//   v32 = byte[1] + 2 * vertCount
		//   rawOff = v32 + 2 * (v32 + 10)
		//   triDataOffset = rawOff + ((-rawOff) & 3)  // align up to multiple of 4
		const int byte1 = leafBytes[1];
		const int v32 = byte1 + 2 * vertCount;
		const int rawOff = v32 + 2 * (v32 + 10);
		const int triDataOffset = rawOff + ((-rawOff) & 3);

		// Two passes over triangle data:
		// Pass 1 (leafBytes[2] groups): triangles — same bit layout as BVH type 4
		//   v0_offset = data & 0x7FF (11 bits), v1_delta = (data>>11) & 0x1FF, v2_delta = (data>>20) & 0x1FF
		// Pass 2 (leafBytes[3] groups): quads — same bit layout as BVH type 6
		//   v0_offset = data & 0x3FF (10 bits), v1_delta = (data>>10) & 0x1FF, v2_delta = (data>>19) & 0x1FF
		//   4th vertex = v2 + v1 - v0 (parallelogram), drawn as 2 triangles

		const int pass1TriGroups = leafBytes[2];
		const int pass2QuadGroups = leafBytes[3];
		int dataPos = triDataOffset;

		// Pass 1: triangles
		for (int g = 0; g < pass1TriGroups; g++)
		{
			if (dataPos + 4 > 4096) break;

			const uint16_t* groupHeader = reinterpret_cast<const uint16_t*>(leafBytes + dataPos);
			const int numPolys = (groupHeader[0] >> 12) + 1;
			int runningBase = (int)groupHeader[1] << 10;

			for (int i = 0; i < numPolys && i < 16; i++)
			{
				const uint32_t triData = *reinterpret_cast<const uint32_t*>(leafBytes + dataPos + 4 + i * 4);
				const int v0_offset = triData & 0x7FF;
				const int v1_delta = (triData >> 11) & 0x1FF;
				const int v2_delta = (triData >> 20) & 0x1FF;

				const int idx0 = runningBase + v0_offset;
				const int idx1 = idx0 + 1 + v1_delta;
				const int idx2 = idx0 + 1 + v2_delta;
				runningBase = idx0;

				if (idx0 >= 0 && idx0 < maxVerts && idx1 >= 0 && idx1 < maxVerts && idx2 >= 0 && idx2 < maxVerts)
				{
					DrawTriangle(decodedVerts[idx0], decodedVerts[idx1], decodedVerts[idx2], s_currentTriggerColor, renderMode);
				}
			}
			dataPos += 4 + numPolys * 4;
		}

		// Pass 2: quads (parallelogram — 4th vertex computed)
		for (int g = 0; g < pass2QuadGroups; g++)
		{
			if (dataPos + 4 > 4096) break;

			const uint16_t* groupHeader = reinterpret_cast<const uint16_t*>(leafBytes + dataPos);
			const int numPolys = (groupHeader[0] >> 12) + 1;
			int runningBase = (int)groupHeader[1] << 10;

			for (int i = 0; i < numPolys && i < 16; i++)
			{
				const uint32_t quadData = *reinterpret_cast<const uint32_t*>(leafBytes + dataPos + 4 + i * 4);
				const int v0_offset = quadData & 0x3FF;        // 10 bits
				const int v1_delta = (quadData >> 10) & 0x1FF; // 9 bits
				const int v2_delta = (quadData >> 19) & 0x1FF; // 9 bits

				const int idx0 = runningBase + v0_offset;
				const int idx1 = idx0 + 1 + v1_delta;
				const int idx2 = idx0 + 1 + v2_delta;
				runningBase = idx0;

				if (idx0 >= 0 && idx0 < maxVerts && idx1 >= 0 && idx1 < maxVerts && idx2 >= 0 && idx2 < maxVerts)
				{
					// Parallelogram: v3 = v1 + v2 - v0
					Vector3D v3;
					v3.x = decodedVerts[idx1].x + decodedVerts[idx2].x - decodedVerts[idx0].x;
					v3.y = decodedVerts[idx1].y + decodedVerts[idx2].y - decodedVerts[idx0].y;
					v3.z = decodedVerts[idx1].z + decodedVerts[idx2].z - decodedVerts[idx0].z;

					DrawTriangle(decodedVerts[idx0], decodedVerts[idx1], decodedVerts[idx2], s_currentTriggerColor, renderMode);
					DrawTriangle(decodedVerts[idx2], decodedVerts[idx1], v3, s_currentTriggerColor, renderMode);
				}
			}
			dataPos += 4 + numPolys * 4;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw a ConvexHull leaf (type 8)
// Self-contained blob with embedded vertices, origin, and scale.
// Verified via IDA reverse engineering of sub_1402DBB20, sub_1402DCEF0, sub_1402DD190.
//
// Hull data format:
//   Byte 0:    numVertices
//   Byte 1:    extra (used in offset calculation)
//   Byte 2:    numTriSections
//   Byte 3:    numQuadSections
//   Bytes 4-19:  origin(xyz) + quantScale (4 floats, hull-local)
//   Bytes 20+:   packed int16 vertices (6 bytes each)
//   Aligned:     triangle sections (11/9/9 bit layout, same as type 4/5)
//   After tri:   quad sections (10/9/9 bit layout, parallelogram, same as type 6/7)
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawConvexHull(const CollisionModelContext_t* ctx, uint32_t childIdx, int depth)
{
	if (!ctx || !ctx->leafDataStream)
		return;

	const int renderMode = bsp_collision_debug_mode.GetInt();
	const int alpha = bsp_collision_debug_alpha.GetInt();
	const bool isTriggerMode = (bsp_trigger_debug.GetInt() != 0);

	const uint8_t* hullData = reinterpret_cast<const uint8_t*>(&ctx->leafDataStream[childIdx]);

	const int numVertices = hullData[0];
	const int extraField = hullData[1];
	const int numTriSections = hullData[2];
	const int numQuadSections = hullData[3];

	if (numVertices == 0)
		return;

	// Hull has its own origin and quantization scale at offset 4
	const float* originScale = reinterpret_cast<const float*>(hullData + 4);
	const Vector3D hullOrigin(originScale[0], originScale[1], originScale[2]);
	const float hullScale = originScale[3];

	// Pre-decode all packed int16 vertices from offset 20
	const int16_t* packedVerts = reinterpret_cast<const int16_t*>(hullData + 20);
	const int maxVerts = (numVertices < 255) ? numVertices : 255;
	Vector3D decodedVerts[256];

	// For trigger volumes, entity origin must be added (brush models are in local space).
	// For worldspawn, s_currentEntityOrigin is (0,0,0) so this is a no-op.
	const Vector3D decodeOrigin(
		s_currentEntityOrigin.x + hullOrigin.x,
		s_currentEntityOrigin.y + hullOrigin.y,
		s_currentEntityOrigin.z + hullOrigin.z);

	for (int v = 0; v < maxVerts; v++)
		decodedVerts[v] = DecodePackedVertex(packedVerts, v, decodeOrigin, hullScale);

	// Compute offset to section data (matches engine formula from sub_1402DBB20)
	// v32 = extra + 2 * numVerts; offset = 3*v32 + 20, aligned up to 4 bytes
	const int v32 = extraField + 2 * numVertices;
	const int x = 3 * v32 + 20;
	int offset = x + ((-x) & 3);

	// Process triangle sections (11/9/9 bit layout, same as type 4/5)
	for (int s = 0; s < numTriSections; s++)
	{
		const uint16_t* sectionHdr = reinterpret_cast<const uint16_t*>(hullData + offset);
		const int numPolys = ((sectionHdr[0] >> 12) & 0xF) + 1;
		const int baseVertex = sectionHdr[1];
		int runningBase = baseVertex << 10;

		const uint32_t* polyData = reinterpret_cast<const uint32_t*>(sectionHdr + 2);

		for (int i = 0; i < numPolys && i < 16; i++)
		{
			const uint32_t triData = polyData[i];
			const int v0_offset = triData & 0x7FF;
			const int v1_delta = (triData >> 11) & 0x1FF;
			const int v2_delta = (triData >> 20) & 0x1FF;

			runningBase += v0_offset;
			const int idx0 = runningBase;
			const int idx1 = idx0 + v1_delta + 1;
			const int idx2 = idx0 + v2_delta + 1;

			if (idx0 >= maxVerts || idx1 >= maxVerts || idx2 >= maxVerts)
				continue;

			const Color triColor = isTriggerMode ? s_currentTriggerColor : GetTriangleColor(childIdx, s * 16 + i, alpha);
			DrawTriangle(decodedVerts[idx0], decodedVerts[idx1], decodedVerts[idx2], triColor, renderMode);
		}

		offset += 4 + 4 * numPolys;
	}

	// Process quad sections (10/9/9 bit layout, parallelogram, same as type 6/7)
	for (int s = 0; s < numQuadSections; s++)
	{
		const uint16_t* sectionHdr = reinterpret_cast<const uint16_t*>(hullData + offset);
		const int numPolys = ((sectionHdr[0] >> 12) & 0xF) + 1;
		const int baseVertex = sectionHdr[1];
		int runningBase = baseVertex << 10;

		const uint32_t* polyData = reinterpret_cast<const uint32_t*>(sectionHdr + 2);

		for (int i = 0; i < numPolys && i < 16; i++)
		{
			const uint32_t triData = polyData[i];
			const int v0_offset = triData & 0x3FF;
			const int v1_delta = (triData >> 10) & 0x1FF;
			const int v2_delta = (triData >> 19) & 0x1FF;

			runningBase += v0_offset;
			const int idx0 = runningBase;
			const int idx1 = idx0 + v1_delta + 1;
			const int idx2 = idx0 + v2_delta + 1;

			if (idx0 >= maxVerts || idx1 >= maxVerts || idx2 >= maxVerts)
				continue;

			const Vector3D& v0 = decodedVerts[idx0];
			const Vector3D& v1 = decodedVerts[idx1];
			const Vector3D& v2 = decodedVerts[idx2];
			const Vector3D v3(v1.x + v2.x - v0.x, v1.y + v2.y - v0.y, v1.z + v2.z - v0.z);

			const Color triColor = isTriggerMode ? s_currentTriggerColor : GetTriangleColor(childIdx, (numTriSections + s) * 16 + i * 2, alpha);
			const Color triColor2 = isTriggerMode ? s_currentTriggerColor : GetTriangleColor(childIdx, (numTriSections + s) * 16 + i * 2 + 1, alpha);
			DrawTriangle(v0, v1, v2, triColor, renderMode);
			DrawTriangle(v2, v1, v3, triColor2, renderMode);
		}

		offset += 4 + 4 * numPolys;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw a Bundle leaf (type 3)
// A container that holds sub-entries of various types and dispatches to them.
// Verified via IDA reverse engineering of sub_1402DBD40.
//
// Bundle data format (in uint32_t units from leafDataStream):
//   dword 0:          count of sub-entries
//   dwords 1..count:  per-entry metadata packed as:
//     bits 0-7:   content mask index
//     bits 8-15:  child type (3=bundle, 4-7=poly, 8=convexhull, 9=staticprop)
//     bits 16-31: data size (uint16, in dwords)
//   After metadata:   sub-entry data blobs (back to back)
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawBundleLeaf(const CollisionModelContext_t* ctx, uint32_t childIdx,
	const Vector3D& origin, float quantScale, int depth)
{
	if (!ctx || childIdx == 0 || !ctx->leafDataStream)
		return;

	const uint32_t* bundleData = &ctx->leafDataStream[childIdx];
	const uint32_t count = bundleData[0];

	if (count == 0 || count > 256)
		return;

	// Data starts after the header: 1 dword count + count dwords metadata
	uint32_t dataOffset = count + 1; // in dwords from bundleData start

	for (uint32_t i = 0; i < count; i++)
	{
		const uint32_t meta = bundleData[1 + i];
		const int subType = (meta >> 8) & 0xFF;
		const uint32_t dataSize = (meta >> 16) & 0xFFFF; // in dwords

		// Compute the absolute index into leafDataStream for this sub-entry
		const uint32_t subChildIdx = childIdx + dataOffset;

		if (subType == 3)
		{
			DrawBundleLeaf(ctx, subChildIdx, origin, quantScale, depth);
		}
		else if (subType >= 4 && subType <= 7)
		{
			DrawLeafTriangles(ctx, subChildIdx, subType, origin, quantScale, depth);
		}
		else if (subType == 8)
		{
			DrawConvexHull(ctx, subChildIdx, depth);
		}

		dataOffset += dataSize;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Get the bounds of a BVH node child
// The bounds in BVH nodes are stored as int16 quantized values
// The engine uses SIMD unpacking that shifts int16 to high 16 bits of int32,
// effectively multiplying by 65536, then multiplies by decodeScale and adds origin.
// Formula: world = int16_value * 65536 * decodeScale + origin
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::GetNodeBounds(const CollBvh4Node_t* node, int childIndex, float scale,
	const Vector3D& origin, Vector3D& outMins, Vector3D& outMaxs)
{
	// The engine's SIMD code does: unpacklo_epi16(0, int16) which shifts to high 16 bits
	// This effectively multiplies by 65536, then multiplies by decodeScale
	// So the effective scale is: decodeScale * 65536
	const float effectiveScale = scale * 65536.0f;

	// BVH nodes store bounds as int16 values per axis:
	// minMax[axis][0][child] = min
	// minMax[axis][1][child] = max
	// world = int16_value * effectiveScale + origin
	outMins.x = (float)node->GetMin(0, childIndex) * effectiveScale + origin.x;
	outMins.y = (float)node->GetMin(1, childIndex) * effectiveScale + origin.y;
	outMins.z = (float)node->GetMin(2, childIndex) * effectiveScale + origin.z;
	
	outMaxs.x = (float)node->GetMax(0, childIndex) * effectiveScale + origin.x;
	outMaxs.y = (float)node->GetMax(1, childIndex) * effectiveScale + origin.y;
	outMaxs.z = (float)node->GetMax(2, childIndex) * effectiveScale + origin.z;
}

//-----------------------------------------------------------------------------
// Purpose: Check if a node child intersects with an AABB
//-----------------------------------------------------------------------------
bool CBSPCollisionDebug::NodeIntersectsAABB(const CollBvh4Node_t* node, int childIndex, float scale,
	const Vector3D& origin, const Vector3D& filterMins, const Vector3D& filterMaxs)
{
	Vector3D nodeMins, nodeMaxs;
	GetNodeBounds(node, childIndex, scale, origin, nodeMins, nodeMaxs);

	// AABB intersection test
	if (nodeMins.x > filterMaxs.x || nodeMaxs.x < filterMins.x)
		return false;
	if (nodeMins.y > filterMaxs.y || nodeMaxs.y < filterMins.y)
		return false;
	if (nodeMins.z > filterMaxs.z || nodeMaxs.z < filterMins.z)
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Recursively draw BVH nodes
// Based on CollBvh_VisitNodes_r from engine decompilation
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawNodeRecursive(const CollisionModelContext_t* ctx, const CollBvh4Node_t* nodes, int nodeIndex,
	const Vector3D& origin, float scale, int depth, int maxDepth,
	const Vector3D& filterMins, const Vector3D& filterMaxs)
{
	if (maxDepth >= 0 && depth > maxDepth)
		return;

	if (nodeIndex < 0)
		return;

	const CollBvh4Node_t* node = &nodes[nodeIndex];

	// Child type extraction (from packedMetaData[2] and [3]):
	//   type0 = packedMetaData[2] & 0xF
	//   type1 = (packedMetaData[2] >> 4) & 0xF
	//   type2 = packedMetaData[3] & 0xF
	//   type3 = (LOBYTE(packedMetaData[3]) >> 4)
	//
	// Child index extraction:
	//   index[i] = packedMetaData[i] >> 8
	//
	// Type meanings:
	//   0 = internal node (recurse with childIndex)
	//   1 = empty/skip
	//   2+ = leaf with polygon data

	for (int i = 0; i < 4; i++)
	{
		const int childType = node->GetChildType(i);
		const uint32_t childIdx = node->GetChildIndex(i);

		// Type 0 = internal node, Type >= 1 = leaf (or empty if type==1)
		const bool isInternalNode = (childType == 0);
		const bool isLeaf = (childType >= 2);  // type 1 is empty/skip, type 2+ is leaf

		// Skip empty children (type 1 or internal with index 0)
		if (childType == 1)
			continue;
		if (childIdx == 0 && isInternalNode)
			continue;

		// Check if this child intersects our filter AABB
		if (!NodeIntersectsAABB(node, i, scale, origin, filterMins, filterMaxs))
			continue;

		// For leaves, draw the actual collision triangles
		// childIdx == 0 is valid for type 8 (convex hull) - data starts at stream offset 0
		if (isLeaf)
		{
			if (childType == 3)
			{
				DrawBundleLeaf(ctx, childIdx, origin, scale, depth);
			}
			else if (childType >= 4 && childType <= 7)
			{
				DrawLeafTriangles(ctx, childIdx, childType, origin, scale, depth);
			}
			else if (childType == 8)
			{
				DrawConvexHull(ctx, childIdx, depth);
			}
		}
		else if (isInternalNode)
		{
			// Recurse into internal nodes
			if (childIdx > 0)
			{
				DrawNodeRecursive(ctx, nodes, childIdx, origin, scale, depth + 1, maxDepth, filterMins, filterMaxs);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw BVH nodes around a specific position
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawBVHNodesAroundPoint(const Vector3D& pos, float radius, int maxDepth)
{
	// Use model index 0 for the world BSP
	if (!g_ppCollisionModelContexts || !*g_ppCollisionModelContexts)
		return;

	const CollisionModelContext_t* ctx = *g_ppCollisionModelContexts;
	
	if (!ctx->bvhNodes)
		return;

	// Get scale from the collision context - the BVH uses int16 bounds that need to be scaled
	float scale = ctx->quantScale;
	if (scale <= 0.0f)
		scale = 1.0f;

	const Vector3D origin(ctx->scaleOriginX, ctx->scaleOriginY, ctx->scaleOriginZ);
	const Vector3D filterMins = pos - Vector3D(radius, radius, radius);
	const Vector3D filterMaxs = pos + Vector3D(radius, radius, radius);

	// Start from root node (index 0)
	DrawNodeRecursive(ctx, ctx->bvhNodes, 0, origin, scale, 0, maxDepth, filterMins, filterMaxs);
}

//-----------------------------------------------------------------------------
// Purpose: Draw BVH for a specific brush model index with a given color
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawBrushModelBVH(int modelIndex, const Color& color, const Vector3D& entityOrigin)
{
	s_currentEntityOrigin = entityOrigin;
	s_currentTriggerColor = color;
	if (!g_ppCollisionModelContexts || !*g_ppCollisionModelContexts)
		return;

	const CollisionModelContext_t* ctx = reinterpret_cast<const CollisionModelContext_t*>(
		reinterpret_cast<const char*>(*g_ppCollisionModelContexts) + 72 * modelIndex);

	// Bounds check against engine's brush model count
	if (g_pNumBrushModels && modelIndex >= *g_pNumBrushModels)
		return;

	if (!ctx->bvhNodes || !ctx->leafDataStream)
		return;

	const Vector3D origin(ctx->scaleOriginX, ctx->scaleOriginY, ctx->scaleOriginZ);
	float scale = ctx->quantScale;
	if (scale <= 0.0f)
		scale = 1.0f;

	// Get player position for distance filter
	Vector3D playerPos(0, 0, 0);
	if (g_pEngineClient && g_pClientEntityList)
	{
		const int localPlayerIndex = g_pEngineClient->GetLocalPlayer();
		if (localPlayerIndex > 0)
		{
			const IClientEntity* pLocalPlayer = g_pClientEntityList->GetClientEntity(localPlayerIndex);
			if (pLocalPlayer)
				playerPos = pLocalPlayer->GetAbsOrigin();
		}
	}

	// Use huge filter bounds — trigger BVHs are small (often 1 node) and
	// the context origin/scale doesn't match world coords for node bounds.
	// Distance filtering is done at the model level, not per-node.
	const Vector3D filterMins(-1e9f, -1e9f, -1e9f);
	const Vector3D filterMaxs(1e9f, 1e9f, 1e9f);

	DrawNodeRecursive(ctx, ctx->bvhNodes, 0, origin, scale, 0, -1, filterMins, filterMaxs);
}

//-----------------------------------------------------------------------------
// Purpose: Render trigger volumes colored by type
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::RenderTriggerVolumes()
{
	const int mode = bsp_trigger_debug.GetInt();
	if (mode == 0)
		return;

	if (!g_ppCollisionModelContexts || !*g_ppCollisionModelContexts)
		return;

	// Mark collision data on first render after map load
	if (g_numTriggerVolumes > 0 && !s_collisionMarked)
	{
		s_collisionMarked = true;
		int collisionCount = 0;
		const int maxIdx = (g_pNumBrushModels && *g_pNumBrushModels < g_numTriggerVolumes)
			? *g_pNumBrushModels : g_numTriggerVolumes;
		for (int i = 1; i < maxIdx; i++)
		{
			if (g_triggerVolumes[i].type == TriggerType_e::NONE)
				continue;
			const CollisionModelContext_t* ctx = reinterpret_cast<const CollisionModelContext_t*>(
				reinterpret_cast<const char*>(*g_ppCollisionModelContexts) + 72 * i);
			if (ctx->bvhNodes)
			{
				g_triggerVolumes[i].hasCollision = true;
				collisionCount++;
			}
		}
		DevMsg(eDLL_T::ENGINE, "TriggerDebug: %d volumes mapped, %d with collision data\n",
			g_numTriggerVolumes, collisionCount);
	}

	if (!g_pClientState || !g_pClientState->IsActive())
		return;

	const int alpha = bsp_collision_debug_alpha.GetInt();

	for (int i = 1; i < g_numTriggerVolumes; i++)
	{
		const TriggerVolumeInfo_t& info = g_triggerVolumes[i];
		if (!info.hasCollision)
			continue;

		// Filter by mode (-1 = all, 1-9 = specific type)
		bool draw = false;
		if (mode == -1)
		{
			draw = (info.type != TriggerType_e::NONE);
		}
		else
		{
			draw = ((int)info.type == mode);
		}

		if (!draw)
			continue;

		const Color color = GetTriggerColor(info.type, alpha);
		const Vector3D entOrigin(info.originX, info.originY, info.originZ);
		DrawBrushModelBVH(i, color, entOrigin);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Main render entry point
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::Render()
{
	const int debugMode = bsp_collision_debug.GetInt();
	if (debugMode <= 0)
		return;

	if (!g_pClientState || !g_pClientState->IsActive())
	{
		static bool sWarnedClientState = false;
		if (!sWarnedClientState)
		{
			Warning(eDLL_T::ENGINE, "BSP Collision Debug: Client not active\n");
			sWarnedClientState = true;
		}
		return;
	}

	if (!g_ppCollisionModelContexts)
	{
		// Pattern not found - feature disabled for this game version
		return;
	}

	if (!*g_ppCollisionModelContexts)
	{
		// Not loaded yet - this is normal before map load
		return;
	}

	const CollisionModelContext_t* ctx = *g_ppCollisionModelContexts;
	if (!ctx || !ctx->bvhNodes)
		return;

	// Get player position for filtering
	Vector3D playerPos(0, 0, 0);

	// Try to get the local player's position
	if (g_pEngineClient && g_pClientEntityList)
	{
		const int localPlayerIndex = g_pEngineClient->GetLocalPlayer();
		if (localPlayerIndex > 0)
		{
			const IClientEntity* pLocalPlayer = g_pClientEntityList->GetClientEntity(localPlayerIndex);
			if (pLocalPlayer)
			{
				playerPos = pLocalPlayer->GetAbsOrigin();
			}
		}
	}

	const float radius = bsp_collision_debug_radius.GetFloat();

	DrawBVHNodesAroundPoint(playerPos, radius, -1);
}

//-----------------------------------------------------------------------------
// Purpose: Console command to toggle BSP collision debug
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::CC_DrawBSPCollision(const CCommand& args)
{
	if (args.ArgC() < 2)
	{
		Msg(eDLL_T::ENGINE, "Usage: bsp_collision_debug <0/1>\n");
		Msg(eDLL_T::ENGINE, "  0 = off\n");
		Msg(eDLL_T::ENGINE, "  1 = draw collision triangles\n");
		return;
	}

	const int mode = atoi(args.Arg(1));
	bsp_collision_debug.SetValue(mode);
}

#endif // !DEDICATED

//-----------------------------------------------------------------------------
// Signature scanning to find collision data pointer
// Defined outside DEDICATED block so it runs on both client and server
//-----------------------------------------------------------------------------
void VBSPCollisionDebug::GetVar(void) const
{
#ifndef DEDICATED
	// The collision model contexts array is accessed in a function like:
	//   mov eax, edx             ; 8B C2
	//   lea rcx, [rax+rax*8]     ; 48 8D 0C C0
	//   mov rax, cs:qword_XXXX   ; 48 8B 05 XX XX XX XX <-- pointer to collision contexts
	//   lea rax, [rax+rcx*8]     ; 48 8D 04 C8
	//   retn                     ; C3
	//
	// This function returns: qword_1634F1638 + 72 * index
	// The 72-byte CollisionModelContext_t is the per-model collision context struct
	CMemory result = Module_FindPattern(g_GameDll, "8B C2 48 8D 0C C0 48 8B 05 ?? ?? ?? ?? 48 8D 04 C8 C3");
	
	if (result)
	{
		// The RIP-relative address is at offset 0x6 from pattern start (inside the mov rax instruction)
		// mov rax, [rip+offset] is: 48 8B 05 XX XX XX XX
		// Pattern: 8B C2 48 8D 0C C0 [48 8B 05 XX XX XX XX] 48 8D 04 C8 C3
		// Offset to 48 8B 05 is 6, relative address at 6+3=9, instruction ends at 6+7=13
		g_ppCollisionModelContexts = result.Offset(0x6).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CollisionModelContext_t**>();
	}

	// g_pNumBrushModels (dword_1634F14E8) - referenced in CM_ParseStarCollFromEntities
	// at loc_14020FE25: 44 8B 05 XX XX XX XX  mov r8d, cs:dword_1634F14E8
	// Scan from resolved function pointer (offset 0x245 into function, scan range 1024)
	if (CM_ParseStarCollFromEntities)
	{
		CMemory fnMem(CM_ParseStarCollFromEntities);
		// Search for: mov r8d, cs:[rip+disp32] followed by cmp r13d, r8d; jb
		// 44 8B 05 ?? ?? ?? ?? 45 3B E8 72
		CMemory brushModelRef = fnMem.FindPattern("44 8B 05 ?? ?? ?? ?? 45 3B E8 72", CMemory::Direction::DOWN, 1024);
		if (brushModelRef)
		{
			g_pNumBrushModels = brushModelRef.ResolveRelativeAddressSelf(0x3, 0x7).RCast<int32_t*>();
		}
	}
#endif // !DEDICATED
}

//-----------------------------------------------------------------------------
// Purpose: Attach/detach the StarColl parsing detour
//-----------------------------------------------------------------------------
void VBSPCollisionDebug::Detour(const bool bAttach) const
{
#ifndef DEDICATED
	if (CM_ParseStarCollFromEntities)
	{
		DetourSetup(&CM_ParseStarCollFromEntities, &CM_ParseStarCollFromEntities_Detour, bAttach);
	}
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Register the detour - this must be in the .cpp file, not the header
// Defined outside DEDICATED block so ConVars are registered on both client and server
REGISTER(VBSPCollisionDebug);
