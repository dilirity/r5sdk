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
// Purpose: Runs the command simulation for fake players.
// Builds a CUserCmd from script input and queues it via ProcessUserCmds
// so that CPlayer::PhysicsSimulate processes it with full time base
// management, animation advancement, and weapon state context — the same
// path real player commands take.
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

			// Bots have no client to drive animations; force the server
			// to run animation updates (StudioFrameAdvance, etc.) instead
			// of waiting for client-side animation replication.
			if (pPlayer->IsClientSideAnimation())
				pPlayer->SetClientSideAnimation(false);

			// Create a proper user command
			CUserCmd cmd;

			cmd.frametime = gpGlobals->frameTime;
			cmd.command_time = gpGlobals->curTime;

			// Set command number — must always increase so ProcessUserCmds
			// doesn't discard the command as a duplicate.
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

			// Build a complete command from script input, just like a real
			// client builds a complete CUserCmd before sending it.
			// NOTE: we do NOT clear hasInput here — StaticRunCommand will
			// also read it (as a safety net) and is responsible for clearing.
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
				}

				// Always merge persistent forced buttons
				cmd.buttons |= g_botInputs[idx].forcedButtons;
			}

			// Auto-acknowledge predicted server events for bots.
			cmd.predicted_server_event_ack = pPlayer->GetPredictableServerEventCount();

			// Queue the command so CPlayer::PhysicsSimulate processes it
			// during v_Physics_RunThinkFunctions with proper time base,
			// animation ticks, and weapon state machine context.
			pPlayer->ProcessUserCmds(&cmd, 1, 1, 0, false);
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
