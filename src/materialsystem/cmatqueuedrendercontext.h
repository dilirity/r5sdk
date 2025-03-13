#ifndef MATQUEUEDRENDERCONTEXT_H
#define MATQUEUEDRENDERCONTEXT_H

struct CallQueue_s
{
	int queueEndIndex;
	char* queueStart;
	int currentAllocIndex;
	int currentCallIndex;

	inline void* GetCurrentAllocatedItem()
	{
		return queueStart + currentAllocIndex;
	}

	inline void* GetCurrentCallItem()
	{
		return queueStart + currentCallIndex;
	}
};

inline int(**g_fnHasRenderCallQueue)(void);
inline CallQueue_s* (**g_fnAddRenderCallQueueItem)(void* method, const size_t structSize, const size_t unknown);
inline int(**g_fnAdvanceRenderCallQueue)(const size_t structSize);

class VMatQueuedRenderContext : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogVarAdr("g_fnHasRenderCallQueue", g_fnHasRenderCallQueue);
		LogVarAdr("g_fnAddRenderCallQueueItem", g_fnAddRenderCallQueueItem);
		LogVarAdr("g_fnAdvanceRenderCallQueue", g_fnAdvanceRenderCallQueue);
	}
	virtual void GetFun(void) const { }
	virtual void GetVar(void) const
	{
		const CMemory tempBase = Module_FindPattern(g_GameDll, "48 83 EC ? FF 15 ? ? ? ? 85 C0 74 ? BA ? ? ? ? 48 8D 0D ? ? ? ? 44 8D 42 ? FF 15 ? ? ? ? B9 ? ? ? ? 8B 50");

		tempBase.FindPattern("FF 15").ResolveRelativeAddressSelf(2, 6).GetPtr(g_fnHasRenderCallQueue);
		tempBase.Offset(0x10).FindPattern("FF 15").ResolveRelativeAddressSelf(2, 6).GetPtr(g_fnAddRenderCallQueueItem);
		tempBase.Offset(0x40).FindPattern("48 FF").ResolveRelativeAddressSelf(3, 7).GetPtr(g_fnAdvanceRenderCallQueue);
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { };
};

#endif // MATQUEUEDRENDERCONTEXT_H
