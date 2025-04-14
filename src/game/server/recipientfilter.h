//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef RECIPIENTFILTER_H
#define RECIPIENTFILTER_H
#include "player.h"
#include "irecipientfilter.h"

inline void(*CRecipientFilter__AddRecipient)(CRecipientFilter* thisp, const CPlayer* pPlayer);
inline void(*CRecipientFilter__RemoveRecipient)(CRecipientFilter* thisp, const CPlayer* pPlayer);

//-----------------------------------------------------------------------------
// Purpose: A generic filter for determining whom to send message/sounds etc. to and
//  providing a bit of additional state information
//-----------------------------------------------------------------------------
class CRecipientFilter : public IRecipientFilter
{
	struct Recipient_s
	{
		int m_nIndex;
		bool m_bIsLocalPlayer;
	};

	static_assert( sizeof( Recipient_s ) == 0x8 );

public:
						CRecipientFilter();
	virtual			~CRecipientFilter();

	virtual bool	IsReliable( void ) const;
	virtual void	MakeReliable( void );

	virtual bool	IsInitMessage( void ) const;

	virtual int		GetRecipientCount( void ) const;
	virtual int		GetRecipientIndex( int nSlot ) const;

	virtual bool	IsLocalPlayer( int nSlot ) const;
	virtual int		DistTo( int nSlot, const Vector3D& pos ) const;

	void			    Reset( void );
	int				FindSlotForIndex( int nIndex ) const;

	FORCEINLINE void	AddRecipient( const CPlayer* pPlayer )
	{
		CRecipientFilter__AddRecipient(this, pPlayer);
	}

	FORCEINLINE void	RemoveRecipient( const CPlayer* pPlayer )
	{
		CRecipientFilter__RemoveRecipient(this, pPlayer);
	}

private:
	bool m_bReliable;
	bool m_bInitMessage;
	CUtlVector<Recipient_s> m_Recipients;
	bool m_bUsingPredictionRules;
	bool m_bIgnorePredictionCull;
};

static_assert(sizeof(CRecipientFilter) == 0x38);

class CSingleUserRecipientFilter :  public CRecipientFilter
{
public:
	CSingleUserRecipientFilter(const CPlayer* pPlayer)
	{
		AddRecipient(pPlayer);
	}
};

class VRecipientFilter : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CRecipientFilter::AddRecipient", CRecipientFilter__AddRecipient);
		LogFunAdr("CRecipientFilter::RemoveRecipient", CRecipientFilter__RemoveRecipient);
	}
	virtual void GetFun(void) const 
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F BF 42 ? 33 F6").GetPtr(CRecipientFilter__AddRecipient);
		Module_FindPattern(g_GameDll, "44 0F BF 42 ? 33 C0").GetPtr(CRecipientFilter__RemoveRecipient);
	};
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { };
};

#endif