#ifndef RTECH_RUI_H
#define RTECH_RUI_H

enum class RuiArgumentType_e : u8
{
	TYPE_STRING = 0x1,
	TYPE_ASSET = 0x2,
	TYPE_BOOL = 0x3,
	TYPE_INT = 0x4,
	TYPE_FLOAT = 0x5,
	TYPE_FLOAT2 = 0x6,
	TYPE_FLOAT3 = 0x7,
	TYPE_COLOR_ALPHA = 0x8,
	TYPE_GAMETIME = 0x9,
	TYPE_WALLTIME = 0xA,
	TYPE_UIHANDLE = 0xB,
	TYPE_IMAGE = 0xC,
	TYPE_FONT_FACE = 0xD,
	TYPE_FONT_HASH = 0xE,
	TYPE_ARRAY = 0xF,
};

struct RuiArg_s
{
	RuiArgumentType_e type;
	char unk1;
	i16 valueOffset;
	i16 nameOffset;
	i16 shortHash;
};

struct RuiArgCluster_s
{
	uint16_t argIndex;
	uint16_t argCount;
	char byte1;
	char byte2;
	i16 unk_6;
	i16 unk_8;
	i16 unk_A;
	char unk_C[4];
	i16 unk_10;
};

struct RuiInstance_s
{
	char* pName;
	char* unk1;
	char* unk2;
	float elementWidth;
	float elementHeight;
	float float20;
	float float24;
	_QWORD qword28;
	RuiArgCluster_s* pArgClusters;
	RuiArg_s* pArgs;
	u16 argCount;
	u16 short42;
	_DWORD dword44;
	bool bool48;
	bool hasError;
	u16 short4a;
	u16 short4c;
	u16 argClusterCount;
	void* unk3;
	void* unk4;
	_QWORD qword60;
	void(__fastcall* drawFunc)(__int64(__fastcall**)(), __int64, __int64, __int64);
	void* hiddenFunc;
};

struct RuiFuncs_s
{
	void (*unkFunc1)(RuiInstance_s* const inst);
	void (*unkFunc2)(RuiInstance_s* const inst);

	void (*codeError)(RuiInstance_s* const inst, const char* const errorMsg);
	// todo: reverse the rest.
};

/* ==== RUI ====================================================================================================================================================== */
inline bool(*v_Rui_Draw)(__int64* a1, __m128* a2, const __m128i* a3, __int64 a4, __m128* a5);
inline void*(*v_Rui_LoadAsset)(const char* szRuiAssetName);
inline int16_t(*v_Rui_GetFontFace)(void);

inline RuiFuncs_s* s_ruiApi = nullptr;

///////////////////////////////////////////////////////////////////////////////
class V_Rui : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Rui_Draw", v_Rui_Draw);
		LogFunAdr("Rui_LoadAsset", v_Rui_LoadAsset);
		LogFunAdr("Rui_GetFontFace", v_Rui_GetFontFace);
		LogVarAdr("s_ruiApi", s_ruiApi);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 40 4C 8B 5A 18").GetPtr(v_Rui_Draw);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? EB 03 49 8B C6 48 89 86 ?? ?? ?? ?? 8B 86 ?? ?? ?? ??").FollowNearCallSelf().GetPtr(v_Rui_LoadAsset);
		Module_FindPattern(g_GameDll, "F7 05 ?? ?? ?? ?? ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 74 05 49 8B D1 EB 19 48 8B 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8B 48 58 48 85 C9 48 0F 45 D1 F7 05 ?? ?? ?? ?? ?? ?? ?? ?? 75 19 48 8B 05 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 4C 8B 40 58 4D 85 C0 4D 0F 45 C8 49 8B C9 48 FF 25 ?? ?? ?? ??").GetPtr(v_Rui_GetFontFace);
	}
	virtual void GetVar(void) const
	{
		CMemory(v_Rui_Draw).Offset(0x16B).FindPatternSelf("48 8D").ResolveRelativeAddressSelf(3, 7).GetPtr(s_ruiApi);
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // RTECH_RUI_H
