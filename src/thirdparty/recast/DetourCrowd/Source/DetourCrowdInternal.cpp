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

#include "DetourCrowd\Include\DetourCrowd.h"
#include "DetourCrowd\Include\DetourCrowdInternal.h"
#include "DetourCrowd\Include\DetourObstacleAvoidance.h"
#include "Detour\Include\DetourNavMesh.h"
#include "Detour\Include\DetourNavMeshQuery.h"
#include "Shared\Include\SharedCommon.h"
#include "Shared\Include\SharedMath.h"
#include "Shared\Include\SharedAssert.h"
#include "Shared\Include\SharedAlloc.h"


void integrate(dtCrowdAgent* ag, const float dt)
{
	// Fake dynamic constraint.
	const float maxDelta = ag->params.maxAcceleration * dt;
	rdVec3D dv;
	rdVsub(&dv, &ag->nvel, &ag->vel);
	float ds = rdVlen(&dv);
	if (ds > maxDelta)
		rdVscale(&dv, &dv, maxDelta/ds);
	rdVadd(&ag->vel, &ag->vel, &dv);
	
	// Integrate
	if (rdVlen(&ag->vel) > 0.0001f)
		rdVmad(&ag->npos, &ag->npos, &ag->vel, dt);
	else
		rdVset(&ag->vel,0,0,0);
}

static inline bool checkLinkProximity(const dtCrowdAgent* ag, const float radius)
{
	const float distSq = rdVdist2DSqr(&ag->npos, &ag->cornerVerts[(ag->ncorners-1)]);
	if (distSq < radius*radius)
		return true;

	return false;
}

bool overOffmeshConnection(const dtCrowdAgent* ag, const float radius)
{
	if (!ag->ncorners)
		return false;
	
	const bool offMeshConnection = dtIsStraightPathOffmeshConnection(ag->cornerFlags[ag->ncorners-1]);
	if (offMeshConnection)
	{
		if (checkLinkProximity(ag, radius))
			return true;
	}
	
	return false;
}

bool overTraversePortal(const dtCrowdAgent* ag, const dtNavMeshQuery* query, const dtQueryFilter* filter, const float radius)
{
	if (!ag->ncorners)
		return false;
	
	const unsigned char curJump = ag->cornerJumps[ag->ncorners-1];
	const bool traversePortal = curJump != DT_NULL_TRAVERSE_TYPE;

	if (traversePortal)
	{
		// note(kawe): if this asserts, then off-mesh connection
		// links made it in the path which should never happen.
		rdAssert(!dtIsTraverseTypeOffMesh(curJump));

		float distToWall = 0;
		rdVec3D hitPos;
		rdVec3D hitNormal;

		if (dtStatusFailed(query->findDistanceToWall(ag->cornerPolys[(ag->ncorners - 1)],
			&ag->npos, radius, filter, &distToWall, &hitPos, &hitNormal)))
			return false;

		if (distToWall < radius)
			return true;
	}
	
	return false;
}

float getDistanceToGoal(const dtCrowdAgent* ag, const float range)
{
	if (!ag->ncorners)
		return range;
	
	const bool endOfPath = dtIsStraightPathEnd(ag->cornerFlags[ag->ncorners-1]);
	if (endOfPath)
		return rdMin(rdVdist2D(&ag->npos, &ag->cornerVerts[(ag->ncorners-1)]), range);
	
	return range;
}

void calcSmoothSteerDirection(const dtCrowdAgent* ag, rdVec3D* dir)
{
	if (!ag->ncorners)
	{
		dir->init(0,0,0);
		return;
	}
	
	const int ip0 = 0;
	const int ip1 = rdMin(1, ag->ncorners-1);
	const rdVec3D* p0 = &ag->cornerVerts[ip0];
	const rdVec3D* p1 = &ag->cornerVerts[ip1];
	
	rdVec3D dir0, dir1;
	rdVsub(&dir0, p0, &ag->npos);
	rdVsub(&dir1, p1, &ag->npos);
	dir0.z = 0;
	dir1.z = 0;
	
	float len0 = rdVlen(&dir0);
	float len1 = rdVlen(&dir1);
	if (len1 > 0.001f)
		rdVscale(&dir1,&dir1,1.0f/len1);
	
	dir->x = dir0.x - dir1.x*len0*0.5f;
	dir->y = dir0.y - dir1.y*len0*0.5f;
	dir->z = 0;
	
	rdVnormalize(dir);
}

void calcStraightSteerDirection(const dtCrowdAgent* ag, rdVec3D* dir)
{
	if (!ag->ncorners)
	{
		dir->init(0,0,0);
		return;
	}
	rdVsub(dir, &ag->cornerVerts[0], &ag->npos);
	dir->z = 0;
	rdVnormalize(dir);
}

int addNeighbour(const int idx, const float dist,
						dtCrowdNeighbour* neis, const int nneis, const int maxNeis)
{
	// Insert neighbour based on the distance.
	dtCrowdNeighbour* nei = 0;
	if (!nneis)
	{
		nei = &neis[nneis];
	}
	else if (dist >= neis[nneis-1].dist)
	{
		if (nneis >= maxNeis)
			return nneis;
		nei = &neis[nneis];
	}
	else
	{
		int i;
		for (i = 0; i < nneis; ++i)
			if (dist <= neis[i].dist)
				break;
		
		const int tgt = i+1;
		const int n = rdMin(nneis-i, maxNeis-tgt);
		
		rdAssert(tgt+n <= maxNeis);
		
		if (n > 0)
			memmove(&neis[tgt], &neis[i], sizeof(dtCrowdNeighbour)*n);
		nei = &neis[i];
	}
	
	memset(nei, 0, sizeof(dtCrowdNeighbour));
	
	nei->idx = idx;
	nei->dist = dist;
	
	return rdMin(nneis+1, maxNeis);
}

int getNeighbours(const rdVec3D* pos, const float height, const float range,
						 const dtCrowdAgent* skip, dtCrowdNeighbour* result, const int maxResult,
						 dtCrowdAgent** agents, const int /*nagents*/, dtProximityGrid* grid)
{
	int n = 0;
	
	static const int MAX_NEIS = 32;
	unsigned short ids[MAX_NEIS];
	int nids = grid->queryItems(pos->x-range, pos->y-range,
								pos->x+range, pos->y+range,
								ids, MAX_NEIS);
	
	for (int i = 0; i < nids; ++i)
	{
		const dtCrowdAgent* ag = agents[ids[i]];
		
		if (ag == skip) continue;
		
		// Check for overlap.
		rdVec3D diff;
		rdVsub(&diff, pos, &ag->npos);
		if (rdMathFabsf(diff.z) >= (height+ag->params.height)/2.0f)
			continue;
		diff.z = 0;
		const float distSqr = rdVlenSqr(&diff);
		if (distSqr > rdSqr(range))
			continue;
		
		n = addNeighbour(ids[i], distSqr, result, n, maxResult);
	}
	return n;
}

int addToOptQueue(dtCrowdAgent* newag, dtCrowdAgent** agents, const int nagents, const int maxAgents)
{
	// Insert neighbour based on greatest time.
	int slot = 0;
	if (!nagents)
	{
		slot = nagents;
	}
	else if (newag->topologyOptTime <= agents[nagents-1]->topologyOptTime)
	{
		if (nagents >= maxAgents)
			return nagents;
		slot = nagents;
	}
	else
	{
		int i;
		for (i = 0; i < nagents; ++i)
			if (newag->topologyOptTime >= agents[i]->topologyOptTime)
				break;
		
		const int tgt = i+1;
		const int n = rdMin(nagents-i, maxAgents-tgt);
		
		rdAssert(tgt+n <= maxAgents);
		
		if (n > 0)
			memmove(&agents[tgt], &agents[i], sizeof(dtCrowdAgent*)*n);
		slot = i;
	}
	
	agents[slot] = newag;
	
	return rdMin(nagents+1, maxAgents);
}

int addToPathQueue(dtCrowdAgent* newag, dtCrowdAgent** agents, const int nagents, const int maxAgents)
{
	// Insert neighbour based on greatest time.
	int slot = 0;
	if (!nagents)
	{
		slot = nagents;
	}
	else if (newag->targetReplanTime <= agents[nagents-1]->targetReplanTime)
	{
		if (nagents >= maxAgents)
			return nagents;
		slot = nagents;
	}
	else
	{
		int i;
		for (i = 0; i < nagents; ++i)
			if (newag->targetReplanTime >= agents[i]->targetReplanTime)
				break;
		
		const int tgt = i+1;
		const int n = rdMin(nagents-i, maxAgents-tgt);
		
		rdAssert(tgt+n <= maxAgents);
		
		if (n > 0)
			memmove(&agents[tgt], &agents[i], sizeof(dtCrowdAgent*)*n);
		slot = i;
	}
	
	agents[slot] = newag;
	
	return rdMin(nagents+1, maxAgents);
}