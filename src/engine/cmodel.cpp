//=============================================================================//
//
// Purpose: BSP collision!
//
// $NoKeywords: $
//=============================================================================//
#include "mathlib/vector.h"
#include "public/engine/ICollideable.h"
#include "cmodel.h"

//-----------------------------------------------------------------------------
// Purpose: returns the world-space center of an entity
//-----------------------------------------------------------------------------
void CM_WorldSpaceCenter(const ICollideable* const pCollideable, Vector3D* const pCenter)
{
	Vector3D vecLocalCenter;
	VectorAdd(pCollideable->OBBMins(), pCollideable->OBBMaxs(), vecLocalCenter);

	vecLocalCenter *= 0.5f;
	QAngle vecLocalAngles;

	if ((pCollideable->GetCollisionAngles(vecLocalAngles) == vec3_angle) || (vecLocalCenter == vec3_origin))
	{
		VectorAdd(vecLocalCenter, pCollideable->GetCollisionOrigin(), *pCenter);
	}
	else
	{
		VectorTransform(vecLocalCenter, pCollideable->CollisionToWorldTransform(), *pCenter);
	}
}
