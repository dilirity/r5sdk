//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "engine/server/server.h"
#include "engine/client/client.h"

#include "player_command.h"

BotInput g_botInputs[MAX_PLAYERS] = {};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CPlayerMove::CPlayerMove(void)
{
}

//-----------------------------------------------------------------------------
// Purpose: Runs movement commands for the player
// Input  : *player - 
//			*ucmd - 
//			*moveHelper - 
//-----------------------------------------------------------------------------
void CPlayerMove::StaticRunCommand(CPlayerMove* thisp, CPlayer* player, CUserCmd* ucmd, IMoveHelper* moveHelper)
{
	CClientExtended* const cle = g_pServer->GetClientExtended(player->GetEdict() - 1);
	float playerFrameTime;
	
	// Always default to clamped UserCmd frame time if this cvar is set
	if (player_disallow_negative_frametime->GetBool())
		playerFrameTime = fmaxf(ucmd->frametime, 0.0f);
	else
	{
		if (player->m_bGamePaused)
			playerFrameTime = 0.0f;
		else
			playerFrameTime = TICK_INTERVAL;

		if (ucmd->frametime)
			playerFrameTime = ucmd->frametime;
	}

	if (sv_clampPlayerFrameTime->GetBool() && player->m_joinFrameTime > ((*g_pflServerFrameTimeBase) + playerframetimekick_margin->GetFloat()))
		playerFrameTime = 0.0f;

	const float timeAllowedForProcessing = cle->ConsumeMovementTimeForUserCmdProcessing(playerFrameTime);

	if (!player->IsBot() && (timeAllowedForProcessing < playerFrameTime))
		return; // Don't process this command

	// Inject script-provided bot input into the command.
	// Primary injection happens in Physics_RunBotSimulation (before queuing),
	// but RunCommand may be called multiple times per frame — this ensures
	// every call has correct input, and clears hasInput when consumed.
	if (player->IsBot())
	{
		const int idx = player->GetEdict() - 1;

		if (idx >= 0 && idx < MAX_PLAYERS)
		{
			if (g_botInputs[idx].hasInput)
			{
				ucmd->viewangles = g_botInputs[idx].viewAngles;
				ucmd->forwardmove = g_botInputs[idx].forwardMove;
				ucmd->sidemove = g_botInputs[idx].sideMove;
				ucmd->buttons = g_botInputs[idx].buttons;

				// Save as persistent input for subsequent RunCommand calls
				// (RunNullCommand triggers a second RunCommand with empty input)
				g_botInputs[idx].hasPersistentInput = true;
				g_botInputs[idx].persistentViewAngles = g_botInputs[idx].viewAngles;
				g_botInputs[idx].persistentForwardMove = g_botInputs[idx].forwardMove;
				g_botInputs[idx].persistentSideMove = g_botInputs[idx].sideMove;
				g_botInputs[idx].persistentButtons = g_botInputs[idx].buttons;

				g_botInputs[idx].hasInput = false;
			}
			else if (g_botInputs[idx].hasPersistentInput)
			{
				// No new input — use last known input to maintain
				// continuous state (critical for wall climbing, holding USE, etc.)
				ucmd->viewangles = g_botInputs[idx].persistentViewAngles;
				ucmd->forwardmove = g_botInputs[idx].persistentForwardMove;
				ucmd->sidemove = g_botInputs[idx].persistentSideMove;
				ucmd->buttons = g_botInputs[idx].persistentButtons;
			}

			// Always merge persistent forced buttons
			ucmd->buttons |= g_botInputs[idx].forcedButtons;
		}
	}

	CPlayerMove__RunCommand(thisp, player, ucmd, moveHelper);
}

void VPlayerMove::Detour(const bool bAttach) const
{
	DetourSetup(&CPlayerMove__RunCommand, &CPlayerMove::StaticRunCommand, bAttach);
}
