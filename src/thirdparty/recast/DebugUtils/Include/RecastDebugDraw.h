//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#ifndef RECAST_DEBUGDRAW_H
#define RECAST_DEBUGDRAW_H

enum DrawRecastMeshFlags
{
	DU_DRAW_RECASTMESH_INPUT_MESH           = 1<<0,
	DU_DRAW_RECASTMESH_NAVMESH              = 1<<1,
	DU_DRAW_RECASTMESH_VOXELS               = 1<<2,
	DU_DRAW_RECASTMESH_VOXELS_WALKABLE      = 1<<3,
	DU_DRAW_RECASTMESH_COMPACT              = 1<<4,
	DU_DRAW_RECASTMESH_COMPACT_DISTANCE     = 1<<5,
	DU_DRAW_RECASTMESH_COMPACT_REGIONS      = 1<<6,
	DU_DRAW_RECASTMESH_REGION_CONNECTIONS   = 1<<7,
	DU_DRAW_RECASTMESH_RAW_CONTOURS         = 1<<8,
	DU_DRAW_RECASTMESH_CONTOURS             = 1<<9,
	DU_DRAW_RECASTMESH_POLYMESH             = 1<<10,
	DU_DRAW_RECASTMESH_POLYMESH_DETAIL      = 1<<11,
	DU_DRAW_RECASTMESH_SHAPE_VOLUMES        = 1<<12,
};

void duDebugDrawTriMesh(struct duDebugDraw* dd, const rdVec3D* verts, int nverts, const int* tris, const rdVec3D* normals, int ntris,
						const unsigned char* flags, const float texScale, const rdVec3D* offset);
void duDebugDrawTriMeshSlope(struct duDebugDraw* dd, const rdVec3D* verts, int nverts, const int* tris, const rdVec3D* normals, int ntris,
						const float walkableSlopeAngle, const float texScale, const rdVec3D* offset);

void duDebugDrawHeightfieldSolid(struct duDebugDraw* dd, const struct rcHeightfield& hf, const rdVec3D* offset);
void duDebugDrawHeightfieldWalkable(struct duDebugDraw* dd, const struct rcHeightfield& hf, const rdVec3D* offset);

void duDebugDrawCompactHeightfieldSolid(struct duDebugDraw* dd, const struct rcCompactHeightfield& chf, const rdVec3D* offset);
void duDebugDrawCompactHeightfieldRegions(struct duDebugDraw* dd, const struct rcCompactHeightfield& chf, const rdVec3D* offset);
void duDebugDrawCompactHeightfieldDistance(struct duDebugDraw* dd, const struct rcCompactHeightfield& chf, const rdVec3D* offset);

void duDebugDrawHeightfieldLayer(duDebugDraw* dd, const struct rcHeightfieldLayer& layer, const int idx, const rdVec3D* offset);
void duDebugDrawHeightfieldLayers(duDebugDraw* dd, const struct rcHeightfieldLayerSet& lset, const rdVec3D* offset);

void duDebugDrawRegionConnections(struct duDebugDraw* dd, const struct rcContourSet& cset, const rdVec3D* offset, const float alpha = 1.0f);
void duDebugDrawRawContours(struct duDebugDraw* dd, const struct rcContourSet& cset, const rdVec3D* offset, const float alpha = 1.0f);
void duDebugDrawContours(struct duDebugDraw* dd, const struct rcContourSet& cset, const rdVec3D* offset, const float alpha = 1.0f);
void duDebugDrawPolyMesh(struct duDebugDraw* dd, const struct rcPolyMesh& mesh, const rdVec3D* offset);
void duDebugDrawPolyMeshDetail(struct duDebugDraw* dd, const struct rcPolyMeshDetail& dmesh, const rdVec3D* offset);

#endif // RECAST_DEBUGDRAW_H
