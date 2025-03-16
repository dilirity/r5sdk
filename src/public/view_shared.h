#ifndef VIEW_SHARED_H
#define VIEW_SHARED_H

#include "mathlib/vector.h"

class CViewSetup
{
public:
	int unk1;
	int unk2;
	int x;
	int y;
	int width;
	int height;
	int unk5[44];
	float unk6[2];
	float fov;
	float fovViewmodel;
	Vector3D origin;
	QAngle angles;
	float zNear;
	float zNearViewmodel;
	float zFar;
	float zFarViewmodel;
	float m_flAspectRatio;
	float m_flNearBlurDepth;
	float m_flNearFocusDepth;
	float m_flFarFocusDepth;
	float m_flFarBlurDepth;
	float m_flNearBlurRadius;
	float m_flFarBlurRadius;
	int m_nDoFQuality;
	float pad2[17];
	char unkOrPad;
	bool m_bOrtho_165;
	Vector3D cockpitOrigin;
	QAngle cockpitAngles;
	float morePadding[2];
};

#endif // VIEW_SHARED_H
