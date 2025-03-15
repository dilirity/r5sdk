#include "cmatrendercontext.h"
#include "materialsystem/meshbuilder.h"

bool CMeshVertexBuilder::Begin(CMatRenderContext* const ctx, const int vertexCount)
{
#ifdef _DEBUG
    m_vertexCount = 0;
#endif // _DEBUG

    m_vertexParams.vertexFormat.position3d = 1;
    m_vertexParams.vertexFormat.color = 1;

    m_vertexParams.vertexStructSize = sizeof(Vector3D) + sizeof(Color);
    m_vertexParams.vertexBlockIndex = 0;
    m_vertexParams.vertexBufferOffset = 0;
    m_vertexParams.vertexCount = 0;

    m_meshBuffer = (MeshVertex_s*)ctx->GetDynamicMesh(vertexCount, &m_vertexParams, 13);
    return m_meshBuffer != nullptr;
}

void CMeshVertexBuilder::End(CMatRenderContext* const ctx) const
{
#ifdef _DEBUG
    // If this gets fired, you processed more or less
    // vertices than allocated.
    Assert(m_vertexCount == m_vertexParams.vertexCount);
#endif // _DEBUG
    ctx->EndDynamicMesh(m_vertexParams.vertexCount);
}

bool CMeshIndexBuilder::Begin(CMatRenderContext* const ctx, const int indexCount)
{
#ifdef _DEBUG
    m_indexCount = 0;
#endif // _DEBUG

    m_indexBuffer = (u16*)ctx->GetDynamicIndexBuffer(indexCount, &m_indexParams);
    return m_indexBuffer != nullptr;
}

void CMeshIndexBuilder::End(CMatRenderContext* const ctx)
{
#ifdef _DEBUG
    // If this gets fired, you processed more or less
    // indices than allocated.
    Assert(m_indexCount == m_indexParams.indexCount);
#endif // _DEBUG
    ctx->EndDynamicIndexBuffer(m_indexParams.indexCount, m_indexBuffer);
}
