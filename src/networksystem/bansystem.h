#pragma once
#include "ebisusdk/EbisuTypes.h"

enum EKickType
{
	KICK_NAME = 0,
	KICK_ID,
	BAN_NAME,
	BAN_ID
};

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

		Banned_t(const char* ipAddress = "", NucleusID_t nucleusId = NULL, BanType_e banType = BanType_e::CONNECT, const char* const banExpiry = nullptr)
			: m_Address(ipAddress)
			, m_NucleusID(nucleusId)
			, m_BanType(banType)
			, m_BanExpiry(banExpiry)
		{}

		inline bool operator==(const Banned_t& other) const
		{
			return m_NucleusID == other.m_NucleusID
				&& m_Address.IsEqual_CaseInsensitive(other.m_Address);
		}

		NucleusID_t m_NucleusID;
		CUtlString m_Address;
		BanType_e m_BanType;
		CUtlString m_BanExpiry;
	};

	typedef CUtlVector<Banned_t> BannedList_t;

public:
	void LoadList(void);
	void SaveList(void) const;

	bool AddEntry(const char* ipAddress, const NucleusID_t nucleusId);
	bool DeleteEntry(const char* ipAddress, const NucleusID_t nucleusId);

	bool IsBanned(const char* ipAddress, const NucleusID_t nucleusId) const;
	bool IsBanListValid(void) const;

	void KickPlayerByName(const char* playerName, const char* reason = nullptr);
	void KickPlayerById(const char* playerHandle, const char* reason = nullptr);

	void BanPlayerByName(const char* playerName, const char* reason = nullptr);
	void BanPlayerById(const char* playerHandle, const char* reason = nullptr);

	void UnbanPlayer(const char* criteria);

private:
	void AuthorPlayerByName(const char* playerName, const bool bBan, const char* reason = nullptr);
	void AuthorPlayerById(const char* playerHandle, const bool bBan, const char* reason = nullptr);

	BannedList_t m_BannedList;
};

extern CBanSystem g_BanSystem;
