//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: AI path finding
//
// $NoKeywords: $
//===========================================================================//
#include "ai_pathfinder.h"
#include "ai_movetypes.h"

//-----------------------------------------------------------------------------
void CAI_Pathfinder::CTriDebugOverlay::AddTriOverlayLines( const Vector3D& vecStart, const Vector3D& vecApex,
	const Vector3D& vecEnd, const AIMoveTrace_t &startTrace, const AIMoveTrace_t &endTrace, const bool bPathClear )
{
	static const unsigned char s_triangulationColor[ 2 ][ 3 ] = 
	{
		{ 255,   0, 0 },
		{   0, 255, 0 }
	};

	const unsigned char* const c = s_triangulationColor[ bPathClear ];

	AddTriOverlayLine( vecStart, vecApex, c[ 0 ],c[ 1 ],c[ 2 ], false );
	AddTriOverlayLine( vecApex, vecEnd, c[ 0 ],c[ 1 ],c[ 2 ], false );

	// If we've blocked, draw an X where we were blocked...
	if ( IsMoveBlocked( startTrace.fStatus ) )
	{
		Vector3D pt1, pt2;
		pt1 = pt2 = startTrace.vEndPosition;

		pt1.x -= 10; pt1.y -= 10;
		pt2.x += 10; pt2.y += 10;
		AddTriOverlayLine( pt1, pt2, c[ 0 ],c[ 1 ],c[ 2 ], false );

		pt1.x += 20;
		pt2.x -= 20;
		AddTriOverlayLine( pt1, pt2, c[ 0 ],c[ 1 ],c[ 2 ], false );
	}

	if ( IsMoveBlocked( endTrace.fStatus ) )
	{
		Vector3D pt1, pt2;
		pt1 = pt2 = endTrace.vEndPosition;

		pt1.x -= 10; pt1.y -= 10;
		pt2.x += 10; pt2.y += 10;
		AddTriOverlayLine( pt1, pt2, c[ 0 ],c[ 1 ],c[ 2 ], false );

		pt1.x += 20;
		pt2.x -= 20;
		AddTriOverlayLine( pt1, pt2, c[ 0 ],c[ 1 ],c[ 2 ], false );
	}
}

//-----------------------------------------------------------------------------
void CAI_Pathfinder::CTriDebugOverlay::ClearTriOverlayLines( void )
{
	if ( m_debugTriOverlayLine )
	{
		for ( int i = 0; i < NUM_NPC_DEBUG_OVERLAYS; i++ )
		{
			m_debugTriOverlayLine[ i ]->draw = false;
		}
	}
}

//-----------------------------------------------------------------------------
void CAI_Pathfinder::CTriDebugOverlay::FadeTriOverlayLines( void )
{
	if ( m_debugTriOverlayLine )
	{
		for ( int i = 0; i < NUM_NPC_DEBUG_OVERLAYS; i++ )
		{
			m_debugTriOverlayLine[ i ]->r *= (int)( (double)m_debugTriOverlayLine[ i ]->r * 0.5 );
			m_debugTriOverlayLine[ i ]->g *= (int)( (double)m_debugTriOverlayLine[ i ]->g * 0.5 );
			m_debugTriOverlayLine[ i ]->b *= (int)( (double)m_debugTriOverlayLine[ i ]->b * 0.5 );
		}
	}
}

//-----------------------------------------------------------------------------
void CAI_Pathfinder::CTriDebugOverlay::AddTriOverlayLine( const Vector3D& origin, const Vector3D& dest,
	const int r, const int g, const int b, const bool noDepthTest )
{
	if ( !m_debugTriOverlayLine )
	{
		m_debugTriOverlayLine = new OverlayLine_s* [ NUM_NPC_DEBUG_OVERLAYS ];

		for ( int i = 0; i < NUM_NPC_DEBUG_OVERLAYS; i++ )
		{
			m_debugTriOverlayLine[ i ] = new OverlayLine_s;
		}
	}

	static int s_overlayCounter = 0;

	if ( s_overlayCounter >= NUM_NPC_DEBUG_OVERLAYS )
	{
		s_overlayCounter = 0;
	}

	m_debugTriOverlayLine[ s_overlayCounter ]->origin = origin;
	m_debugTriOverlayLine[ s_overlayCounter ]->dest = dest;
	m_debugTriOverlayLine[ s_overlayCounter ]->r = r;
	m_debugTriOverlayLine[ s_overlayCounter ]->g = g;
	m_debugTriOverlayLine[ s_overlayCounter ]->b = b;
	m_debugTriOverlayLine[ s_overlayCounter ]->noDepthTest = noDepthTest;
	m_debugTriOverlayLine[ s_overlayCounter ]->draw = true;

	s_overlayCounter++;
}
