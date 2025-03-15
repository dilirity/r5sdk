//===== Copyright © 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: A set of utilities to render standard shapes
//
//===========================================================================//
//
///////////////////////////////////////////////////////////////////////////////

#include "mathlib/color.h"
#include "mathlib/vector.h"
#include "mathlib/vector2d.h"
#include "mathlib/vector4d.h"
#include "mathlib/mathlib.h"
#include "tier2/renderutils.h"
#include "engine/debugoverlay.h" // TODO[ AMOS ]: must be a public interface!
#include "rtech/pak/pakstate.h"
#include "imaterial.h"
#include "materialsystem/cmaterialsystem.h"
#include "materialsystem/cmatrendercontext.h"
#include "materialsystem/cmatqueuedrendercontext.h"
#include "materialsystem/meshbuilder.h"

//-----------------------------------------------------------------------------
// Purpose: standard materials
//-----------------------------------------------------------------------------
static IMaterial* s_transIgnoreZWire;
static IMaterial* s_transNormalZWire;
static IMaterial* s_transIgnoreZFront;
static IMaterial* s_transNormalZFront;
static IMaterial* s_transIgnoreZBoth;
static IMaterial* s_transNormalZBoth;

static bool s_standardMaterialsInitialized = false;

//-----------------------------------------------------------------------------
// Purpose: initializes the standard materials; these must always be available
//-----------------------------------------------------------------------------
static void InitializeStandardMaterials()
{
    // Load engine materials first before proceeding with the SDK.
    // The engine's impl handles LOCAL_THREAD_LOCK internally.
    v_InitializeStandardMaterials();

    LOCAL_THREAD_LOCK();

    if (s_standardMaterialsInitialized)
        return;

    s_standardMaterialsInitialized = true;
    Assert(g_pakLoadApi);

    s_transIgnoreZWire = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_ignorez_wire_rgdu.rpak");
    s_transNormalZWire = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_normalz_wire_rgdu.rpak");
    s_transIgnoreZFront = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_ignorez_front_rgdu.rpak");
    s_transNormalZFront = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_normalz_front_rgdu.rpak");
    s_transIgnoreZBoth = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_ignorez_both_rgdu.rpak");
    s_transNormalZBoth = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_normalz_both_rgdu.rpak");

    // Make sure all standard materials have loaded, if this fails, then most
    // likely the startup.rpak is corrupt or the materials have been renamed.
    Assert(s_transIgnoreZWire);
    Assert(s_transNormalZWire);
    Assert(s_transIgnoreZFront);
    Assert(s_transNormalZFront);
    Assert(s_transIgnoreZBoth);
    Assert(s_transNormalZBoth);
}

//-----------------------------------------------------------------------------
// purpose: box indices
//-----------------------------------------------------------------------------
static const int s_boxFaceIndices[6][4] =
{
    { 0, 4, 6, 2 }, // -x
    { 5, 1, 3, 7 }, // +x
    { 0, 1, 5, 4 }, // -y
    { 2, 6, 7, 3 }, // +y
    { 0, 2, 3, 1 },	// -z
    { 4, 5, 7, 6 }	// +z
};
static const int s_boxFaceIndicesInsideOut[6][4] =
{
    { 0, 2, 6, 4 }, // -x
    { 5, 7, 3, 1 }, // +x
    { 0, 4, 5, 1 }, // -y
    { 2, 3, 7, 6 }, // +y
    { 0, 1, 3, 2 },	// -z
    { 4, 6, 7, 5 }	// +z
};

//-----------------------------------------------------------------------------
// Purpose: generates the box vertices from the rotation matrix
//-----------------------------------------------------------------------------
static void GenerateBoxVertices(const matrix3x4_t& fRotateMatrix, const Vector3D& vMins, const Vector3D& vMaxs, Vector3D pVerts[8])
{
    Vector3D vecPos;
    for (int i = 0; i < 8; ++i)
    {
        vecPos.x = (i & 0x1) ? vMaxs.x : vMins.x;
        vecPos.y = (i & 0x2) ? vMaxs.y : vMins.y;
        vecPos.z = (i & 0x4) ? vMaxs.z : vMins.z;

        VectorTransform(vecPos, fRotateMatrix, pVerts[i]);
    }
}

struct RenderBoxQueue_s
{
    void (*function)(const matrix3x4_t& fRotateMatrix, const Vector3D& vMins, const Vector3D& vMaxs, Color c, IMaterial* pMaterial, bool bInsideOut);
    matrix3x4_t fRotateMatrix;
    Vector3D vMins;
    Vector3D vMaxs;
    Color color;
    IMaterial* material;
    bool bInsideOut;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance box render queue
//-----------------------------------------------------------------------------
static void RenderBoxQueueFunctor(CallQueue_s* const queue)
{
    RenderBoxQueue_s* const item = (RenderBoxQueue_s*)queue->GetCurrentCallItem();
    item->function(item->fRotateMatrix, item->vMins, item->vMaxs, item->color, item->material, item->bInsideOut);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderBoxQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: renders a solid box
//-----------------------------------------------------------------------------
static void RenderBoxInternal(const matrix3x4_t& fRotateMatrix, const Vector3D& vMins, const Vector3D& vMaxs, Color c, IMaterial* pMaterial, bool bInsideOut)
{
    InitializeStandardMaterials();

    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderBoxQueueFunctor, sizeof(RenderBoxQueue_s), 7);
        RenderBoxQueue_s* const item = (RenderBoxQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderBoxInternal;
        item->fRotateMatrix = fRotateMatrix;
        item->vMins = vMins;
        item->vMaxs = vMaxs;
        item->color = c;
        item->material = pMaterial;
        item->bInsideOut = bInsideOut;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderBoxQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, 36))
    {
        ctx->Bind(pMaterial);

        Vector3D p[8];
        GenerateBoxVertices(fRotateMatrix, vMins, vMaxs, p);

        // Draw the box
        for (int i = 0; i < 6; i++)
        {
            const int* const ppFaceIndices = bInsideOut ? s_boxFaceIndicesInsideOut[i] : s_boxFaceIndices[i];
            for (int j = 1; j < 3; ++j)
            {
                const int i0 = ppFaceIndices[0];
                const int i1 = ppFaceIndices[j];
                const int i2 = ppFaceIndices[j + 1];

                vertexBuilder.AppendVertex(p[i0], c);
                vertexBuilder.AppendVertex(p[i2], c);
                vertexBuilder.AppendVertex(p[i1], c);
            }
        }

        vertexBuilder.End(ctx);
        ctx->DrawTriangleList(vertexBuilder.GetParams(), nullptr, 0);
    }
}

//-----------------------------------------------------------------------------
// Purpose: appends axes to the provided mesh
//-----------------------------------------------------------------------------
static void AppendAxes(const Vector3D& origin, Vector3D* const pts, const int idx, const Color c, CMeshVertexBuilder& vertexBuilder)
{
    Vector3D start, temp;
    VectorAdd(pts[idx], origin, start);

    vertexBuilder.AppendVertex(start, c);

    int endidx = (idx & 0x1) ? idx - 1 : idx + 1;
    VectorAdd(pts[endidx], origin, temp);

    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(start, c);

    endidx = (idx & 0x2) ? idx - 2 : idx + 2;
    VectorAdd(pts[endidx], origin, temp);
    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(start, c);

    endidx = (idx & 0x4) ? idx - 4 : idx + 4;
    VectorAdd(pts[endidx], origin, temp);

    vertexBuilder.AppendVertex(temp, c);
}

//-----------------------------------------------------------------------------
// Purpose: appends extrusion faces to the provided mesh
//-----------------------------------------------------------------------------
static void AppendExtrusionFace(const Vector3D& start, const Vector3D& end,
    Vector3D* const pts, const int idx1, const int idx2, const Color c, CMeshVertexBuilder& vertexBuilder)
{
    Vector3D temp;
    VectorAdd(pts[idx1], start, temp);
    vertexBuilder.AppendVertex(temp, c);

    VectorAdd(pts[idx2], start, temp);

    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(temp, c);

    VectorAdd(pts[idx2], end, temp);

    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(temp, c);

    VectorAdd(pts[idx1], end, temp);

    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(temp, c);

    VectorAdd(pts[idx1], start, temp);
    vertexBuilder.AppendVertex(temp, c);
}

struct RenderSweptBoxQueue_s
{
    void (*function)(const Vector3D& vStart, const Vector3D& vEnd, const QAngle& angles,
        const Vector3D& vMins, const Vector3D& vMaxs, const Color c, IMaterial* const pMaterial);
    Vector3D vStart;
    Vector3D vEnd;
    QAngle angles;
    Vector3D vMins;
    Vector3D vMaxs;
    Color color;
    IMaterial* pMaterial;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance swept box render queue
//-----------------------------------------------------------------------------
static void RenderSweptBoxQueueFunctor(CallQueue_s* const queue)
{
    RenderSweptBoxQueue_s* const item = (RenderSweptBoxQueue_s*)queue->GetCurrentCallItem();
    item->function(item->vStart, item->vEnd, item->angles, item->vMins, item->vMaxs, item->color, item->pMaterial);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderSweptBoxQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: renders an extruded box
//-----------------------------------------------------------------------------
static void RenderWireframeSweptBoxInternal(const Vector3D& vStart, const Vector3D& vEnd,
    const QAngle& angles, const Vector3D& vMins, const Vector3D& vMaxs, const Color c, IMaterial* const pMaterial)
{
    InitializeStandardMaterials();

    // Queue it off if this is called outside the render thread.
    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderSweptBoxQueueFunctor, sizeof(RenderSweptBoxQueue_s), 7);
        RenderSweptBoxQueue_s* const item = (RenderSweptBoxQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderWireframeSweptBoxInternal;
        item->vStart = vStart;
        item->vEnd = vEnd;
        item->angles = angles;
        item->vMins = vMins;
        item->vMaxs = vMaxs;
        item->color = c;
        item->pMaterial = pMaterial;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderSweptBoxQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, 60))
    {
        ctx->Bind(pMaterial);

        // Build a rotation matrix from angles
        matrix3x4_t fRotateMatrix;
        AngleMatrix(angles, fRotateMatrix);

        Vector3D vDelta;
        VectorSubtract(vEnd, vStart, vDelta);

        // Compute the box points, rotated but without the origin added
        Vector3D temp;
        Vector3D pts[8];
        float dot[8];
        int minidx = 0;
        for (int i = 0; i < 8; ++i)
        {
            temp.x = (i & 0x1) ? vMaxs[0] : vMins[0];
            temp.y = (i & 0x2) ? vMaxs[1] : vMins[1];
            temp.z = (i & 0x4) ? vMaxs[2] : vMins[2];

            // Rotate the corner point
            VectorRotate(temp, fRotateMatrix, pts[i]);

            // Find the dot product with dir
            dot[i] = DotProduct(pts[i], vDelta);
            if (dot[i] < dot[minidx])
            {
                minidx = i;
            }
        }

        // Choose opposite corner
        const int maxidx = minidx ^ 0x7;

        // Draw the start + end axes...
        AppendAxes(vStart, pts, minidx, c, vertexBuilder);
        AppendAxes(vEnd, pts, maxidx, c, vertexBuilder);

        // Draw the extrusion faces
        for (int j = 0; j < 3; ++j)
        {
            const int dirflag1 = (1 << ((j + 1) % 3));
            const int dirflag2 = (1 << ((j + 2) % 3));

            const int idx1 = (minidx & dirflag1) ? minidx - dirflag1 : minidx + dirflag1;
            const int idx2 = (minidx & dirflag2) ? minidx - dirflag2 : minidx + dirflag2;
            const int idx3 = (minidx & dirflag2) ? idx1 - dirflag2 : idx1 + dirflag2;

            AppendExtrusionFace(vStart, vEnd, pts, idx1, idx3, c, vertexBuilder);
            AppendExtrusionFace(vStart, vEnd, pts, idx2, idx3, c, vertexBuilder);
        }

        vertexBuilder.End(ctx);
        ctx->DrawLineList(vertexBuilder.GetParams(), nullptr, 0);
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

struct RenderTriangleQueue_s
{
    void (*function)(const Vector3D& p1, const Vector3D& p2, const Vector3D& p3, const Color c, IMaterial* const pMaterial);
    Vector3D p1;
    Vector3D p2;
    Vector3D p3;
    Color color;
    IMaterial* material;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance triangle render queue
//-----------------------------------------------------------------------------
static void RenderTriangleQueueFunctor(CallQueue_s* const queue)
{
    RenderTriangleQueue_s* const item = (RenderTriangleQueue_s*)queue->GetCurrentCallItem();
    item->function(item->p1, item->p2, item->p3, item->color, item->material);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderTriangleQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: process and advance triangle render queue
//-----------------------------------------------------------------------------
static void RenderTriangleInternal(const Vector3D& p1, const Vector3D& p2, const Vector3D& p3, const Color c, IMaterial* const pMaterial)
{
    InitializeStandardMaterials();

    // Queue it off if this is called outside the render thread.
    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderTriangleQueueFunctor, sizeof(RenderTriangleQueue_s), 7);
        RenderTriangleQueue_s* const item = (RenderTriangleQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderTriangleInternal;
        item->p1 = p1;
        item->p2 = p2;
        item->p3 = p3;
        item->color = c;
        item->material = pMaterial;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderTriangleQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, 3))
    {
        ctx->Bind(pMaterial);

        vertexBuilder.AppendVertex(p3, c);
        vertexBuilder.AppendVertex(p2, c);
        vertexBuilder.AppendVertex(p1, c);

        vertexBuilder.End(ctx);
        ctx->DrawTriangleList(vertexBuilder.GetParams(), nullptr, 0);
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

//-----------------------------------------------------------------------------
// Purpose: public proxy for RenderBoxInternal
//-----------------------------------------------------------------------------
void RenderBox(const matrix3x4_t& vTransforms, const Vector3D& vMins, const Vector3D& vMaxs, Color color, bool bZBuffer)
{
    IMaterial* const mat = bZBuffer ? s_transNormalZFront : s_transIgnoreZFront;
    RenderBoxInternal(vTransforms, vMins, vMaxs, color, mat, false);
}

//-----------------------------------------------------------------------------
// Purpose: public proxy for RenderWireframeSweptBoxInternal
//-----------------------------------------------------------------------------
void RenderWireframeSweptBox(const Vector3D& vStart, const Vector3D& vEnd, const QAngle& angles,
    const Vector3D& vMins, const Vector3D& vMaxs, const Color c, const bool bZBuffer)
{
    IMaterial* const pMaterial = bZBuffer ? s_transNormalZWire : s_transIgnoreZWire;
    RenderWireframeSweptBoxInternal(vStart, vEnd, angles, vMins, vMaxs, c, pMaterial);
}

//-----------------------------------------------------------------------------
// Purpose: public proxy for RenderTriangleInternal
//-----------------------------------------------------------------------------
void RenderTriangle(const Vector3D& p1, const Vector3D& p2, const Vector3D& p3, const Color c, const bool bZBuffer)
{
    IMaterial* pMaterial;
    if (c.a() < 1)
        pMaterial = bZBuffer ? s_transNormalZWire : s_transIgnoreZWire;
    else
        pMaterial = bZBuffer ? s_transNormalZBoth : s_transIgnoreZBoth;

    RenderTriangleInternal(p1, p2, p3, c, pMaterial);
}

///////////////////////////////////////////////////////////////////////////////
// Below a set of helper functions for shapes utilizing the render code above
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// Purpose: render angled box:
// +y              _+z
// ^               /|
// |              /
// |  +----------+
// | /|         /|
//  / |        / |
// +----------+  |
// |  +-------|--+
// | /        | /
// |/         |/
// +----------+ --> +x
//-----------------------------------------------------------------------------
void DebugDrawBox(const Vector3D& vOrigin, const QAngle& vAngles, const Vector3D& vMins, const Vector3D& vMaxs, Color color, bool bZBuffer)
{
    Vector3D vPoints[8];
    PointsFromAngledBox(vAngles, vMins, vMaxs, &*vPoints);

    v_RenderLine(vOrigin + vPoints[0], vOrigin + vPoints[1], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[1], vOrigin + vPoints[2], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[2], vOrigin + vPoints[3], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[3], vOrigin + vPoints[0], color, bZBuffer);

    v_RenderLine(vOrigin + vPoints[4], vOrigin + vPoints[5], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[5], vOrigin + vPoints[6], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[6], vOrigin + vPoints[7], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[7], vOrigin + vPoints[4], color, bZBuffer);

    v_RenderLine(vOrigin + vPoints[0], vOrigin + vPoints[4], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[1], vOrigin + vPoints[5], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[2], vOrigin + vPoints[6], color, bZBuffer);
    v_RenderLine(vOrigin + vPoints[3], vOrigin + vPoints[7], color, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render cylinder:
// +y           _+z
// ^            /|
// |           /
// |.-'"|"'-. /
// (----|----)
// |'-._|_.-'|
// |    |    |
// |    |    |
// | <--+--> |--> +r
// |    |    |
// |    |    |
//  "-._|_.-" --> +x
//-----------------------------------------------------------------------------
void DebugDrawCylinder(const Vector3D& vOrigin, const QAngle& vAngles, float flRadius, float flHeight, Color color, int nSides, bool bZBuffer)
{
    float flDegrees = 360.f / float(nSides);
    QAngle vComposed;
    Vector3D vForward;
    CUtlVector<Vector3D> vecPoints(0, nSides);

    AngleVectors(vAngles, &vForward);

    for (int i = 0; i < nSides; i++)
    {
        Vector3D right;

        AngleCompose(vAngles, { 0.f, 0.f, flDegrees * i }, vComposed);
        AngleVectors(vComposed, nullptr, &right, nullptr);
        vecPoints.AddToTail(vOrigin + (right * flRadius));
    }

    for (int i = 0; i < nSides; i++)
    {
        Vector3D vStart = vecPoints[i];
        Vector3D vEnd = i == 0 ? vecPoints[nSides - 1] : vecPoints[i - 1];

        v_RenderLine(vStart, vEnd, color, bZBuffer);
        v_RenderLine(vStart + (vForward * flHeight), vEnd + (vForward * flHeight), color, bZBuffer);
        v_RenderLine(vStart, vStart + (vForward * flHeight), color, bZBuffer);
    }
}

//-----------------------------------------------------------------------------
// Purpose: render capsule:
// +y           _+z
// ^            /|
// |           /
// |.-'"|"'-. /
// |----|----|
// |    |    |
// |    |    |
// | <--+--> |--> +r
// |    |    |
// |    |    |
// |----|----|
//  "-..|..-" --> +x
//-----------------------------------------------------------------------------
void DebugDrawCapsule(const Vector3D& vStart, const QAngle& vAngles, const Vector3D& vRadius, float flHeight, Color color, bool bZBuffer)
{
    Vector3D vForward, vUp;
    QAngle vHemi, vComposed;

    AngleVectors(vAngles, nullptr, nullptr, &vUp);

    for (int i = 0; i < 4; i++)
    {
        AngleCompose(vAngles, { 0.f, 90.f * i, 0.f }, vComposed);
        AngleVectors(vComposed, &vForward);
        v_RenderLine(vStart + (vForward * vRadius), vStart + (vForward * vRadius) + (vUp * flHeight), color, bZBuffer);
    }

    AngleCompose(vAngles, { 180.f, 180.f, 0.f }, vHemi);

    DebugDrawHemiSphere(vStart + (vUp * flHeight), vAngles, vRadius, color, 8, bZBuffer);
    DebugDrawHemiSphere(vStart, vHemi, vRadius, color, 8, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render sphere:
// +y                _+z
// ^                 /|
// |                /
// |   .--"|"--.   /
//  .'     |     '.
// /       |       \
// | <----( )---->-|--> +r
// \       |       /
//  '.     |     .'
//    "-.._|_..-"   --> +x
//-----------------------------------------------------------------------------
void DebugDrawSphere(const Vector3D& vOrigin, float flRadius, Color color, int nSegments, bool bZBuffer)
{
    DebugDrawCircle(vOrigin, { 90.f, 0.f, 0.f }, flRadius, color, nSegments, bZBuffer);
    DebugDrawCircle(vOrigin, { 0.f, 90.f, 0.f }, flRadius, color, nSegments, bZBuffer);
    DebugDrawCircle(vOrigin, { 0.f, 0.f, 90.f }, flRadius, color, nSegments, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render hemisphere:
// +y                _+z
// ^                 /|
// |                /
// |   .--"|"--.   /
//  .'     |     '.
// /       |       \ /--> +r
// | <----( )---->-|/ --> +x
//-----------------------------------------------------------------------------
void DebugDrawHemiSphere(const Vector3D& vOrigin, const QAngle& vAngles, const Vector3D& vRadius, Color color, int nSegments, bool bZBuffer)
{
    const float flDegrees = 360.0f / float(nSegments * 2);
    bool bFirstLoop = true;

    Vector3D vStart[4], vEnd[4], vForward[4];
    QAngle vComposed[4];

    for (int i = 0; i < (nSegments + 1); i++)
    {
        const float angleOffset = flDegrees * i;

        AngleCompose(vAngles, { angleOffset - 180, 0, 0 }, vComposed[0]);
        AngleCompose(vAngles, { 0, angleOffset - 90, 0 }, vComposed[1]);
        AngleCompose(vAngles, { angleOffset + 180, 90, 0 }, vComposed[2]);
        AngleCompose(vAngles, { 0, angleOffset + 90, 0 }, vComposed[3]);

        AngleVectors(vComposed[0], &vForward[0]);
        AngleVectors(vComposed[1], &vForward[1]);
        AngleVectors(vComposed[2], &vForward[2]);
        AngleVectors(vComposed[3], &vForward[3]);

        vEnd[0] = vOrigin + (vForward[0] * vRadius);
        vEnd[1] = vOrigin + (vForward[1] * vRadius);
        vEnd[2] = vOrigin + (vForward[2] * vRadius);
        vEnd[3] = vOrigin + (vForward[3] * vRadius);

        if (!bFirstLoop)
        {
            v_RenderLine(vStart[0], vEnd[0], color, bZBuffer);
            v_RenderLine(vStart[1], vEnd[1], color, bZBuffer);
            v_RenderLine(vStart[2], vEnd[2], color, bZBuffer);
            v_RenderLine(vStart[3], vEnd[3], color, bZBuffer);
        }

        bFirstLoop = false;

        vStart[0] = vEnd[0];
        vStart[1] = vEnd[1];
        vStart[2] = vEnd[2];
        vStart[3] = vEnd[3];
    }
}

//-----------------------------------------------------------------------------
// Purpose: render circle:
// +y                _+z
// ^                 /|
// |                /
// |   .--"""--.   /
//  .'           '.
// /               \
// | <----( )---->-|--> +r
// \               /
//  '.           .'
//    "-..___..-"   --> +x
//-----------------------------------------------------------------------------
void DebugDrawCircle(const Vector3D& vOrigin, const QAngle& vAngles, float flRadius, Color color, int nSegments, bool bZBuffer)
{
    bool bFirstLoop = true;
    float flDegrees = 360.f / float(nSegments);

    Vector3D vStart, vEnd, vFirstend, vForward;
    QAngle vComposed;

    for (int i = 0; i < nSegments; i++)
    {
        AngleCompose(vAngles, { 0.f, flDegrees * i, 0.f }, vComposed);
        AngleVectors(vComposed, &vForward);
        vEnd = vOrigin + (vForward * flRadius);

        if (bFirstLoop)
            vFirstend = vEnd;

        if (!bFirstLoop)
            v_RenderLine(vStart, vEnd, color, bZBuffer);

        vStart = vEnd;

        bFirstLoop = false;
    }

    v_RenderLine(vEnd, vFirstend, color, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render square:
// +y              _+z
// |               /|
// .--------------.
// |              |
// |              |
// |              |
// |              |
// |              |
// |              |
// '--------------' --> +x
//-----------------------------------------------------------------------------
void DebugDrawSquare(const Vector3D& vOrigin, const QAngle& vAngles, float flSquareSize, Color color, bool bZBuffer)
{
    DebugDrawCircle(vOrigin, vAngles, flSquareSize, color, 4, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render triangle:
// +y              _+z
// |               /|
// |      /\      /
// |     /  \    /
// |    /    \  /
// |   /      \
// |  /        \
// | /          \
//  /            \
// '--------------' --> +x
//-----------------------------------------------------------------------------
void DebugDrawTriangle(const Vector3D& vOrigin, const QAngle& vAngles, float flTriangleSize, Color color, bool bZBuffer)
{
    DebugDrawCircle(vOrigin, vAngles, flTriangleSize, color, 3, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render mark:
// +y     _+z
// |      /|
// |     /
//   \  /--> +r
// ___\/___
//    /\
//   /  \
//  /    --> +x
//-----------------------------------------------------------------------------
void DebugDrawMark(const Vector3D& vOrigin, float flRadius, const vector<int>& vColor, bool bZBuffer)
{
    v_RenderLine((vOrigin - Vector3D{ flRadius, 0.f, 0.f }), (vOrigin + Vector3D{ flRadius, 0.f, 0.f }), Color(vColor[0], vColor[1], vColor[2], vColor[3]), bZBuffer);
    v_RenderLine((vOrigin - Vector3D{ 0.f, flRadius, 0.f }), (vOrigin + Vector3D{ 0.f, flRadius, 0.f }), Color(vColor[0], vColor[1], vColor[2], vColor[3]), bZBuffer);
    v_RenderLine((vOrigin - Vector3D{ 0.f, 0.f, flRadius }), (vOrigin + Vector3D{ 0.f, 0.f, flRadius }), Color(vColor[0], vColor[1], vColor[2], vColor[3]), bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render star:
// +y     _+z
// |      /|
// |     /
//   \  /--> +r
// ___\/___
//    /\
//   /  \
//       --> +x
//-----------------------------------------------------------------------------
void DrawStar(const Vector3D& vOrigin, float flRadius, bool bZBuffer)
{
    Vector3D vForward;
    for (int i = 0; i < 50; i++)
    {
        AngleVectors({ RandomFloat(0.f, 360.f), RandomFloat(0.f, 360.f), RandomFloat(0.f, 360.f) }, &vForward);
        v_RenderLine(vOrigin, vOrigin + vForward * flRadius, Color(RandomInt(0, 255), RandomInt(0, 255), RandomInt(0, 255), 255), bZBuffer);
    }
}

//-----------------------------------------------------------------------------
// Purpose: render arrow:
// +y     _+z
// |      /|
// |  .  /
// | / \
//  /   \
// /_____\ --> r
//    |
//    |
//    |   --> +x
//-----------------------------------------------------------------------------
void DebugDrawArrow(const Vector3D& vOrigin, const Vector3D& vEnd, float flArraySize, Color color, bool bZBuffer)
{
    Vector3D vAngles;

    v_RenderLine(vOrigin, vEnd, color, bZBuffer);
    AngleVectors(Vector3D(vEnd - vOrigin).Normalized().AsQAngle(), &vAngles);
    DebugDrawCircle(vEnd, vAngles.AsQAngle(), flArraySize, color, 3, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render 3d axis:
// +y
// ^
// |   _+z
// |   /|
// |  /
// | /
// |/
// +----------> +x
//-----------------------------------------------------------------------------
void DebugDrawAxis(const Vector3D& vOrigin, const QAngle& vAngles, float flScale, bool bZBuffer)
{
    Vector3D vForward, vRight, vUp;
    AngleVectors(vAngles, &vForward, &vRight, &vUp);

    v_RenderLine(vOrigin, vOrigin + vForward * flScale, Color(0, 255, 0, 255), bZBuffer);
    v_RenderLine(vOrigin, vOrigin + vUp * flScale, Color(255, 0, 0, 255), bZBuffer);
    v_RenderLine(vOrigin, vOrigin + vRight * flScale, Color(0, 0, 255, 255), bZBuffer);
}

void V_RenderUtils::Detour(const bool bAttach) const
{
    DetourSetup(&v_InitializeStandardMaterials, &InitializeStandardMaterials, bAttach);
}
