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

#include "Recast/Include/Recast.h"
#include "Detour/Include/DetourNavMesh.h"
#include "DebugUtils/Include/RecastDebugDraw.h"
#include "DebugUtils/Include/DetourDebugDraw.h"
#include "NavEditor/Include/OffMeshConnectionTool.h"
#include "NavEditor/Include/InputGeom.h"
#include "NavEditor/Include/Editor.h"
#include "NavEditor/Include/CameraUtils.h"

#ifdef WIN32
#	define snprintf _snprintf
#endif

OffMeshConnectionTool::OffMeshConnectionTool() :
	m_editor(0),
	m_lastSelectedAgentRadius(0),
	m_radius(0),
	m_hitPosSet(0),
	m_bidir(true),
	m_invertVertexLookupOrder(false),
	m_traverseType(0),
	m_oldFlags(0),
	m_selectedTileOffMeshTile(-1),
	m_selectedTileOffMeshIdx(-1),
	m_copiedTileOffMeshTile(-1),
	m_copiedTileOffMeshIdx(-1)
{
	m_hitPos.init(0.0f,0.0f,0.0f);
	m_refOffset.init(0.0f,0.0f,0.0f);
	memset(&m_copyTileOffMeshInstance, 0, sizeof(OffMeshConnection));
}

OffMeshConnectionTool::~OffMeshConnectionTool()
{
	if (m_editor)
	{
		if (m_oldFlags & DU_DRAW_DETOURMESH_OFFMESHCONS)
		{
			const unsigned int curFlags = m_editor->getNavMeshDrawFlags();
			m_editor->setNavMeshDrawFlags(curFlags | DU_DRAW_DETOURMESH_OFFMESHCONS);
		}
	}
}

void OffMeshConnectionTool::init(Editor* editor)
{
	if (m_editor != editor)
	{
		m_editor = editor;
		m_oldFlags = m_editor->getNavMeshDrawFlags();

		const float agentRadius = m_editor->getAgentRadius();
		m_radius = agentRadius;
		m_lastSelectedAgentRadius = agentRadius;

		m_refOffset.init(0.0f,0.0f, agentRadius);
	}
}

void OffMeshConnectionTool::reset()
{
	const float agentRadius = m_editor->getAgentRadius();
	m_radius = agentRadius;
	m_lastSelectedAgentRadius = agentRadius;
	m_hitPosSet = false;
	m_selectedTileOffMeshTile = -1;
	m_selectedTileOffMeshIdx = -1;
	m_copiedTileOffMeshTile = -1;
	m_copiedTileOffMeshIdx = -1;
}

#define VALUE_ADJUST_WINDOW 200

void OffMeshConnectionTool::disconnectTileOffMeshLinks(dtNavMesh* nav, dtMeshTile* tile)
{
	if (!tile->header) return;

	const dtPolyRef base = nav->getPolyRefBase(tile);

	for (int i = 0; i < tile->header->offMeshConCount; i++)
	{
		dtOffMeshConnection* con = &tile->offMeshCons[i];
		dtPoly* conPoly = &tile->polys[con->poly];
		const dtPolyRef conPolyRef = base | (dtPolyRef)(con->poly);

		// Walk the off-mesh poly's link chain and remove reverse links
		// from ground polys pointing back to this off-mesh poly.
		unsigned int j = conPoly->firstLink;
		while (j != DT_NULL_LINK)
		{
			dtLink& link = tile->links[j];
			const unsigned int nextJ = link.next;

			// Find the target tile and poly, remove their link back to us.
			const unsigned int targetTileIdx = nav->decodePolyIdTile(link.ref);
			const unsigned int targetPolyIdx = nav->decodePolyIdPoly(link.ref);
			dtMeshTile* targetTile = nav->getTile(targetTileIdx);
			dtPoly* targetPoly = &targetTile->polys[targetPolyIdx];

			// Walk target poly's links, remove any pointing to our off-mesh poly.
			unsigned int k = targetPoly->firstLink;
			unsigned int pk = DT_NULL_LINK;
			while (k != DT_NULL_LINK)
			{
				dtLink& targetLink = targetTile->links[k];
				if (targetLink.ref == conPolyRef)
				{
					const unsigned int nk = targetLink.next;
					if (pk == DT_NULL_LINK)
						targetPoly->firstLink = nk;
					else
						targetTile->links[pk].next = nk;
					targetTile->freeLink(k);
					k = nk;
				}
				else
				{
					pk = k;
					k = targetLink.next;
				}
			}

			tile->freeLink(j);
			j = nextJ;
		}

		conPoly->firstLink = DT_NULL_LINK;
		conPoly->flags &= ~DT_POLYFLAGS_JUMP_LINKED;
	}
}

void OffMeshConnectionTool::applyTileOffMeshChanges()
{
	if (!m_editor) return;
	dtNavMesh* nav = m_editor->getNavMesh();
	if (!nav) return;
	if (m_selectedTileOffMeshTile < 0 || m_selectedTileOffMeshIdx < 0) return;

	dtMeshTile* tile = nav->getTile(m_selectedTileOffMeshTile);
	if (!tile->header) return;

	dtOffMeshConnection* con = &tile->offMeshCons[m_selectedTileOffMeshIdx];
	dtPoly* poly = &tile->polys[con->poly];
	const dtTileRef tileRef = nav->getTileRef(tile);

	// Disconnect all off-mesh connections in this tile before reconnecting.
	disconnectTileOffMeshLinks(nav, tile);

	// Update poly center from new endpoint positions.
	rdVadd(&poly->center, &con->posa, &con->posb);
	rdVscale(&poly->center, &poly->center, 0.5f);

	// Reconnect all off-mesh connections in the tile.
	nav->connectOffMeshLinks(tileRef);

	// Read back clamped positions and update the backup for slider ranges.
	m_copyTileOffMeshInstance.posa = con->posa;
	m_copyTileOffMeshInstance.posb = con->posb;
	m_copyTileOffMeshInstance.refPos = con->refPos;
	m_copyTileOffMeshInstance.refYaw = con->refYaw;

	m_editor->invalidateNavMeshCache();
}

void OffMeshConnectionTool::renderTileOffMeshModifyMenu()
{
	dtNavMesh* nav = m_editor->getNavMesh();
	if (!nav) return;
	if (m_selectedTileOffMeshTile < 0 || m_selectedTileOffMeshIdx < 0) return;

	dtMeshTile* tile = nav->getTile(m_selectedTileOffMeshTile);
	if (!tile->header) return;
	if (m_selectedTileOffMeshIdx >= tile->header->offMeshConCount) return;

	dtOffMeshConnection* con = &tile->offMeshCons[m_selectedTileOffMeshIdx];
	dtPoly* poly = &tile->polys[con->poly];

	// Backup original values on first access.
	if (m_copiedTileOffMeshTile != m_selectedTileOffMeshTile ||
		m_copiedTileOffMeshIdx != m_selectedTileOffMeshIdx)
	{
		m_copyTileOffMeshInstance.posa = con->posa;
		m_copyTileOffMeshInstance.posb = con->posb;
		m_copyTileOffMeshInstance.refPos = con->refPos;
		m_copyTileOffMeshInstance.refYaw = con->refYaw;
		m_copyTileOffMeshInstance.rad = con->rad;

		m_copiedTileOffMeshTile = m_selectedTileOffMeshTile;
		m_copiedTileOffMeshIdx = m_selectedTileOffMeshIdx;
	}

	ImGui::Separator();
	ImGui::Text("Modify Tile Off-Mesh Connection");

	const bool linked = (poly->flags & DT_POLYFLAGS_JUMP_LINKED) != 0;
	ImGui::Text("Status: %s", linked ? "Linked" : "Unlinked");

	ImGui::PushItemWidth(60);

	ImGui::SliderFloat("##TileOffMeshStartX", &con->posa.x, m_copyTileOffMeshInstance.posa.x - VALUE_ADJUST_WINDOW, m_copyTileOffMeshInstance.posa.x + VALUE_ADJUST_WINDOW);
	ImGui::SameLine();
	ImGui::SliderFloat("##TileOffMeshStartY", &con->posa.y, m_copyTileOffMeshInstance.posa.y - VALUE_ADJUST_WINDOW, m_copyTileOffMeshInstance.posa.y + VALUE_ADJUST_WINDOW);
	ImGui::SameLine();
	ImGui::SliderFloat("##TileOffMeshStartZ", &con->posa.z, m_copyTileOffMeshInstance.posa.z - VALUE_ADJUST_WINDOW, m_copyTileOffMeshInstance.posa.z + VALUE_ADJUST_WINDOW);
	ImGui::SameLine();
	ImGui::Text("Start");

	ImGui::SliderFloat("##TileOffMeshEndX", &con->posb.x, m_copyTileOffMeshInstance.posb.x - VALUE_ADJUST_WINDOW, m_copyTileOffMeshInstance.posb.x + VALUE_ADJUST_WINDOW);
	ImGui::SameLine();
	ImGui::SliderFloat("##TileOffMeshEndY", &con->posb.y, m_copyTileOffMeshInstance.posb.y - VALUE_ADJUST_WINDOW, m_copyTileOffMeshInstance.posb.y + VALUE_ADJUST_WINDOW);
	ImGui::SameLine();
	ImGui::SliderFloat("##TileOffMeshEndZ", &con->posb.z, m_copyTileOffMeshInstance.posb.z - VALUE_ADJUST_WINDOW, m_copyTileOffMeshInstance.posb.z + VALUE_ADJUST_WINDOW);
	ImGui::SameLine();
	ImGui::Text("End");

	ImGui::PopItemWidth();

	if (ImGui::Button("Apply##TileOffMesh"))
	{
		applyTileOffMeshChanges();
	}

	if (ImGui::Button("Reset##TileOffMesh"))
	{
		con->posa = m_copyTileOffMeshInstance.posa;
		con->posb = m_copyTileOffMeshInstance.posb;
		con->refPos = m_copyTileOffMeshInstance.refPos;
		con->refYaw = m_copyTileOffMeshInstance.refYaw;
	}

	if (ImGui::Button("Recalculate Reference##TileOffMesh"))
	{
		con->refYaw = dtCalcOffMeshRefYaw(&con->posa, &con->posb);
		const rdVec3D refOffset(0.0f, 0.0f, con->rad);
		dtCalcOffMeshRefPos(&con->posa, con->refYaw, &refOffset, &con->refPos);
	}

	if (ImGui::Button("Deselect##TileOffMesh"))
	{
		m_selectedTileOffMeshTile = -1;
		m_selectedTileOffMeshIdx = -1;
	}
}

void OffMeshConnectionTool::handleMenu()
{
	ImGui::Text("Create Off-Mesh Connection");

	// On newer navmesh sets, off-mesh links are always bidirectional.
#if DT_NAVMESH_SET_VERSION < 7
	ImGui::Checkbox("Bidirectional##OffMeshConnectionCreate", &m_bidir);
#endif
	ImGui::Checkbox("Invert Lookup Order##OffMeshConnectionCreate", &m_invertVertexLookupOrder);

	ImGui::PushItemWidth(140);
	ImGui::SliderInt("Jump##OffMeshConnectionCreate", &m_traverseType, 0, DT_MAX_TRAVERSE_TYPES-1, "%d", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Radius##OffMeshConnectionCreate", &m_radius, 0, 512);
	ImGui::PopItemWidth();

	renderTileOffMeshModifyMenu();
}

void OffMeshConnectionTool::handleClick(const rdVec3D* /*s*/, const rdVec3D* p, const int /*v*/, bool shift)
{
	if (!m_editor) return;

	if (shift)
	{
		// Select nearest tile-based off-mesh connection endpoint.
		float nearestDist = FLT_MAX;
		int nearestIdx = -1;
		int nearestTile = -1;
		dtNavMesh* nav = m_editor->getNavMesh();
		if (nav)
		{
			for (int i = 0; i < nav->getMaxTiles(); i++)
			{
				const dtMeshTile* tile = nav->getTile(i);
				if (!tile->header) continue;

				for (int j = 0; j < tile->header->offMeshConCount; j++)
				{
					const dtOffMeshConnection* con = &tile->offMeshCons[j];
					float da = rdVdist2DSqr(p, &con->posa);
					float db = rdVdist2DSqr(p, &con->posb);
					float d = rdMin(da, db);
					if (d < nearestDist)
					{
						nearestDist = d;
						nearestIdx = j;
						nearestTile = i;
					}
				}
			}
		}

		if (nearestIdx != -1 && rdMathSqrtf(nearestDist) < m_radius)
		{
			m_selectedTileOffMeshTile = nearestTile;
			m_selectedTileOffMeshIdx = nearestIdx;
		}
	}
	else
	{
		// Create
		InputGeom* geom = m_editor->getInputGeom();
		if (!geom) return;

		if (!m_hitPosSet)
		{
			m_hitPos = *p;
			m_hitPosSet = true;
		}
		else
		{
			const unsigned char area = DT_POLYAREA_JUMP;
			const unsigned short flags = DT_POLYFLAGS_WALK
#if DT_NAVMESH_SET_VERSION >= 7
				| DT_POLYFLAGS_JUMP;
#else
				;
#endif;
			geom->addOffMeshConnection(&m_hitPos, p, m_radius, m_bidir ? 1 : 0,
				(unsigned char)m_traverseType, m_invertVertexLookupOrder ? 1 : 0, area, flags);
			m_hitPosSet = false;
		}
	}
}

void OffMeshConnectionTool::handleToggle()
{
}

void OffMeshConnectionTool::handleStep()
{
}

void OffMeshConnectionTool::handleUpdate(const float /*dt*/)
{
	const float agentRadius = m_editor->getAgentRadius();

	if (m_lastSelectedAgentRadius < agentRadius || m_lastSelectedAgentRadius > agentRadius)
	{
		m_lastSelectedAgentRadius = agentRadius;
		m_radius = agentRadius;
	}
}

void OffMeshConnectionTool::handleRender()
{
	duDebugDraw& dd = m_editor->getDebugDraw();
	const float s = m_editor->getAgentRadius();
	
	if (m_hitPosSet)
		duDebugDrawCross(&dd, m_hitPos[0],m_hitPos[1],m_hitPos[2]+0.1f, s, duRGBA(0,0,0,128), 2.0f, nullptr);

	// Draw InputGeom off-mesh connections (newly created ones).
	InputGeom* geom = m_editor->getInputGeom();
	if (geom)
		geom->drawOffMeshConnections(&dd, m_editor->getRecastDrawOffset(), -1);

	// Highlight selected tile-based off-mesh connection.
	if (m_selectedTileOffMeshTile >= 0 && m_selectedTileOffMeshIdx >= 0)
	{
		dtNavMesh* nav = m_editor->getNavMesh();
		if (nav)
		{
			const dtMeshTile* tile = nav->getTile(m_selectedTileOffMeshTile);
			if (tile->header && m_selectedTileOffMeshIdx < tile->header->offMeshConCount)
			{
				const dtOffMeshConnection* con = &tile->offMeshCons[m_selectedTileOffMeshIdx];
				const unsigned int col = duRGBA(255, 200, 0, 220);
				const rdVec3D* offset = m_editor->getDetourDrawOffset();

				dd.begin(DU_DRAW_LINES, 4.0f, offset);
				duAppendCircle(&dd, con->posa.x, con->posa.y, con->posa.z + 5.0f, con->rad, col);
				duAppendCircle(&dd, con->posb.x, con->posb.y, con->posb.z + 5.0f, con->rad, col);
				dd.end();
			}
		}
	}
}

void OffMeshConnectionTool::handleRenderOverlay(double* model, double* proj, int* view)
{
	const int h = view[3];
	rdVec2D screenPos;
	
	// Draw start and end point labels
	if (m_hitPosSet && worldToScreen(model, proj, view, m_hitPos.x, m_hitPos.y, m_hitPos.z, screenPos))
	{
		ImGui_RenderText(ImGuiTextAlign_e::kAlignCenter, ImVec2(screenPos.x, h-(screenPos.y-25)), ImVec4(0,0,0,0.8f), "Start");
	}
	
	// Tool help
	if (!m_hitPosSet)
	{
		ImGui_RenderText(ImGuiTextAlign_e::kAlignLeft,
			ImVec2(300, 40), ImVec4(1.0f,1.0f,1.0f,0.75f), "LMB: Create new connection.  SHIFT+LMB: Delete existing connection, click close to start or end point.");
	}
	else
	{
		ImGui_RenderText(ImGuiTextAlign_e::kAlignLeft, 
			ImVec2(300, 40), ImVec4(1.0f,1.0f,1.0f,0.75f), "LMB: Set connection end point and finish.");
	}
}
