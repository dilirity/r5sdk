//====== Copyright � 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: Physics simulation for non-havok/ipion objects
//
// $NoKeywords: $
//=============================================================================//
#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "player.h"
#include "physics_main.h"
#include "player_command.h"
#include "movehelper_server.h"
#include "engine/server/server.h"
#include "engine/client/client.h"
#include "game/server/util_server.h"

static ConVar sv_simulateBots("sv_simulateBots", "1", FCVAR_RELEASE, "Simulate user commands for bots on the server.");

//-----------------------------------------------------------------------------
// Purpose: Runs the command simulation for fake players
//-----------------------------------------------------------------------------
void Physics_RunBotSimulation(bool bSimulating)
{
	if (!sv_simulateBots.GetBool())
		return;

	for (int i = 0; i < g_ServerGlobalVariables->maxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient->IsActive() && pClient->IsFakeClient())
		{
			CPlayer* const pPlayer = UTIL_PlayerByIndex(pClient->GetHandle());
			if (!pPlayer)
				continue;

			// Skip dead bots
			if (pPlayer->GetLifeState() != 0)
				continue;

			// Create a proper user command
			CUserCmd cmd;

			float flOldFrameTime = gpGlobals->frameTime;
			float flOldCurTime = gpGlobals->curTime;

			cmd.frametime = flOldFrameTime;
			cmd.command_time = flOldCurTime;

			// Set command number
			static int s_botCommandNumber = 1;
			cmd.command_number = s_botCommandNumber++;
			cmd.tick_count = gpGlobals->tickCount;

			// Get eye position for wall climb detection
			const Vector3D eyePos = pPlayer->GetAbsOrigin() + pPlayer->GetViewOffset();

			// Set head/camera positions - CRITICAL for wall climb detection
			cmd.headposition = eyePos;
			cmd.camerapos = eyePos;

			// Get current eye angles
			pPlayer->EyeAngles(&cmd.viewangles);

			// Apply script-provided bot input if available
			const int idx = pPlayer->GetEdict() - 1;
			if (idx >= 0 && idx < MAX_PLAYERS)
			{
				if (g_botInputs[idx].hasInput)
				{
					cmd.viewangles = g_botInputs[idx].viewAngles;
					cmd.forwardmove = g_botInputs[idx].forwardMove;
					cmd.sidemove = g_botInputs[idx].sideMove;
					cmd.upmove = g_botInputs[idx].upMove;
					cmd.buttons = g_botInputs[idx].buttons;

					// Save as persistent input for subsequent frames
					g_botInputs[idx].hasPersistentInput = true;
					g_botInputs[idx].persistentViewAngles = g_botInputs[idx].viewAngles;
					g_botInputs[idx].persistentForwardMove = g_botInputs[idx].forwardMove;
					g_botInputs[idx].persistentSideMove = g_botInputs[idx].sideMove;

					g_botInputs[idx].hasInput = false;
				}
				else if (g_botInputs[idx].hasPersistentInput)
				{
					// Use persistent input when no new input provided
					// This ensures continuous movement for wall climbing etc.
					cmd.viewangles = g_botInputs[idx].persistentViewAngles;
					cmd.forwardmove = g_botInputs[idx].persistentForwardMove;
					cmd.sidemove = g_botInputs[idx].persistentSideMove;
					cmd.buttons = g_botInputs[idx].forcedButtons; // Use forced buttons as base
				}

				// Always merge persistent forced buttons (from BotButtonPress)
				cmd.buttons |= g_botInputs[idx].forcedButtons;
			}

			// Execute the command
			pPlayer->SetTimeBase(gpGlobals->curTime);
			MoveHelperServer()->SetHost(pPlayer);

			pPlayer->PlayerRunCommand(&cmd, MoveHelperServer());

			pPlayer->SetLastUserCommand(&cmd);

			gpGlobals->frameTime = flOldFrameTime;
			gpGlobals->curTime = flOldCurTime;

			MoveHelperServer()->SetHost(NULL);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Runs the main physics simulation loop against all entities ( except players )
//-----------------------------------------------------------------------------
void Physics_RunThinkFunctions(bool bSimulating)
{
	Physics_RunBotSimulation(bSimulating);
	v_Physics_RunThinkFunctions(bSimulating);
}

///////////////////////////////////////////////////////////////////////////////
void VPhysics_Main::Detour(const bool bAttach) const
{
	DetourSetup(&v_Physics_RunThinkFunctions, &Physics_RunThinkFunctions, bAttach);
}
