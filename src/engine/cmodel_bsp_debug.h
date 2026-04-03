//=============================================================================//
//
// Purpose: BSP collision debug rendering
//
// $NoKeywords: $
//=============================================================================//
#pragma once
#include "mathlib/vector.h"
#include "public/cmodel.h"

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class ConVar;

//-----------------------------------------------------------------------------
// ConVars for BSP collision debug - declared outside DEDICATED so they exist on server
//-----------------------------------------------------------------------------
extern ConVar bsp_collision_debug;
extern ConVar bsp_collision_debug_mode;
extern ConVar bsp_collision_debug_radius;
extern ConVar bsp_collision_debug_alpha;

#ifndef DEDICATED

//-----------------------------------------------------------------------------
// 
// Memory layout (64 bytes):
//   minMax[3][2][4] - 48 bytes: int16 bounds per axis/minmax/child
//     [axis 0=X,1=Y,2=Z][minmax 0=min,1=max][child 0-3]
//   packedMetaData[4] - 16 bytes: child type/index info
//
// Child type extraction (from CollBvh_VisitNodes_r):
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
//-----------------------------------------------------------------------------
struct CollBvh4Node_t
{
	int16_t minMax[3][2][4];        // [axis][min=0/max=1][child 0-3]
	uint32_t packedMetaData[4];     // child info
	
	// Helper to get min/max for a specific axis and child
	inline int16_t GetMin(int axis, int child) const { return minMax[axis][0][child]; }
	inline int16_t GetMax(int axis, int child) const { return minMax[axis][1][child]; }
	
	// Get child type (0=internal node, 1=empty, 2+=leaf)
	inline int GetChildType(int child) const {
		if (child < 2)
			return (packedMetaData[2] >> (child * 4)) & 0xF;
		else
			return (packedMetaData[3] >> ((child - 2) * 4)) & 0xF;
	}
	
	// Get child index (node index for internal, leaf data offset for leaf)
	inline uint32_t GetChildIndex(int child) const {
		return packedMetaData[child] >> 8;
	}
};
static_assert(sizeof(CollBvh4Node_t) == 64);

//-----------------------------------------------------------------------------
// CollBvh4_s - Full collision BVH structure (88 bytes)
//
// This is the main collision structure containing the BVH tree and metadata.
// context struct instead (CollisionModelContext_t below).
//-----------------------------------------------------------------------------
struct CollBvh4_t
{
	const CollBvh4Node_t* nodes;    // +0x00 (8 bytes)
	const void* surfProps;          // +0x08 (8 bytes) CollSurfProps_s*
	const void* skinInfos;          // +0x10 (8 bytes) CollSkinInfo_s*
	const uint32_t* leafDataStream; // +0x18 (8 bytes)
	const void* verts;              // +0x20 (8 bytes)
	const uint32_t* contentsMask;   // +0x28 (8 bytes)
	const char* surfPropNameBuf;    // +0x30 (8 bytes)
	const void* heightfields;       // +0x38 (8 bytes) CollHeightfield_s*
	uint32_t bvhFlags;              // +0x40 (4 bytes)
	uint8_t skinCount;              // +0x44 (1 byte)
	uint8_t meshGroupCount;         // +0x45 (1 byte)
	uint16_t _padding;              // +0x46 (2 bytes)
	float originX;                  // +0x48 (4 bytes) - float3 origin.x
	float originY;                  // +0x4C (4 bytes) - float3 origin.y
	float originZ;                  // +0x50 (4 bytes) - float3 origin.z
	float decodeScale;              // +0x54 (4 bytes) - quantization scale
};
static_assert(sizeof(CollBvh4_t) == 88);

//-----------------------------------------------------------------------------
// CollisionModelContext_t - 72-byte collision context
//
// This is the per-model collision context structure used in older game versions.
// Array accessed via qword_1634F1638 + 72 * modelIndex
//
// Populated from packed collision data as shown in sub_14020FBE0:
//   ctx[0] = data + data[5]     (BVH nodes)
//   ctx[8] = data + data[1]     (surface properties)
//   ctx[16] = data + data[7]    (leaf data stream)
//   ctx[24] = data + data[6]    (vertices - packed or float depending on model)
//   ctx[32] = data + data[0]    (content masks)
//   ctx[40] = data + data[2]    (unknown - may be unused)
//   ctx[48] = data[4]           (int)
//   ctx[52] = 0                 (int)
//   ctx[56] = *(qword*)(data+32)(scale origin as packed float2 or 2 floats)
//   ctx[64] = data[10]          (float - scale origin z)
//   ctx[68] = data[11]          (float - quantization scale)
//
// Note: In the 88-byte CollBvh4_s struct, offset 32 holds vertices and the
// bvhFlags bit 0 indicates packed vs float. The 72-byte struct may use
// offset 24 for vertices similarly.
//-----------------------------------------------------------------------------
struct CollisionModelContext_t
{
	const CollBvh4Node_t* bvhNodes;  // +0x00 offset 0 - pointer to BVH nodes
	const void* surfProps;           // +0x08 offset 8 - surface properties
	const uint32_t* leafDataStream;  // +0x10 offset 16 - pointer to leaf data
	const char* verts;               // +0x18 offset 24 - vertices (packed int16 or float depending on model)
	const uint32_t* contentMasks;    // +0x20 offset 32 - content mask array
	const void* unk3;                // +0x28 offset 40 - unknown (may be unused)
	int unk4;                        // +0x30 offset 48
	int unk5;                        // +0x34 offset 52 - always 0
	float scaleOriginX;              // +0x38 offset 56 - BVH scale origin X
	float scaleOriginY;              // +0x3C offset 60 - BVH scale origin Y
	float scaleOriginZ;              // +0x40 offset 64 - BVH scale origin Z
	float quantScale;                // +0x44 offset 68 - quantization scale
};
static_assert(sizeof(CollisionModelContext_t) == 72);

//-----------------------------------------------------------------------------
// StarCollHeader_t - On-disk collision data header for brush entities
//
// Stored in entity *coll0..*collN keys as base64-encoded binary data.
// Parsed by sub_14020FBE0 which base64-decodes the concatenated keys,
// then interprets the first 48 bytes as offsets into the data blob:
//
//   int32[0]  = contentMasksOffset    → CollisionModelContext_t.contentMasks
//   int32[1]  = surfPropsOffset       → CollisionModelContext_t.surfProps
//   int32[2]  = surfPropNameBufOffset → CollisionModelContext_t.unk3
//   int32[3]  = (unused / reserved)
//   int32[4]  = flags                 → CollisionModelContext_t.unk4
//   int32[5]  = bvhNodesOffset        → CollisionModelContext_t.bvhNodes
//   int32[6]  = vertsOffset           → CollisionModelContext_t.verts
//   int32[7]  = leafDataStreamOffset  → CollisionModelContext_t.leafDataStream
//   float[8]  = scaleOriginX          → CollisionModelContext_t.scaleOriginX
//   float[9]  = scaleOriginY          → CollisionModelContext_t.scaleOriginY
//   int32[10] = scaleOriginZ          → CollisionModelContext_t.scaleOriginZ
//   int32[11] = quantScale            → CollisionModelContext_t.quantScale
//
// All offsets are relative to the start of the header itself.
// After the header: material string (null-terminated, padded to 4-byte alignment),
// then BVH nodes, vertices, leaf data, content masks, etc.
//
// The BVH data uses the same CollBvh4Node_t format as worldspawn collision.
//-----------------------------------------------------------------------------
struct StarCollHeader_t
{
	int32_t contentMasksOffset;      // [0]  offset to content mask array
	int32_t surfPropsOffset;         // [1]  offset to surface properties
	int32_t surfPropNameBufOffset;   // [2]  offset to surface property name buffer
	int32_t _reserved;               // [3]  unused
	int32_t flags;                   // [4]  collision flags
	int32_t bvhNodesOffset;          // [5]  offset to CollBvh4Node_t array
	int32_t vertsOffset;             // [6]  offset to vertices (float3 or packed int16)
	int32_t leafDataStreamOffset;    // [7]  offset to BVH leaf data
	float   scaleOriginX;            // [8]  BVH decode origin X
	float   scaleOriginY;            // [9]  BVH decode origin Y
	float   scaleOriginZ;            // [10] BVH decode origin Z
	float   quantScale;              // [11] BVH quantization scale
};
static_assert(sizeof(StarCollHeader_t) == 48);

//-----------------------------------------------------------------------------
// Full collision BSP data structure
// Loaded by sub_14020F2F0 from the BSP file
//-----------------------------------------------------------------------------
struct CollisionBSPData_t
{
	char name[96];                   // +0x00  offset 0 - map name (0x60 bytes)
	int version;                     // +0x60  offset 96 - BSP version
	int numVertices;                 // +0x64  offset 100
	int numPackedVertices;           // +0x68  offset 104
	int numContentMasks;             // +0x6C  offset 108
	int numSurfaceProperties;        // +0x70  offset 112
	int numLeafData;                 // +0x74  offset 116
	int numBvhNodes;                 // +0x78  offset 120
	int numBvhLeafData;              // +0x7C  offset 124
	void* vertices;                  // +0x80  offset 128 - Vector3D array
	void* packedVertices;            // +0x88  offset 136 - packed 6-byte vertices
	void* contentMasks;              // +0x90  offset 144
	void* surfaceProperties;         // +0x98  offset 152 - 8 bytes each
	void* leafData;                  // +0xA0  offset 160
	CollBvh4Node_t* bvhNodes;        // +0xA8  offset 168 - BVH node array (64 bytes each)
	void* bvhLeafData;               // +0xB0  offset 176
	// ... more fields follow
};

//-----------------------------------------------------------------------------
// Trigger volume types for debug rendering
//-----------------------------------------------------------------------------
enum class TriggerType_e
{
	NONE = 0,
	OUT_OF_BOUNDS,     // 1
	SLIP,              // 2
	HURT,              // 3
	SOUNDSCAPE,        // 4
	NO_ZIPLINE,        // 5
	NO_GRAPPLE,        // 6
	WARP_GATE,         // 7 (phase runner teleporters)
	SKYDIVE,           // 8 (rift exits)
	MULTIPLE_OTHER,    // 9 (other trigger_multiple)
	OTHER_TRIGGER,     // 10
	NON_TRIGGER        // 11 (func_brush, etc.)
};

//-----------------------------------------------------------------------------
// Per-brush-model trigger info, populated during entity lump parsing
//-----------------------------------------------------------------------------
struct TriggerVolumeInfo_t
{
	TriggerType_e type;
	bool hasCollision;   // true if CollisionModelContext_t was populated for this model
	float originX, originY, originZ;  // entity origin offset
};

// Max brush models we track (matches engine limit)
constexpr int MAX_TRIGGER_VOLUMES = 4096;

// Trigger volume info array, indexed by brush model index
// Populated by our CM_ParseStarCollFromEntities detour
inline TriggerVolumeInfo_t g_triggerVolumes[MAX_TRIGGER_VOLUMES] = {};
inline int g_numTriggerVolumes = 0;

//-----------------------------------------------------------------------------
// Debug drawing modes
//-----------------------------------------------------------------------------
enum class BVHDebugMode_e
{
	NONE = 0,
	NODES_ONLY,           // Draw BVH node bounding boxes
	LEAVES_ONLY,          // Draw only leaf nodes
	ALL,                  // Draw all nodes
	TRACE_PATH,           // Draw nodes visited during trace
	DEPTH_COLORED         // Color nodes by depth in tree
};

//-----------------------------------------------------------------------------
// BSP collision debug interface
//-----------------------------------------------------------------------------
class CBSPCollisionDebug
{
public:
	static void Init();
	static void Shutdown();

	// Main render entry point - called from debug overlay system
	static void Render();

	// Draw collision triangles around a specific position
	static void DrawBVHNodesAroundPoint(const Vector3D& pos, float radius, int maxDepth = -1);

	// Console command
	static void CC_DrawBSPCollision(const CCommand& args);

	// Trigger volume rendering
	static void RenderTriggerVolumes();
	static void DrawBrushModelBVH(int modelIndex, const Color& color, const Vector3D& entityOrigin);

private:
	static void DrawNodeRecursive(const CollisionModelContext_t* ctx, const CollBvh4Node_t* nodes, int nodeIndex,
		const Vector3D& origin, float scale, int depth, int maxDepth,
		const Vector3D& filterMins, const Vector3D& filterMaxs);

	static bool NodeIntersectsAABB(const CollBvh4Node_t* node, int childIndex, float scale,
		const Vector3D& origin, const Vector3D& filterMins, const Vector3D& filterMaxs);

	static void GetNodeBounds(const CollBvh4Node_t* node, int childIndex, float scale,
		const Vector3D& origin, Vector3D& outMins, Vector3D& outMaxs);

	// Triangle geometry decoding and drawing
	static Vector3D DecodePackedVertex(const int16_t* packedVerts, int vertexIndex,
		const Vector3D& origin, float quantScale);
	static Vector3D DecodeFloatVertex(const float* floatVerts, int vertexIndex,
		const Vector3D& origin);
	static void DrawTriangle(const Vector3D& v0, const Vector3D& v1, const Vector3D& v2, const Color& color, int renderMode = 1);
	static void DrawLeafTriangles(const CollisionModelContext_t* ctx, uint32_t childIdx,
		int childType, const Vector3D& origin, float quantScale, int depth);
};

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
// Pointer to pointer to the collision model context array, resolved at runtime
// This is an array of CollisionModelContext_t (72-byte structures), one per collision model
// Accessed via: g_ppCollisionModelContexts + 72 * modelIndex
inline CollisionModelContext_t** g_ppCollisionModelContexts = nullptr;

// Number of brush models (trigger volumes, func_brush, etc.)
// Referenced as dword_1634F14E8 in sub_14020FBE0
inline int32_t* g_pNumBrushModels = nullptr;

//-----------------------------------------------------------------------------
// CM_ParseStarCollFromEntities - sub_14020FBE0
//
// Parses entity lump text, extracts base64-encoded *coll keys (StarColl data),
// and populates the CollisionModelContext_t array for brush model collision.
//
// Parameters:
//   a1 - pointer to entity lump text buffer
//   a2 - length of entity lump text
//
// Returns: new length of entity lump text (with *coll keys stripped out)
//
// For each entity with *coll keys:
//   1. Base64-decodes concatenated *coll0..*collN values
//   2. Reads the model key (*N) to get brush model index
//   3. Interprets decoded data as StarCollHeader_t (48 bytes of offsets)
//   4. Populates CollisionModelContext_t at g_ppCollisionModelContexts[modelIndex]
//      by adding each header offset to the base data pointer
//-----------------------------------------------------------------------------
inline int64_t(*CM_ParseStarCollFromEntities)(char* entityLumpText, int entityLumpLength);

#endif // !DEDICATED

///////////////////////////////////////////////////////////////////////////////
// VBSPCollisionDebug - IDetour class for resolving pointers at runtime
// Defined outside DEDICATED block so REGISTER macro works on both client/server
///////////////////////////////////////////////////////////////////////////////
class VBSPCollisionDebug : public IDetour
{
	virtual void GetAdr(void) const
	{
#ifndef DEDICATED
		LogVarAdr("g_ppCollisionModelContexts", g_ppCollisionModelContexts);
		LogVarAdr("g_pNumBrushModels", g_pNumBrushModels);
		LogFunAdr("CM_ParseStarCollFromEntities", CM_ParseStarCollFromEntities);
#endif
	}
	virtual void GetFun(void) const
	{
#ifndef DEDICATED
		// sub_14020FBE0 - parses *coll keys from entity lump text,
		// base64-decodes StarColl data, and populates CollisionModelContext_t
		// Pattern scan for sub_14020FBE0 (CM_ParseStarCollFromEntities)
		// Anchor on unique sequence: movsxd rbx,edx; lea rsi,[rip+??]; mov rcx,rsi; mov [rsp+?],r13; mov edx,400h
		// Then subtract 0x16 bytes to get to the function start
		{
			CMemory mid = Module_FindPattern(g_GameDll, "48 63 DA 48 8D 35 ?? ?? ?? ?? 48 8B CE 4C 89 6C 24 ?? BA 00 04 00 00");
			if (mid)
				CM_ParseStarCollFromEntities = mid.Offset(-0x16).RCast<decltype(CM_ParseStarCollFromEntities)>();
		}
#endif
	}
	virtual void GetVar(void) const;
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
