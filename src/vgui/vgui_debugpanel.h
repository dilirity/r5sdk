#pragma once
#include "core/stdafx.h"
#include "mathlib/color.h"

class CTextOverlay
{
public:
	CTextOverlay()
	{
		m_nFontHeight = 16;
		m_nLineSpacing = 14;
		m_currentFontFace = 0;
		m_updateFontFace = false;
		m_nCon_NPrintf_Idx = 0;
		memset(m_szCon_NPrintf_Buf, '\0', sizeof(m_szCon_NPrintf_Buf));
	}

	void UpdateMiniConsole(void);
	void UpdateInGamePanels(void);

	void AddLog(const eDLL_T context, const char* pszText, const ssize_t textLen);
	void DrawNotify(void);
	void DrawDebugOverlay(void);
	void ShouldDraw(const float flFrameTime);
	void DrawFrameMetrics(void);
	void DrawCrosshairMaterial(void);

	void Con_NPrintf(void);

private:
	u16 GetFontFace();
	Color GetLogColorForType(const eDLL_T type) const;

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

private:
	CUtlVector<TextNotify_s> m_NotifyLines;
	int m_nFontHeight;  // Hardcoded to 16 in this engine.
	int m_nLineSpacing; // Hardcoded to 14 in this engine.

	u16 m_currentFontFace;
	bool m_updateFontFace;

	mutable CThreadFastMutex m_Mutex;

public:
	int m_nCon_NPrintf_Idx;
	char m_szCon_NPrintf_Buf[4096];
};

///////////////////////////////////////////////////////////////////////////////
extern CTextOverlay g_TextOverlay;
