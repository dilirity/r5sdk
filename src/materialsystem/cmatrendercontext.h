#ifndef MATRENDERCONTEXT_H
#define MATRENDERCONTEXT_H
#include "materialsystem/imaterial.h"
#include "materialsystem/imatrendercontext.h"

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

    inline void* GetDynamicVertexBuffer(const int vertexCount, DirectDrawVertexParams_s* const drawParams, const int unknown)
    {
        const static int index = 83;
        return CallVFunc<void*>(index, this, vertexCount, drawParams, unknown);
    }

    inline void EndDynamicVertexBuffer(const int vertexCount)
    {
        const static int index = 85;
        return CallVFunc<void>(index, this, vertexCount);
    }

    inline void* GetDynamicIndexBuffer(const int indexCount, DirectDrawIndexParams_s* const indexParams)
    {
        const static int index = 86;
        return CallVFunc<void*>(index, this, indexCount, indexParams);
    }

    inline void EndDynamicIndexBuffer(const int indexCount, u16* const indexBuffer)
    {
        const static int index = 88;
        return CallVFunc<void>(index, this, indexCount, indexBuffer);
    }

    inline void DrawTriangleListIndexed(const DirectDrawVertexParams_s* const vertexParams, const DirectDrawIndexParams_s* const indexParams, const int unknown)
    {
        const static int index = 89;
        return CallVFunc<void>(index, this, vertexParams, indexParams, unknown);
    }

    inline void DrawTriangleList(const DirectDrawVertexParams_s* const drawParams, ID3D11SamplerState* const samplerState, const int unknown)
    {
        const static int index = 98;
        return CallVFunc<void>(index, this, drawParams, samplerState, unknown);
    }

    inline void DrawTriangleStrip(const DirectDrawVertexParams_s* const drawParams, ID3D11SamplerState* const samplerState, const int unknown)
    {
        const static int index = 99;
        return CallVFunc<void>(index, this, drawParams, samplerState, unknown);
    }

    inline void DrawLineList(const DirectDrawVertexParams_s* const drawParams, ID3D11SamplerState* const samplerState, const int unknown)
    {
        const static int index = 102;
        return CallVFunc<void>(index, this, drawParams, samplerState, unknown);
    }

    inline void DrawPointList(const DirectDrawVertexParams_s* const drawParams, ID3D11SamplerState* const samplerState, const int unknown)
    {
        const static int index = 103;
        return CallVFunc<void>(index, this, drawParams, samplerState, unknown);
    }
};

#endif // MATRENDERCONTEXT_H
