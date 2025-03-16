#pragma once
#include "mathlib/vector.h"
#include "mathlib/vmatrix.h"
#include "view_shared.h"

#ifndef DEDICATED

bool ClipTransform(const VMatrix& w2sMatrix, const Vector3D& point, Vector3D* const pClip);
bool ScreenTransform(const CViewSetup& view, const VMatrix& w2sMatrix, const Vector3D& point, Vector3D* const pClip);

///////////////////////////////////////////////////////////////////////////////
class VGL_RMain : public IDetour
{
	virtual void GetAdr(void) const { }
	virtual void GetFun(void) const { }
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif