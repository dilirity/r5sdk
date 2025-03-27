#pragma once
#include "imgui_surface.h"

class CParticleOverlay : public CImguiSurface
{
public:
	CParticleOverlay(void);
	~CParticleOverlay(void);

	virtual bool Init(void);
	virtual void Shutdown(void);

	virtual void RunFrame(void);
	virtual bool DrawSurface(void);

	void UpdateWindowAvailability(void);

	bool ResizeScratchBuffer(const size_t newSize);
	void FreeScratchBuffer(void);

	void Begin();
	void End();

	void AppendText(const char* const text, const size_t textLen);

	inline bool IsFrozen() const { return m_freezeCapture; }

private:
	char* m_scratchBuffer;
	size_t m_scratchBufferSize;
	size_t m_bufferCursor;

	bool m_freezeCapture;
	bool m_lastAvailability;
};

extern CParticleOverlay g_particleOverlay;
