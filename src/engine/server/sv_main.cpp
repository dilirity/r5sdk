//===========================================================================//
//
// Purpose:
//
//===========================================================================//
#include "core/stdafx.h"
#include "tier0/threadtools.h"
#include "tier0/frametask.h"
#include "tier1/cvar.h"
#include "engine/server/sv_main.h"
#include "engine/client/client.h"
#include "networksystem/pylon.h"
#include "networksystem/bansystem.h"
#include "engine/client/client.h"
#include "server.h"
#include "game/server/gameinterface.h"
#include "game/server/util_server.h"
#include "tier2/cryptutils.h"
#include "thirdparty/mbedtls/include/mbedtls/sha256.h"

static string SV_HashPasswordTag(const char* const pszPassword)
{
	if (!pszPassword || !pszPassword[0])
		return string();

	char ctx[128] = {};
	const char* ip = hostip->GetString();
	int port = hostport->GetInt();
	Q_snprintf(ctx, sizeof(ctx), "%s:%d", (ip && ip[0]) ? ip : "0.0.0.0", port);

	string material;
	material.reserve(strlen(pszPassword) + strlen("R5SDK_PW_PPR_v1") + strlen(ctx) + 2);
	material.append(pszPassword);
	material.push_back('|');
	material.append("R5SDK_PW_PPR_v1");
	material.push_back('|');
	material.append(ctx);

	uint8_t digest[32] = {};
	if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(material.data()), (size_t)material.size(), digest, 0) != 0)
	{
		return string();
	}

	uint64_t tag64 = 0;
	memcpy(&tag64, digest, sizeof(tag64));
	return Format("pw:%016llx", (unsigned long long)tag64);
}

static void SV_ServerPasswordChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData)
{
	ConVar* const pPassword = g_pCVar->FindVar(pConVar->GetName());
	ConVar* const pFilter   = g_pCVar->FindVar("serverFilter");
	if (!pPassword || !pFilter)
		return;

	const char* const newPw = pPassword->GetString();

	if (pOldString && strcmp(pOldString, newPw) == 0)
		return;

	const string tagged = SV_HashPasswordTag(newPw);
	pFilter->SetValue(tagged.c_str());
}

static ConVar sv_password("sv_password", "", FCVAR_RELEASE, "Server password for entry.", false, 0.f, false, 0.f, &SV_ServerPasswordChanged_f, nullptr);
static ConVar sv_applyGlobalCommsBans("sv_applyGlobalCommsBans", "3", FCVAR_RELEASE, "Determines whether or not to use the global chat ban list, 0 = None, 1 = Text, 2 = Voice, 3 = Both.", false, 0.f, true, 3.f);
static ConVar sv_commsBansAreGameBans("sv_commsBansAreGameBans", "0", FCVAR_RELEASE, "If set chat bans will be applied as game bans", false, 0.f, true, 1.f);

bool SV_ShouldApplyTextChatGlobalMutes()
{
	const int globalBanSetting = sv_applyGlobalCommsBans.GetInt();
	if (globalBanSetting == 1 || globalBanSetting == 3)
		return true;
	return false;
}

bool SV_ShouldApplyVoiceChatGlobalMutes()
{
	const int globalBanSetting = sv_applyGlobalCommsBans.GetInt();
	if (globalBanSetting >= 2)
		return true;
	return false;
}

static bool SV_GlobalCommsBansEnabled()
{
	if (sv_applyGlobalCommsBans.GetInt() != 0)
		return true;
	return false;
}

static void SV_HandleConnectBan(CClient* const pClient, const char* const pszReason, const char* const pszIpStr, const int nPort, const NucleusID_t nNucleusID)
{
	pClient->Disconnect(Reputation_t::REP_MARK_BAD, "%s", pszReason);
	Warning(eDLL_T::SERVER, "Removed client '[%s]:%i' from slot #%i ('%llu' is banned globally!)\n",
		pszIpStr, nPort, pClient->GetUserID(), nNucleusID);
}

static void SV_HandleCommunicationBan(CClient* const pClient, const char* const pszReason, const char* const pszExpiry, const char* const pszIpStr, const int nPort, const NucleusID_t nNucleusID)
{
	const int nUserId = pClient->GetUserID();
	CClientExtended* const pClientExtended = pClient->GetClientExtended();

	if (sv_commsBansAreGameBans.GetBool())
	{
		pClient->Disconnect(Reputation_t::REP_MARK_BAD, "%s", pszReason);
		Warning(eDLL_T::SERVER, "Removed client '[%s]:%i' from slot #%i ('%llu' is communication banned and communication bans are treated as game bans!)\n",
			pszIpStr, nPort, nUserId, nNucleusID);
	}
	else
	{
		DevMsg(eDLL_T::SERVER, "Muting client '[%s]:%i' from slot #%i ('%llu' is communication banned!)\n",
			pszIpStr, nPort, nUserId, nNucleusID);
	}

	pClientExtended->SetClientIsCommsBanned(true);
	pClientExtended->SetCommsBanInfo(pszReason, pszExpiry);
}

//-----------------------------------------------------------------------------
// Purpose: checks if particular client is banned on the comp server
//-----------------------------------------------------------------------------
void SV_CheckForBanAndDisconnect(CClient* const pClient, const string& svIPAddr,
	const NucleusID_t nNucleusID, const string& svPersonaName, const int nPort)
{
	Assert(pClient != nullptr);

	string svError;
	string expiry;
	CBanSystem::Banned_t::BanType_e banType = CBanSystem::Banned_t::CONNECT;
	
	const bool bCompBanned = g_MasterServer.CheckForBan(svIPAddr, nNucleusID, svPersonaName, svError, banType, expiry);

	if (bCompBanned)
	{
		g_TaskQueue.Dispatch([pClient, svError, svIPAddr, nNucleusID, nPort, banType, expiry]
			{
				// Make sure client isn't already disconnected,
				// and that if there is a valid netchannel, that
				// it hasn't been taken by a different client by
				// the time this task is getting executed.
				const CNetChan* const pChan = pClient->GetNetChan();
				if (pChan && pClient->GetNucleusID() == nNucleusID)
				{
					switch (banType)
					{
					case CBanSystem::Banned_t::CONNECT:
					{
						SV_HandleConnectBan(pClient, svError.c_str(), svIPAddr.c_str(), nPort, nNucleusID);
						break;
					}
					case CBanSystem::Banned_t::COMMUNICATION:
					{
						if(SV_GlobalCommsBansEnabled())
							SV_HandleCommunicationBan(pClient, svError.c_str(), expiry.c_str(), svIPAddr.c_str(), nPort, nNucleusID);
						break;
					default:
						break;
					}
					}
				}
			}, 0);
	}
}

//-----------------------------------------------------------------------------
// Purpose: checks if particular client is banned on the master server
//-----------------------------------------------------------------------------
void SV_ProcessBulkCheck(const CBanSystem::BannedList_t* const pBannedVec)
{
	CBanSystem::BannedList_t* outBannedVec = nullptr;

	if (!g_MasterServer.GetBannedList(*pBannedVec, &outBannedVec))
		return;

	g_TaskQueue.Dispatch([outBannedVec]
		{
			SV_CheckClientsForBan(outBannedVec);
			delete outBannedVec;
		}, 0);
}

//-----------------------------------------------------------------------------
// Purpose: creates a snapshot of the currently connected clients
// Input  : *pBannedVec - if passed, will check for bans and kick the clients
//-----------------------------------------------------------------------------
void SV_CheckClientsForBan(const CBanSystem::BannedList_t* const pBannedVec /*= nullptr*/)
{
	Assert(ThreadInMainThread());

	CBanSystem::BannedList_t* bannedVec = !pBannedVec 
		? new CBanSystem::BannedList_t 
		: nullptr;

	for (int c = 0; c < gpGlobals->maxClients; c++) // Loop through all possible client instances.
	{
		CClient* const pClient = g_pServer->GetClient(c);
		const CNetChan* const pNetChan = pClient->GetNetChan();

		if (!pNetChan)
			continue;

		if (!pClient->IsConnected())
			continue;

		if (pNetChan->GetRemoteAddress().IsLoopback())
			continue;

		CPlayer* const pPlayer = UTIL_PlayerByIndex(pClient->GetHandle());

		// Bots shouldn't be checked for bans because these are added by the
		// server host.
		if (pPlayer && pPlayer->IsBot())
			continue;

		const char* const szIPAddr = pNetChan->GetAddress(true);
		const NucleusID_t nNucleusID = pClient->GetNucleusID();

		// If no banned list was provided, build one with all clients
		// on the server. This will be used for bulk checking so live
		// bans could be performed, as this function is called periodically.
		if (bannedVec)
			bannedVec->AddToTail(CBanSystem::Banned_t(szIPAddr, nNucleusID));
		else
		{
			// Check if current client is within provided banned list, and
			// prune if so...
			FOR_EACH_VEC(*pBannedVec, i)
			{
				const CBanSystem::Banned_t& banned = (*pBannedVec)[i];

				//If this ban isnt for this client then we check the next
				if (banned.m_NucleusID != pClient->GetNucleusID())
					continue;

				const int nPort = pNetChan->GetPort();

				//What ban type do we have for this client
				switch (banned.m_BanType)
				{
				case CBanSystem::Banned_t::CONNECT:
				{
					SV_HandleConnectBan(pClient, banned.m_Address.String(), szIPAddr, nPort, nNucleusID);
					break;
				}
				case CBanSystem::Banned_t::COMMUNICATION:
				{
					//Does the host have the comms ban system on and is our client already banned, no point rebanning them if they are
					if (SV_GlobalCommsBansEnabled() && (!pClient->GetClientExtended()->IsClientCommsBanned() || sv_commsBansAreGameBans.GetBool()))
						SV_HandleCommunicationBan(pClient, banned.m_Address.String(), banned.m_BanExpiry.Get(), szIPAddr, nPort, nNucleusID);
					break;
				}
				//Unknown ban type
				default:
					break;
				}

				//Since we have handled this client we can move onto the next one
				break;
			}
		}
	}

	if (bannedVec && !bannedVec->IsEmpty())
	{
		std::thread bulkCheck([bannedVec]()
			{
				SV_ProcessBulkCheck(bannedVec);
				delete bannedVec;
			});

		bulkCheck.detach();
	}
	else if (bannedVec)
	{
		delete bannedVec;
		bannedVec = nullptr;
	}
}

//-----------------------------------------------------------------------------
// Purpose: loads the game .dll
//-----------------------------------------------------------------------------
void SV_InitGameDLL()
{
	// Mirror current password into the server filter used by the engine's connect validation (hashed tag).
	if (ConVar* const pFilter = g_pCVar->FindVar("serverFilter"))
	{
		const string tagged = SV_HashPasswordTag(sv_password.GetString());
		pFilter->SetValue(tagged.c_str());
	}
	v_SV_InitGameDLL();
}

//-----------------------------------------------------------------------------
// Purpose: release resources associated with extension DLLs.
//-----------------------------------------------------------------------------
void SV_ShutdownGameDLL()
{
	v_SV_ShutdownGameDLL();
}

//-----------------------------------------------------------------------------
// Purpose: activates the server
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool SV_ActivateServer()
{
	return v_SV_ActivateServer();
}

//-----------------------------------------------------------------------------
// Purpose: returns whether voice data can be broadcasted from the server
//-----------------------------------------------------------------------------
bool SV_CanBroadcastVoice()
{
	if (IsPartyDedi())
		return false;

	if (IsTrainingDedi())
		return false;

	if (!sv_voiceenable->GetBool())
		return false;

	if (gpGlobals->maxClients <= 0)
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: relays voice data to other clients
//-----------------------------------------------------------------------------
void SV_BroadcastVoiceData(CClient* const cl, const int nBytes, char* const data)
{
	if (!SV_CanBroadcastVoice())
		return;

	const bool bShouldApplyGlobalMutes = SV_ShouldApplyVoiceChatGlobalMutes();
	const bool bBannedClientsCanHearOtherClients = sv_commsBannedClientsCanReceiveComms.GetBool();

	SVC_VoiceData voiceData(cl->GetUserID(), nBytes, data);

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		CClient* const pClient = g_pServer->GetClient(i);

		// is this client fully connected
		if (pClient->GetSignonState() != SIGNONSTATE::SIGNONSTATE_FULL)
			continue;

		//If the client is communication banned and the server has decidecd that players who are comms banned cant hear other players, skip broadcasting to them
		if (!bBannedClientsCanHearOtherClients && bShouldApplyGlobalMutes &&  pClient->GetClientExtended()->IsClientCommsBanned())
			continue;

		// is this client the sender
		if (pClient == cl && !sv_voiceEcho->GetBool())
			continue;

		// is this client on the sender's team
		if (pClient->GetTeamNum() != cl->GetTeamNum() && !sv_alltalk->GetBool())
			continue;

		//if (voice_noxplat->GetBool() && cl->GetXPlatID() != pClient->GetXPlatID())
		//{
		//	if ((cl->GetXPlatID() -1) > 1 || (pClient->GetXPlatID() -1) > 1)
		//		continue;
		//}

		CNetChan* const pNetChan = pClient->GetNetChan();

		if (!pNetChan)
			continue;

		// if voice stream has enough space for new data
		if (pNetChan->GetStreamVoice().GetNumBitsLeft() >= 8 * nBytes + 96)
			pClient->SendNetMsgEx(&voiceData, false, false, true);
	}
}

//-----------------------------------------------------------------------------
// Purpose: relays durango voice data to other clients
//-----------------------------------------------------------------------------
void SV_BroadcastDurangoVoiceData(CClient* const cl, const int nBytes, char* const data,
	const int nXid, const int unknown, const bool useVoiceStream, const bool skipXidCheck)
{
	if (!SV_CanBroadcastVoice())
		return;

	const bool bShouldApplyGlobalMutes = SV_ShouldApplyVoiceChatGlobalMutes();
	const bool bBannedClientsCanHearOtherClients = sv_commsBannedClientsCanReceiveComms.GetBool();

	SVC_DurangoVoiceData voiceData(cl->GetUserID(), nBytes, data, unknown, useVoiceStream);

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		CClient* const pClient = g_pServer->GetClient(i);

		// is this client fully connected
		if (pClient->GetSignonState() != SIGNONSTATE::SIGNONSTATE_FULL)
			continue;

		//If the client is communication banned and the server has decidecd that players who are comms banned cant other players, skip broadcasting to them
		if (!bBannedClientsCanHearOtherClients && bShouldApplyGlobalMutes && pClient->GetClientExtended()->IsClientCommsBanned())
			continue;

		// is this client the sender
		if (pClient == cl && !sv_voiceEcho->GetBool())
			continue;

		if (!skipXidCheck && i != nXid)
			continue;

		// is this client on the sender's team
		if (pClient->GetTeamNum() != cl->GetTeamNum() && !sv_alltalk->GetBool())
		{
			// NOTE: on Durango packets, the game appears to bypass the team
			// check if 'useVoiceStream' is false, thus forcing the usage
			// of the reliable stream. Omitted the check as it appears that
			// could be exploited to transmit voice to other teams while cvar
			// 'sv_alltalk' is unset.
			continue;
		}

		// NOTE: xplat code checks disabled; CClient::GetXPlatID() seems to be
		// an enumeration of platforms, but the enum hasn't been reversed yet.
		//if (voice_noxplat->GetBool() && cl->GetXPlatID() != pClient->GetXPlatID())
		//{
		//	if ((cl->GetXPlatID() - 1) > 1 || (pClient->GetXPlatID() - 1) > 1)
		//		continue;
		//}

		CNetChan* const pNetChan = pClient->GetNetChan();

		if (!pNetChan)
			continue;

		// NOTE: the game appears to have the ability to use the unreliable
		// stream as well, but the condition to hit that code path can never
		// evaluate to true - appears to be a compile time option that hasn't
		// been fully optimized away? For now only switch between voice and
		// reliable streams as that is what the original code does.
		const bf_write& stream = useVoiceStream ? pNetChan->GetStreamVoice() : pNetChan->GetStreamReliable();

		// if stream has enough space for new data
		if (stream.GetNumBitsLeft() >= 8 * nBytes + 34)
			pClient->SendNetMsgEx(&voiceData, false, !useVoiceStream, useVoiceStream);
	}
}
