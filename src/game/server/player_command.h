//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#ifndef PLAYER_COMMAND_H
#define PLAYER_COMMAND_H

#include "edict.h"
#include "game/shared/usercmd.h"
#include "game/server/player.h"

class IMoveHelper;
class CMoveData;
class CBasePlayer;

//-----------------------------------------------------------------------------
// Purpose: Server side player movement
//-----------------------------------------------------------------------------
class CPlayerMove
{
public:
	//DECLARE_CLASS_NOBASE(CPlayerMove);

	// Construction/destruction
	CPlayerMove(void);
	virtual			~CPlayerMove(void) {}

	// Hook statics:
	static void StaticRunCommand(CPlayerMove* thisp, CPlayer* player, CUserCmd* ucmd, IMoveHelper* moveHelper);

	// Public interfaces:
	// Run a movement command from the player
	virtual void	RunCommand(CPlayer* player, CUserCmd* ucmd, IMoveHelper* moveHelper) = 0;

protected:
	// Prepare for running movement
	virtual void	SetupMove(CPlayer* player, CUserCmd* ucmd, CMoveData* move) = 0;

	// Finish movement
	virtual void	FinishMove(CPlayer* player, CUserCmd* ucmd, CMoveData* move) = 0;

	// Called before and after any movement processing
	virtual void	StartCommand(CPlayer* player, IMoveHelper* pHelper, CUserCmd* cmd) = 0;
};

//-----------------------------------------------------------------------------
// Purpose: stores script-provided input for bot players.
// Written by VScript SetBotInput, consumed by Physics_RunBotSimulation.
//-----------------------------------------------------------------------------
struct BotInput
{
	QAngle viewAngles;
	float forwardMove;
	float sideMove;
	float upMove;
	int buttons;
	bool hasInput;  // true = script provided input this frame
	int forcedButtons; // persistent buttons from BotButtonPress/BotButtonRelease

	// Persistent movement input - used when hasInput is false
	// This ensures continuous input across frames (critical for wall climbing)
	bool hasPersistentInput;
	QAngle persistentViewAngles;
	float persistentForwardMove;
	float persistentSideMove;

	void Reset()
	{
		viewAngles.Init();
		forwardMove = 0.f;
		sideMove = 0.f;
		upMove = 0.f;
		buttons = 0;
		hasInput = false;
		// NOTE: forcedButtons and persistent input intentionally NOT reset here —
		// they persist until explicitly changed.
	}

	void ClearPersistentInput()
	{
		hasPersistentInput = false;
		persistentViewAngles.Init();
		persistentForwardMove = 0.f;
		persistentSideMove = 0.f;
	}
};

extern BotInput g_botInputs[MAX_PLAYERS];

inline void (*CPlayerMove__RunCommand)(CPlayerMove* thisp, CPlayer* player, CUserCmd* ucmd, IMoveHelper* moveHelper);

///////////////////////////////////////////////////////////////////////////////
class VPlayerMove : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CPlayerMove::RunCommand", CPlayerMove__RunCommand);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 8B C4 55 53 56 57 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 44 0F 29 50 ??").GetPtr(CPlayerMove__RunCommand);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // PLAYER_COMMAND_H
