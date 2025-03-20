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
#include "DebugUtils/Include/DetourDebugDraw.h"
#include "DebugUtils/Include/RecastDebugDraw.h"
#include "NavEditor/Include/ShapeVolumeTool.h"
#include "NavEditor/Include/InputGeom.h"
#include "NavEditor/Include/Editor.h"

#include "coordsize.h"

#define VALUE_ADJUST_WINDOW 4000.f

// Quick and dirty convex hull.

// Returns true if 'c' is left of line 'a'-'b'.
inline bool left(const rdVec2D* a, const rdVec2D* b, const rdVec2D* c)
{ 
	const float u1 = b->x - a->x;
	const float v1 = b->y - a->y;
	const float u2 = c->x - a->x;
	const float v2 = c->y - a->y;
	return u1 * v2 - v1 * u2 < 0;
}

// Returns true if 'a' is more lower-left than 'b'.
inline bool cmppt(const rdVec2D* a, const rdVec2D* b)
{
	if (a->x < b->x) return true;
	if (a->x > b->x) return false;
	if (a->y < b->y) return true;
	if (a->y > b->y) return false;
	return false;
}
// Calculates convex hull on xy-plane of points on 'pts',
// stores the indices of the resulting hull in 'out' and
// returns number of points on hull.
static int convexhull(const rdVec3D* pts, int npts, int* out)
{
	// Find lower-leftmost point.
	int hull = 0;
	for (int i = 1; i < npts; ++i)
		if (cmppt(&pts[i], &pts[hull]))
			hull = i;
	// Gift wrap hull.
	int endpt = 0;
	int i = 0;
	do
	{
		out[i++] = hull;
		endpt = 0;
		for (int j = 1; j < npts; ++j)
			if (hull == endpt || left(&pts[hull], &pts[endpt], &pts[j]))
				endpt = j;
		hull = endpt;
	}
	while (endpt != out[0]);
	
	return i;
}

static bool isValidVolumeIndex(const int index, const InputGeom* geom)
{
	return index > -1 && index < geom->getShapeVolumeCount();
}

void handleVolumeFlags(int& flags, const char* buttonId)
{
	ImGui::Indent();
	ImGui::Text("Poly Flags");

	const int numPolyFlags = V_ARRAYSIZE(g_navMeshPolyFlagNames);
	char buttonName[64];

	for (int i = 0; i < numPolyFlags; i++)
	{
		snprintf(buttonName, sizeof(buttonName), "%s##%s", g_navMeshPolyFlagNames[i], buttonId);
		ImGui::CheckboxFlags(buttonName, &flags, i == (numPolyFlags - 1) ? DT_POLYFLAGS_ALL : 1 << i);
	}

	ImGui::Unindent();
}

static const char* getBrushNameForArea(const unsigned char area)
{
	switch (area)
	{
	case DT_POLYAREA_GROUND:
		return "Clip";
	case DT_POLYAREA_TRIGGER:
		return "Trigger";
	default:
		return "Invalid";
	}
}

bool handleBrushTypes(int& area, const char* buttonId)
{
	bool ret = false;

	char buttonName[64];
	snprintf(buttonName, sizeof(buttonName), "Brush##%s", buttonId);

	if (ImGui::BeginCombo(buttonName, getBrushNameForArea((unsigned char)area)))
	{
		if (ImGui::Selectable(getBrushNameForArea(DT_POLYAREA_GROUND), area == DT_POLYAREA_GROUND))
		{
			area = DT_POLYAREA_GROUND;
			ret = true;
		}

		if (ImGui::Selectable(getBrushNameForArea(DT_POLYAREA_TRIGGER), area == DT_POLYAREA_TRIGGER))
		{
			area = DT_POLYAREA_TRIGGER;
			ret = true;
		}

		ImGui::EndCombo();
	}

	return ret;
}

ShapeVolumeTool::ShapeVolumeTool() :
	m_selectedVolumeIndex(-1),
	m_editor(0),
	m_selectedPrimitive(VOLUME_CONVEX),
	m_areaType(RC_NULL_AREA),
	m_polyFlags(0),
	m_boxDescent(32),
	m_boxAscent(32),
	m_cylinderRadius(64),
	m_cylinderHeight(128),
	m_convexOffset(0.0f),
	m_convexHeight(650.0f),
	m_convexDescent(150.0f),
	m_npts(0),
	m_nhull(0),
	m_copiedShapeIndex(-1)
{
}

void ShapeVolumeTool::init(Editor* editor)
{
	m_editor = editor;
}

void ShapeVolumeTool::reset()
{
	m_npts = 0;
	m_nhull = 0;
}

static const char* s_primitiveNames[] = {
	"Box",
	"Cylinder",
	"Convex"
};

void ShapeVolumeTool::handleMenu()
{
	ImGui::Text("Create Shape");

	if (ImGui::BeginCombo("Primitive##ShapeVolumeCreate", s_primitiveNames[m_selectedPrimitive]))
	{
		for (int i = 0; i < V_ARRAYSIZE(s_primitiveNames); i++)
		{
			if (ImGui::Selectable(s_primitiveNames[i], i == m_selectedPrimitive))
			{
				m_selectedPrimitive = i;

				m_npts = 0;
				m_nhull = 0;
			}
		}

		ImGui::EndCombo();
	}

	handleBrushTypes(m_areaType, "ShapeVolumeCreate");

	switch (m_selectedPrimitive)
	{
	case VOLUME_BOX:
		ImGui::SliderFloat("Descent##ShapeVolumeCreate", &m_boxDescent, m_boxDescent-(VALUE_ADJUST_WINDOW/2), m_boxDescent+(VALUE_ADJUST_WINDOW/2));
		ImGui::SliderFloat("Ascent##ShapeVolumeCreate", &m_boxAscent, m_boxAscent-(VALUE_ADJUST_WINDOW/2), m_boxAscent+(VALUE_ADJUST_WINDOW/2));
		break;
	case VOLUME_CYLINDER:
		ImGui::SliderFloat("Radius##ShapeVolumeCreate", &m_cylinderRadius, 0.1f, VALUE_ADJUST_WINDOW);
		ImGui::SliderFloat("Height##ShapeVolumeCreate", &m_cylinderHeight, 0.1f, VALUE_ADJUST_WINDOW);
		break;
	case VOLUME_CONVEX:
		ImGui::SliderFloat("Height##ShapeVolumeCreate", &m_convexHeight, 0.1f, VALUE_ADJUST_WINDOW);
		ImGui::SliderFloat("Descent##ShapeVolumeCreate", &m_convexDescent, 0.1f, VALUE_ADJUST_WINDOW);
		ImGui::SliderFloat("Offset##ShapeVolumeCreate", &m_convexOffset, 0.0f, VALUE_ADJUST_WINDOW/2);
		break;
	}

	if (m_areaType == DT_POLYAREA_TRIGGER)
	{
		ImGui::Separator();
		handleVolumeFlags(m_polyFlags, "ShapeVolumeCreate");
	}

	if ((m_npts || m_nhull) && ImGui::Button("Clear Shape##ShapeVolumeCreate"))
	{
		m_npts = 0;
		m_nhull = 0;
	}

	InputGeom* geom = m_editor->getInputGeom();

	if (!geom || !isValidVolumeIndex(m_selectedVolumeIndex, geom))
		return;

	ImGui::Separator();
	ImGui::Text("Modify Shape");

	ShapeVolume& vol = geom->getShapeVolumes()[m_selectedVolumeIndex];

	if (m_selectedVolumeIndex != m_copiedShapeIndex)
	{
		m_shapeCopy = vol;
		m_copiedShapeIndex = m_selectedVolumeIndex;
	}

	int volArea = vol.area;
	handleBrushTypes(volArea, "ShapeVolumeModify");

	if (volArea != vol.area)
		vol.area = (unsigned char)volArea;

	switch (vol.type)
	{
	case VOLUME_BOX:

		ImGui::PushItemWidth(60);
		ImGui::SliderFloat("##ShapeBoxMinsX", &vol.verts[0].x, m_shapeCopy.verts[0].x-(VALUE_ADJUST_WINDOW/2), vol.verts[1].x);
		ImGui::SameLine();
		ImGui::SliderFloat("##ShapeBoxMinsY", &vol.verts[0].y, m_shapeCopy.verts[0].y-(VALUE_ADJUST_WINDOW/2), vol.verts[1].y);
		ImGui::SameLine();
		ImGui::SliderFloat("##ShapeBoxMinsZ", &vol.verts[0].z, m_shapeCopy.verts[0].z-(VALUE_ADJUST_WINDOW/2), vol.verts[1].z);
		ImGui::SameLine();
		ImGui::Text("Mins");

		ImGui::SliderFloat("##ShapeBoxMaxsX", &vol.verts[1].x, vol.verts[0].x, m_shapeCopy.verts[1].x+(VALUE_ADJUST_WINDOW/2));
		ImGui::SameLine();
		ImGui::SliderFloat("##ShapeBoxMaxsY", &vol.verts[1].y, vol.verts[0].y, m_shapeCopy.verts[1].y+(VALUE_ADJUST_WINDOW/2));
		ImGui::SameLine();
		ImGui::SliderFloat("##ShapeBoxMaxsZ", &vol.verts[1].z, vol.verts[0].z, m_shapeCopy.verts[1].z+(VALUE_ADJUST_WINDOW/2));
		ImGui::SameLine();
		ImGui::Text("Maxs");
		ImGui::PopItemWidth();
		break;
	case VOLUME_CYLINDER:

		ImGui::PushItemWidth(55);
		ImGui::SliderFloat("##ShapeCylinderX", &vol.verts[0].x, m_shapeCopy.verts[0].x-(VALUE_ADJUST_WINDOW/2), m_shapeCopy.verts[0].x+(VALUE_ADJUST_WINDOW/2));
		ImGui::SameLine();
		ImGui::SliderFloat("##ShapeCylinderY", &vol.verts[0].y, m_shapeCopy.verts[0].y-(VALUE_ADJUST_WINDOW/2), m_shapeCopy.verts[0].y+(VALUE_ADJUST_WINDOW/2));
		ImGui::SameLine();
		ImGui::SliderFloat("##ShapeCylinderZ", &vol.verts[0].z, m_shapeCopy.verts[0].z-(VALUE_ADJUST_WINDOW/2), m_shapeCopy.verts[0].z+(VALUE_ADJUST_WINDOW/2));
		ImGui::SameLine();
		ImGui::Text("Position");
		ImGui::PopItemWidth();

		ImGui::SliderFloat("Radius##ShapeVolumeModify", &vol.verts[1].x, 0.1f, VALUE_ADJUST_WINDOW);
		ImGui::SliderFloat("Height##ShapeVolumeModify", &vol.verts[1].y, 0.1f, VALUE_ADJUST_WINDOW);
		break;
	case VOLUME_CONVEX:
		ImGui::SliderFloat("Descent##ShapeVolumeModify", &vol.hmin, m_shapeCopy.hmin-(VALUE_ADJUST_WINDOW/2), m_shapeCopy.hmax);
		ImGui::SliderFloat("Ascent##ShapeVolumeModify", &vol.hmax, m_shapeCopy.hmin, m_shapeCopy.hmax+(VALUE_ADJUST_WINDOW/2));

		char sliderId[64];

		for (int i = 0; i < vol.nverts; i++)
		{
			const int len = snprintf(sliderId, sizeof(sliderId), "##(%d)ShapeConvexX", i);
			if (len < 0)
			{
				rdAssert(0);
				break;
			}

			ImGui::PushItemWidth(60);
			ImGui::SliderFloat(sliderId, &vol.verts[i].x, m_shapeCopy.verts[i].x-(VALUE_ADJUST_WINDOW/2), m_shapeCopy.verts[i].x+(VALUE_ADJUST_WINDOW/2));
			ImGui::SameLine();
			sliderId[len] = 'Y';
			ImGui::SliderFloat(sliderId, &vol.verts[i].y, m_shapeCopy.verts[i].y-(VALUE_ADJUST_WINDOW/2), m_shapeCopy.verts[i].y+(VALUE_ADJUST_WINDOW/2));
			ImGui::SameLine();
			sliderId[len] = 'Z';
			ImGui::SliderFloat(sliderId, &vol.verts[i].z, m_shapeCopy.verts[i].z-(VALUE_ADJUST_WINDOW/2), m_shapeCopy.verts[i].z+(VALUE_ADJUST_WINDOW/2));
			ImGui::SameLine();

			snprintf(sliderId, sizeof(sliderId), "Vert #%d", i);

			ImGui::Text(sliderId);
			ImGui::PopItemWidth();
		}
		break;
	}

	if (vol.area == DT_POLYAREA_TRIGGER)
	{
		ImGui::Separator();

		int flags = vol.flags;
		handleVolumeFlags(flags, "ShapeVolumeModify");

		if (flags != vol.flags)
			vol.flags = (unsigned short)flags;
	}

	if (ImGui::Button("Reset Shape##ShapeVolumeModify"))
	{
		vol = m_shapeCopy;
	}

	if (ImGui::Button("Delete Shape##ShapeVolumeModify"))
	{
		geom->deleteShapeVolume(m_selectedVolumeIndex);
		m_selectedVolumeIndex = -1;
		m_copiedShapeIndex = -1;

		return;
	}
}

void ShapeVolumeTool::handleClick(const rdVec3D* /*s*/, const rdVec3D* p, const int v, bool shift)
{
	if (!m_editor) return;
	InputGeom* geom = m_editor->getInputGeom();
	if (!geom) return;
	
	if (shift)
	{
		m_selectedVolumeIndex = v;

		if (v == -1)
			m_copiedShapeIndex = -1;
	}
	else // Create
	{
		switch (m_selectedPrimitive)
		{
		case VOLUME_BOX:
			rdVcopy(&m_pts[m_npts], p);
			m_npts++;

			if (m_npts > 1)
			{
				rdVec3D* bmin = &m_pts[0];
				rdVec3D* bmax = &m_pts[1];
				
				if (bmin->x > bmax->x)
					rdSwap(bmin->x, bmax->x);
				if (bmin->y > bmax->y)
					rdSwap(bmin->y, bmax->y);
				if (bmin->z > bmax->z)
					rdSwap(bmin->z, bmax->z);

				bmin->z -= m_boxDescent;
				bmax->z += m_boxAscent;

				m_selectedVolumeIndex = geom->addBoxVolume(bmin, bmax, (unsigned short)m_polyFlags, (unsigned char)m_areaType);

				m_npts = 0;
				m_nhull = 0;
			}
			break;
		case VOLUME_CYLINDER:
			m_selectedVolumeIndex = geom->addCylinderVolume(p, m_cylinderRadius, m_cylinderHeight, (unsigned short)m_polyFlags, (unsigned char)m_areaType);
			break;
		case VOLUME_CONVEX:
			// If clicked on that last pt, create the shape.
			if (m_npts && rdVdistSqr(p, &m_pts[(m_npts-1)]) < rdSqr(0.2f))
			{
				if (m_nhull > 2)
				{
					// Create shape.
					rdVec3D verts[MAX_SHAPEVOL_PTS];
					for (int i = 0; i < m_nhull; ++i)
						rdVcopy(&verts[i], &m_pts[m_hull[i]]);
						
					float minh = FLT_MAX, maxh = 0;
					for (int i = 0; i < m_nhull; ++i)
						minh = rdMin(minh, verts[i].z);
					minh -= m_convexDescent;
					maxh = minh + m_convexHeight;

					if (m_convexOffset > 0.01f)
					{
						rdVec3D offset[MAX_SHAPEVOL_PTS*2];
						const int noffset = rcOffsetPoly(verts, m_nhull, m_convexOffset, offset, MAX_SHAPEVOL_PTS*2);
						if (noffset > 0)
							m_selectedVolumeIndex = geom->addConvexVolume(offset, noffset, minh, maxh, (unsigned short)m_polyFlags, (unsigned char)m_areaType);
					}
					else
					{
						m_selectedVolumeIndex = geom->addConvexVolume(verts, m_nhull, minh, maxh, (unsigned short)m_polyFlags, (unsigned char)m_areaType);
					}
				}
				
				m_npts = 0;
				m_nhull = 0;
			}
			else
			{
				// Add new point 
				if (m_npts < MAX_SHAPEVOL_PTS)
				{
					rdVcopy(&m_pts[m_npts], p);
					m_npts++;

					// Update hull.
					if (m_npts > 1)
						m_nhull = convexhull(m_pts, m_npts, m_hull);
					else
						m_nhull = 0;
				}
			}		
			break;
		}
	}

	printf("<%f, %f, %f>\n", p->x, p->y, p->z);
}

void ShapeVolumeTool::handleToggle()
{
}

void ShapeVolumeTool::handleStep()
{
}

void ShapeVolumeTool::handleUpdate(const float /*dt*/)
{
}

void ShapeVolumeTool::handleRender()
{
	duDebugDraw& dd = m_editor->getDebugDraw();
	const rdVec3D* drawOffset = m_editor->getDetourDrawOffset();
	
	// Find height extent of the shape.
	float minh = FLT_MAX, maxh = 0;
	for (int i = 0; i < m_npts; ++i)
		minh = rdMin(minh, m_pts[i].z);
	minh -= m_convexDescent;
	maxh = minh + m_convexHeight;

	dd.begin(DU_DRAW_POINTS, 4.0f, drawOffset);
	for (int i = 0; i < m_npts; ++i)
	{
		unsigned int col = duRGBA(255,255,255,255);
		if (i == m_npts-1)
			col = duRGBA(240,32,16,255);

		dd.vertex(m_pts[i].x,m_pts[i].y,m_pts[i].z+0.1f, col);
	}
	dd.end();

	if (m_selectedPrimitive == VOLUME_CONVEX)
	{
		dd.begin(DU_DRAW_LINES, 2.0f, drawOffset);
		for (int i = 0, j = m_nhull-1; i < m_nhull; j = i++)
		{
			const rdVec3D* vi = &m_pts[m_hull[j]];
			const rdVec3D* vj = &m_pts[m_hull[i]];
			dd.vertex(vj->x,vj->y,minh, duRGBA(255,255,255,64));
			dd.vertex(vi->x,vi->y,minh, duRGBA(255,255,255,64));
			dd.vertex(vj->x,vj->y,maxh, duRGBA(255,255,255,64));
			dd.vertex(vi->x,vi->y,maxh, duRGBA(255,255,255,64));
			dd.vertex(vj->x,vj->y,minh, duRGBA(255,255,255,64));
			dd.vertex(vj->x,vj->y,maxh, duRGBA(255,255,255,64));
		}
		dd.end();
	}
}

void ShapeVolumeTool::handleRenderOverlay(double* /*model*/, double* /*proj*/, int* /*view*/)
{
	// Tool help
	if (!m_npts)
	{
		ImGui_RenderText(ImGuiTextAlign_e::kAlignLeft,
			ImVec2(300, 40), ImVec4(1.0f,1.0f,1.0f,0.75f), "LMB: Create new shape.  SHIFT+LMB: Delete existing shape (click on a shape).");
	}
	else
	{
		ImGui_RenderText(ImGuiTextAlign_e::kAlignLeft,
			ImVec2(300, 60), ImVec4(1.0f,1.0f,1.0f,0.75f), "The shape will be convex hull of all added points.");
	}
}
