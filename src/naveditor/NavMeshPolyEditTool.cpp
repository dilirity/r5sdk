#include "Shared/Include/SharedCommon.h"
#include "Detour/Include/DetourNavMesh.h"
#include "Detour/Include/DetourNavMeshBuilder.h"
#include "Detour/Include/DetourNavMeshQuery.h"
#include "DebugUtils/Include/DetourDebugDraw.h"
#include "NavEditor/Include/NavMeshPolyEditTool.h"
#include "NavEditor/Include/InputGeom.h"
#include "NavEditor/Include/Editor.h"

NavMeshPolyEditTool::NavMeshPolyEditTool() :
	m_editor(0),
	m_hitPosSet(false)
{
	m_hitPos[0] = 0.0f;
	m_hitPos[1] = 0.0f;
	m_hitPos[2] = 0.0f;
}

NavMeshPolyEditTool::~NavMeshPolyEditTool()
{
}

void NavMeshPolyEditTool::init(Editor* editor)
{
	m_editor = editor;
	reset();
}

void NavMeshPolyEditTool::reset()
{
	m_hitPosSet = false;
	m_selectedPolys.clear();
}

bool NavMeshPolyEditTool::isSelected(dtPolyRef ref) const
{
	for (int i = 0; i < (int)m_selectedPolys.size(); i++)
	{
		if (m_selectedPolys[i] == ref)
			return true;
	}
	return false;
}

void NavMeshPolyEditTool::toggleSelection(dtPolyRef ref)
{
	for (int i = 0; i < (int)m_selectedPolys.size(); i++)
	{
		if (m_selectedPolys[i] == ref)
		{
			// Remove by swapping with last element.
			m_selectedPolys[i] = m_selectedPolys[m_selectedPolys.size() - 1];
			m_selectedPolys.pop_back();
			return;
		}
	}
	m_selectedPolys.push_back(ref);
}

// Removes a specific link index from a polygon's link chain and frees it.
static void unlinkFromChain(dtMeshTile* tile, dtPoly* poly, unsigned int linkIdx)
{
	unsigned int k = poly->firstLink;
	unsigned int pk = DT_NULL_LINK;
	while (k != DT_NULL_LINK)
	{
		if (k == linkIdx)
		{
			const unsigned int nk = tile->links[k].next;
			if (pk == DT_NULL_LINK)
				poly->firstLink = nk;
			else
				tile->links[pk].next = nk;
			tile->freeLink(k);
			return;
		}
		pk = k;
		k = tile->links[k].next;
	}
}

// Removes all links from a tile's polygons that reference a specific poly ref.
static void removeLinksToPolyRef(dtNavMesh* nav, dtMeshTile* tile, dtPolyRef targetRef)
{
	if (!tile->header) return;

	const unsigned int targetTileIdx = nav->decodePolyIdTile(targetRef);
	const unsigned int targetPolyIdx = nav->decodePolyIdPoly(targetRef);

	for (int i = 0; i < tile->header->polyCount; i++)
	{
		dtPoly* poly = &tile->polys[i];
		unsigned int j = poly->firstLink;
		unsigned int pj = DT_NULL_LINK;
		while (j != DT_NULL_LINK)
		{
			const dtLink& currLink = tile->links[j];
			if (nav->decodePolyIdTile(currLink.ref) == targetTileIdx &&
				nav->decodePolyIdPoly(currLink.ref) == targetPolyIdx)
			{
				// Remove link.
				const unsigned int nj = currLink.next;
				if (pj == DT_NULL_LINK)
					poly->firstLink = nj;
				else
					tile->links[pj].next = nj;

				if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
					poly->flags &= ~DT_POLYFLAGS_JUMP_LINKED;

				tile->freeLink(j);
				j = nj;
			}
			else
			{
				pj = j;
				j = currLink.next;
			}
		}
	}
}

void NavMeshPolyEditTool::removeSelectedPolys()
{
	if (!m_editor) return;
	dtNavMesh* nav = m_editor->getNavMesh();
	if (!nav) return;
	if (m_selectedPolys.empty()) return;

	// Track which tile indices are affected.
	rdPermVector<unsigned int> affectedTiles;

	static const int MAX_NEIS = 32;
	dtMeshTile* neis[MAX_NEIS];

	// Stage 1: Disconnect all links to/from selected polygons before disabling.
	for (int i = 0; i < (int)m_selectedPolys.size(); i++)
	{
		const dtPolyRef selRef = m_selectedPolys[i];
		const unsigned int selTileIdx = nav->decodePolyIdTile(selRef);
		const unsigned int selPolyIdx = nav->decodePolyIdPoly(selRef);
		dtMeshTile* selTile = nav->getTile(selTileIdx);
		dtPoly* selPoly = &selTile->polys[selPolyIdx];

		// Step A: Walk this polygon's outgoing links. For traverse links
		// with a valid reverseLink, remove the reverse link from the
		// target polygon's chain. This prevents dtUpdateNavMeshData from
		// accessing stale reverseLink indices in newLinkIdMap.
		unsigned int j = selPoly->firstLink;
		while (j != DT_NULL_LINK)
		{
			dtLink& link = selTile->links[j];
			const unsigned int nextJ = link.next;

			if (link.reverseLink != DT_NULL_TRAVERSE_REVERSE_LINK)
			{
				const unsigned int targetTileIdx = nav->decodePolyIdTile(link.ref);
				const unsigned int targetPolyIdx = nav->decodePolyIdPoly(link.ref);
				dtMeshTile* targetTile = nav->getTile(targetTileIdx);
				dtPoly* targetPoly = &targetTile->polys[targetPolyIdx];

				unlinkFromChain(targetTile, targetPoly, (unsigned int)link.reverseLink);
			}

			selTile->freeLink(j);
			j = nextJ;
		}
		selPoly->firstLink = DT_NULL_LINK;

		// Step B: Remove any remaining links from the same tile's polygons
		// and from neighbor tiles' polygons that reference this polygon.
		removeLinksToPolyRef(nav, selTile, selRef);

		for (int side = 0; side < 8; side++)
		{
			const int nneis = nav->getNeighbourTilesAt(
				selTile->header->x, selTile->header->y, side, neis, MAX_NEIS);
			for (int n = 0; n < nneis; n++)
				removeLinksToPolyRef(nav, neis[n], selRef);
		}

		// Step C: Mark the polygon as disabled.
		selPoly->groupId = DT_UNLINKED_POLY_GROUP;
		selPoly->flags = DT_POLYFLAGS_DISABLED;

		// Track affected tile.
		bool found = false;
		for (int t = 0; t < (int)affectedTiles.size(); t++)
		{
			if (affectedTiles[t] == selTileIdx)
			{
				found = true;
				break;
			}
		}
		if (!found)
			affectedTiles.push_back(selTileIdx);
	}

	// Stage 2: Update each affected tile.
	for (int i = 0; i < (int)affectedTiles.size(); i++)
	{
		const unsigned int tileIdx = affectedTiles[i];
		dtMeshTile* tile = nav->getTile(tileIdx);
		dtMeshHeader* header = tile->header;

		if (!header) continue;

		// Check if all polys in this tile are disabled.
		int numUnlinkedPolys = 0;
		for (int j = 0; j < header->polyCount; j++)
		{
			if (tile->polys[j].flags & DT_POLYFLAGS_DISABLED)
				numUnlinkedPolys++;
		}

		if (numUnlinkedPolys == header->polyCount)
		{
			header->userId = DT_FULL_UNLINKED_TILE_USER_ID;
			nav->removeTile(nav->getTileRef(tile), 0, 0);
		}
		else
		{
			header->userId = DT_SEMI_UNLINKED_TILE_USER_ID;
			dtUpdateNavMeshData(nav, tileIdx);
		}
	}

	// Stage 3: Clean up traverse link poly map.
	std::map<TraverseLinkPolyPair, unsigned int>& polyMap = m_editor->getTraverseLinkPolyMap();

	for (auto it = polyMap.cbegin(); it != polyMap.cend();)
	{
		const TraverseLinkPolyPair& pair = it->first;

		bool shouldErase = false;
		for (int i = 0; i < (int)m_selectedPolys.size(); i++)
		{
			if (pair.poly1 == m_selectedPolys[i] || pair.poly2 == m_selectedPolys[i])
			{
				shouldErase = true;
				break;
			}
		}

		if (shouldErase)
			it = polyMap.erase(it);
		else
			++it;
	}

	// Stage 4: Rebuild pathing data and refresh display.
	m_editor->createStaticPathingData();
	m_editor->invalidateNavMeshCache();

	// Poly refs are invalidated after tile compaction, clear selection.
	m_selectedPolys.clear();
	m_hitPosSet = false;
}

void NavMeshPolyEditTool::handleMenu()
{
	dtNavMesh* nav = m_editor->getNavMesh();
	if (!nav) return;

	const int numSelected = (int)m_selectedPolys.size();

	ImGui::Text("Selected: %d polygon%s", numSelected, numSelected == 1 ? "" : "s");

	if (numSelected > 0)
	{
		if (ImGui::Button("Remove Selected"))
		{
			removeSelectedPolys();
		}

		if (ImGui::Button("Clear Selection"))
		{
			m_selectedPolys.clear();
			m_hitPosSet = false;
		}
	}
}

void NavMeshPolyEditTool::handleClick(const rdVec3D* s, const rdVec3D* p, const int /*v*/, bool shift)
{
	rdIgnoreUnused(s);
	rdIgnoreUnused(shift);

	if (!m_editor) return;
	dtNavMesh* nav = m_editor->getNavMesh();
	if (!nav) return;
	dtNavMeshQuery* query = m_editor->getNavMeshQuery();
	if (!query) return;

	m_hitPos = *p;
	m_hitPosSet = true;

	const rdVec3D halfExtents(64, 64, 128);
	dtQueryFilter filter;
	dtPolyRef ref = 0;

	if (dtStatusSucceed(query->findNearestPoly(p, &halfExtents, &filter, &ref, 0)) && ref)
	{
		// Skip already-disabled polygons.
		const dtMeshTile* tile;
		const dtPoly* poly;
		nav->getTileAndPolyByRefUnsafe(ref, &tile, &poly);

		if (poly->flags & DT_POLYFLAGS_DISABLED)
			return;

		toggleSelection(ref);
	}
}

void NavMeshPolyEditTool::handleToggle()
{
}

void NavMeshPolyEditTool::handleStep()
{
}

void NavMeshPolyEditTool::handleUpdate(const float /*dt*/)
{
}

void NavMeshPolyEditTool::handleRender()
{
	duDebugDraw& dd = m_editor->getDebugDraw();

	if (m_hitPosSet)
	{
		const float s = m_editor->getAgentRadius();
		const unsigned int col = duRGBA(255, 100, 0, 255);
		dd.begin(DU_DRAW_LINES);
		dd.vertex(m_hitPos[0]-s,m_hitPos[1],m_hitPos[2], col);
		dd.vertex(m_hitPos[0]+s,m_hitPos[1],m_hitPos[2], col);
		dd.vertex(m_hitPos[0],m_hitPos[1]-s,m_hitPos[2], col);
		dd.vertex(m_hitPos[0],m_hitPos[1]+s,m_hitPos[2], col);
		dd.vertex(m_hitPos[0],m_hitPos[1],m_hitPos[2]-s, col);
		dd.vertex(m_hitPos[0],m_hitPos[1],m_hitPos[2]+s, col);
		dd.end();
	}

	const dtNavMesh* nav = m_editor->getNavMesh();
	const rdVec3D* drawOffset = m_editor->getDetourDrawOffset();
	const unsigned int drawFlags = m_editor->getNavMeshDrawFlags();

	if (nav && !m_selectedPolys.empty())
	{
		for (int i = 0; i < (int)m_selectedPolys.size(); i++)
		{
			duDebugDrawNavMeshPoly(&dd, *nav, m_selectedPolys[i], drawOffset, drawFlags, duRGBA(255, 100, 0, 160));
		}
	}
}

void NavMeshPolyEditTool::handleRenderOverlay(double* model, double* proj, int* view)
{
	rdIgnoreUnused(model);
	rdIgnoreUnused(proj);
	rdIgnoreUnused(view);

	ImGui_RenderText(ImGuiTextAlign_e::kAlignLeft, ImVec2(300, 40), ImVec4(1.0f,1.0f,1.0f,0.75f), "LMB: Toggle polygon selection.");
}
