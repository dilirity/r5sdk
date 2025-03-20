#ifndef CAMERAUTILS_H
#define CAMERAUTILS_H

bool worldToScreen(const GLdouble* const model, const GLdouble* const proj, const int* const view, const float ix, const float iy, const float iz, rdVec2D& out);
bool worldToScreen(const GLdouble* const model, const GLdouble* const proj, const int* const view, const rdVec3D& pos, rdVec2D& out);

#endif // CAMERAUTILS_H
