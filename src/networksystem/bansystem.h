#pragma once
#include "tier1/NetAdr.h"
#include "tier2/netadrutils.h"
#include "ebisusdk/EbisuTypes.h"

class CBanSystem
{
public:
	struct Banned_t
	{
		enum BanType_e
		{
			CONNECT = 0,
			COMMUNICATION
		};

	Banned_t(const char* ipAddress = "", SteamID_t steamId = NULL, BanType_e banType = BanType_e::CONNECT, const char* const banExpiry = nullptr)
		: m_Address(ipAddress)
		, m_SteamID(steamId)
		, m_BanType(banType)
		, m_BanExpiry(banExpiry)
		{}

		inline bool operator==(const Banned_t& other) const
		{
			return m_SteamID == other.m_SteamID
				&& m_Address.IsEqual_CaseInsensitive(other.m_Address);
		}

		SteamID_t m_SteamID;
		CUtlString m_Address;
		BanType_e m_BanType;
		CUtlString m_BanExpiry;
	};

	typedef CUtlVector<Banned_t> BannedList_t;

public:
	void LoadList(void);
	void SaveList(void) const;

	void Clear();

	bool AddEntry(const netadr_t* const adr, const SteamID_t steamId);
	bool DeleteEntry(const netadr_t* const adr, const SteamID_t steamId);

	bool IsBanned(const netadr_t* const adr, const SteamID_t steamId) const;

	void KickPlayerByName(const char* playerName, const char* reason = nullptr);
	void KickPlayerById(const char* playerHandle, const char* reason = nullptr);

	void BanPlayerByName(const char* playerName, const char* reason = nullptr);
	void BanPlayerById(const char* playerHandle, const char* reason = nullptr);

	void UnbanPlayer(const char* criteria);

private:
	bool AddEntry(const in6_addr* const adr, const SteamID_t steamId);
	bool DeleteEntry(const in6_addr* const adr, const SteamID_t steamId);

	bool IsBanned(const in6_addr* const adr, const SteamID_t steamId) const;

	void AuthorPlayerByName(const char* playerName, const bool bBan, const char* reason = nullptr);
	void AuthorPlayerById(const char* playerHandle, const bool bBan, const char* reason = nullptr);

	std::unordered_set<SteamID_t> m_bannedIdList;
	std::unordered_set<IPv6Wrapper_s, IPv6Hasher_s> m_bannedIpList;
};

extern CBanSystem g_BanSystem;
