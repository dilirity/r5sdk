#ifndef NAVMESHPOLYEDITTOOL_H
#define NAVMESHPOLYEDITTOOL_H

#include "NavEditor/Include/Editor.h"

// Interactively select and remove individual navmesh polygons.

class NavMeshPolyEditTool : public EditorTool
{
	Editor* m_editor;

	rdPermVector<dtPolyRef> m_selectedPolys;

	rdVec3D m_hitPos;
	bool m_hitPosSet;

public:
	NavMeshPolyEditTool();
	virtual ~NavMeshPolyEditTool();

	virtual int type() { return TOOL_POLY_EDIT; }
	virtual void init(Editor* editor);
	virtual void reset();
	virtual void handleMenu();
	virtual void handleClick(const rdVec3D* s, const rdVec3D* p, const int v, bool shift);
	virtual void handleToggle();
	virtual void handleStep();
	virtual void handleUpdate(const float dt);
	virtual void handleRender();
	virtual void handleRenderOverlay(double* model, double* proj, int* view);

private:
	void selectGroup();
	void removeSelectedPolys();
	bool isSelected(dtPolyRef ref) const;
	void toggleSelection(dtPolyRef ref);

	// Explicitly disabled copy constructor and copy assignment operator.
	NavMeshPolyEditTool(const NavMeshPolyEditTool&);
	NavMeshPolyEditTool& operator=(const NavMeshPolyEditTool&);
};

#endif // NAVMESHPOLYEDITTOOL_H
