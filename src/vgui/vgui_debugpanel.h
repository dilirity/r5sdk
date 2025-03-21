#pragma once
#include "core/stdafx.h"
#include "mathlib/color.h"

class CTextOverlay
{
private:
	struct TextNotify_s
	{
		void Init(const eDLL_T type, const float flTime, const char* pszText, const ssize_t textLen)
		{
			this->m_Type = type;
			this->m_flLifeRemaining = flTime;
			this->m_Text.SetDirect(pszText, textLen);
		}

		eDLL_T m_Type;
		float m_flLifeRemaining;
		CUtlString m_Text;
	};

public:
	CTextOverlay()
	{
		m_nFontHeight = 16;
		m_nLineSpacing = 15;
		m_nCon_NPrintf_Idx = 0;
		memset(m_szCon_NPrintf_Buf, '\0', sizeof(m_szCon_NPrintf_Buf));
	}

	void Update(void);
	void AddLog(const eDLL_T context, const char* pszText, const ssize_t textLen);
	void DrawNotify(void);
	void DrawDebugOverlay(void) const;
	void DrawFormat(const int x, const int y, const Color c, const char* pszFormat, ...) const;
	void ShouldDraw(const float flFrameTime);
	void DrawSimStats(void) const;
	void DrawGPUStats(void) const;
	void DrawCrosshairMaterial(void) const;

	void Con_NPrintf(void);

private:
	Color GetLogColorForType(const eDLL_T type) const;
	CUtlVector<TextNotify_s> m_NotifyLines;
	int m_nFontHeight; // Hardcoded to 16 in this engine.
	int m_nLineSpacing;

	mutable CThreadFastMutex m_Mutex;

public:
	int m_nCon_NPrintf_Idx;
	char m_szCon_NPrintf_Buf[4096];
};

///////////////////////////////////////////////////////////////////////////////
extern CTextOverlay g_TextOverlay;
