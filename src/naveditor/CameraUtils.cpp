#include "recast/Shared/Include/SharedCommon.h"
#include "include/CameraUtils.h"

bool worldToScreen(const GLdouble* const model, const GLdouble* const proj, const int* const view, const float ix, const float iy, const float iz, rdVec2D& out)
{
	const GLdouble camForwardX = model[3];
	const GLdouble camForwardY = model[7];
	const GLdouble camForwardZ = model[11];
	const GLdouble vecToPointX = ix - model[12];
	const GLdouble vecToPointY = iy - model[13];
	const GLdouble vecToPointZ = iz - model[14];
	const GLdouble dot = camForwardX * vecToPointX + camForwardY * vecToPointY + camForwardZ * vecToPointZ;

	if (dot < 0)
		return false; // Behind the camera.

	GLdouble x, y, z;

	if (!gluProject((GLdouble)ix, (GLdouble)iy, (GLdouble)iz, model, proj, view, &x, &y, &z))
		return false;

	if (z < 0.0 || z > 1.0)
		return false; // Outside depth range.

	out.init((float)x, (float)y);
	return true;
}

bool worldToScreen(const GLdouble* const model, const GLdouble* const proj, const int* const view, const rdVec3D& pos, rdVec2D& out)
{
	return worldToScreen(model, proj, view, pos.x, pos.y, pos.z, out);
}
