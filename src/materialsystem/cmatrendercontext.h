#ifndef MATRENDERCONTEXT_H
#define MATRENDERCONTEXT_H
#include "imaterial.h"

struct VertexInputFormat_s
{
    VertexInputFormat_s()
    {
        *(u64*)this = 0;
    }

    u64 position3d : 1; // Use 3D positions.
    u64 position2d : 1; // Use 2D positions.
    u64 unknown1 : 1;
    u64 unknown2 : 1;
    u64 color : 1; // Use colors (packed into single u32).
    u64 unknown3 : 1;
    u64 colorHdr : 1; // Use HDR colors(packed into single u64).
    u64 unknown4 : 1;
    u64 normal : 1; // Use 3D normals.
    u64 normalPacked : 1; // Use quantized normals (packed into single u32).
    u64 biNormal : 1; // Use bi-normals.
    u64 tangent : 1; // Use tangents.
    u64 blendIndex : 1; // Use blend indices.
    u64 blendWeight : 1; // Use blend weights.
    u64 blendWeightPacked : 1; // Use packed blend weights (packed into 2x s16).
    u64 unknown5 : 1;
    u64 unknown6 : 1;
    u64 unknown7 : 1;
    u64 unknown8 : 1;
    u64 unknown9 : 1;
    u64 unknown10 : 1;
    u64 unknown11 : 1;
    u64 unknown12 : 1;
    u64 unknown13 : 1;
    u64 unknown14 : 1;
    u64 texCoordFlags : 8; // The texture UV flags.
};

struct DirectDrawParams_s
{
    // 64-bit format flags, see function Gfx_CreateInputLayout.
    // s_currentFormatFlags caches the last used format flags.
    VertexInputFormat_s vertexFormat;
    int vertexStructSize;
    int vertexBlockIndex;
    int vertexBufferOffset;
    int vertexCount;
};

class CMatRenderContext
{
public:

    inline void EndRenderer()
    {
        const static int index = 1;
        CallVFunc<void>(index, this);
    }

    inline void Bind(IMaterial* const material)
    {
        const static int index = 19;
        CallVFunc<void>(index, this, material);
    }

    inline void* GetDynamicMesh(const int vertexCount, DirectDrawParams_s* const drawParams, const int unknown)
    {
        const static int index = 83;
        return CallVFunc<void*>(index, this, vertexCount, drawParams, unknown);
    }

    inline void EndDynamicMesh(const int vertexCount)
    {
        const static int index = 85;
        return CallVFunc<void>(index, this, vertexCount);
    }

    inline void DrawTriangleList(DirectDrawParams_s* const drawParams, ID3D11SamplerState* const samplerState, const int unknown)
    {
        const static int index = 98;
        return CallVFunc<void>(index, this, drawParams, samplerState, unknown);
    }

    inline void DrawTriangleStrip(DirectDrawParams_s* const drawParams, ID3D11SamplerState* const samplerState, const int unknown)
    {
        const static int index = 99;
        return CallVFunc<void>(index, this, drawParams, samplerState, unknown);
    }

    inline void DrawLineList(DirectDrawParams_s* const drawParams, ID3D11SamplerState* const samplerState, const int unknown)
    {
        const static int index = 102;
        return CallVFunc<void>(index, this, drawParams, samplerState, unknown);
    }

    inline void DrawPointList(DirectDrawParams_s* const drawParams, ID3D11SamplerState* const samplerState, const int unknown)
    {
        const static int index = 103;
        return CallVFunc<void>(index, this, drawParams, samplerState, unknown);
    }
};

#endif // MATRENDERCONTEXT_H
