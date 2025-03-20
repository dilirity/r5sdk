//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef COLLISIONPROPERTY_H
#define COLLISIONPROPERTY_H

#include "mathlib/vector.h"
#include "engine/ICollideable.h"

class CBaseEntity;
struct model_t;

class CCollisionProperty : public ICollideable
{
public:
	// NOTE: these are implemented in the game engine, they are stubs
	// here as CBaseEntity has a static instance of CCollisionProperty.
	// The simpler methods have been reconstructed for reference, but
	// these aren't the ones that are used since currently all instances
	// of this class is allocated by the game engine.

	virtual IHandleEntity* GetEntityHandle() override { return (IHandleEntity*)m_pOuter; }
	virtual const Vector3D& OBBMins() const override { return m_vecMins; }
	virtual const Vector3D& OBBMaxs() const override { return m_vecMaxs; }

	virtual void WorldSpaceAABB(WorldSpaceTriggerBounds_s* const /*wstb*/) const override { };

	// Returns the hit box test radius.
	virtual float GetHitBoxTestRadius() const override { return m_hitboxTestRadius; };

	// Returns the bounds of a world-space box used when the collideable is being traced
	// against as a trigger. It's only valid to call these methods if the solid flags
	// have the FSOLID_USE_TRIGGER_BOUNDS flag set.
	virtual void WorldSpaceTriggerBounds(WorldSpaceTriggerBounds_s* const /*wstb*/) const override { };
	virtual void WorldSpaceTriggerBounds(Vector3D* const /*pVecWorldMins*/, Vector3D* const /*pVecWorldMaxs*/) const override  { };

	// Returns the BRUSH model index if this is a brush model. Otherwise, returns -1.
	virtual int				GetCollisionModelIndex() const override { };

	// Return the model, if it's a studio model.
	virtual const model_t* GetCollisionModel() const override { };

	// Get angles and origin.
	virtual const Vector3D& GetCollisionOrigin() const override { };
	virtual const QAngle& GetCollisionAngles(QAngle& out) const override { };
	virtual const matrix3x4_t& CollisionToWorldTransform() const override  { };

private:
	CBaseEntity* m_pOuter;
	Vector3D m_vecMins;
	Vector3D m_vecMaxs;
	int m_usSolidFlags;
	char m_nSolidType;
	char m_triggerBloat;
	char m_collisionDetailLevel;
	char m_isInDirtyList;
	char m_hasDirtyBounds;
	char m_hiddenFromSpatialQueries;
	__int16 m_dirtyListIndex;
	char m_nSurroundType;
	char gap_35[1];
	__int16 m_spatialAccelHandle;
	__int16 m_partitionMask;
	char gap_3a[2];
	float m_flRadius;
	Vector3D m_vecSpecifiedSurroundingMins;
	Vector3D m_vecSpecifiedSurroundingMaxs;
	Vector3D m_vecSurroundingMins;
	Vector3D m_vecSurroundingMaxs;
	float m_hitboxTestRadius;
};

static_assert(sizeof(CCollisionProperty) == 0x78);

#endif // COLLISIONPROPERTY_H