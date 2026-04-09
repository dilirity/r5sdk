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

#ifndef OFFMESHCONNECTIONTOOL_H
#define OFFMESHCONNECTIONTOOL_H

#include "NavEditor/Include/Editor.h"

// Tool to create off-mesh connection for InputGeom

// TODO: eventually this needs to be the type for off-mesh connections in InputGeom!
struct OffMeshConnection
{
	rdVec3D posa;
	rdVec3D posb;
	rdVec3D refPos;
	float rad;
	float refYaw;
	unsigned char dir;
	unsigned char jump;
	unsigned char order;
	unsigned char area;
	unsigned short flags;
};

class OffMeshConnectionTool : public EditorTool
{
	Editor* m_editor;
	rdVec3D m_hitPos;
	rdVec3D m_refOffset;
	float m_lastSelectedAgentRadius;
	float m_radius;
	bool m_hitPosSet;
	bool m_bidir;
	bool m_invertVertexLookupOrder;
	int m_traverseType;
	unsigned int m_oldFlags;

	// Tile-based off-mesh connection editing.
	int m_selectedTileOffMeshTile;
	int m_selectedTileOffMeshIdx;
	int m_copiedTileOffMeshTile;
	int m_copiedTileOffMeshIdx;
	OffMeshConnection m_copyTileOffMeshInstance;

public:
	OffMeshConnectionTool();
	~OffMeshConnectionTool();

	void renderTileOffMeshModifyMenu();
	void applyTileOffMeshChanges();
	static void disconnectTileOffMeshLinks(class dtNavMesh* nav, struct dtMeshTile* tile);
	
	virtual int type() { return TOOL_OFFMESH_CONNECTION; }
	virtual void init(Editor* editor);
	virtual void reset();
	virtual void handleMenu();
	virtual void handleClick(const rdVec3D* s, const rdVec3D* p, const int v, bool shift);
	virtual void handleToggle();
	virtual void handleStep();
	virtual void handleUpdate(const float dt);
	virtual void handleRender();
	virtual void handleRenderOverlay(double* model, double* proj, int* view);
};

#endif // OFFMESHCONNECTIONTOOL_H
