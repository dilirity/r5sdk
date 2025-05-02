#pragma once
#include "squirrel.h"
#include "sqvm.h"

inline SQRESULT(*v_sqstd_format)(HSQUIRRELVM v, SQInteger nformatstringidx, SQBool forceNewline, SQInteger* outlen, SQChar** output);

///////////////////////////////////////////////////////////////////////////////
class VSquirrelStdString : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("sqstd_format", v_sqstd_format);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "4C 89 4C 24 20 44 89 44 24 18 89 54 24 10 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC ?? 48 8B").GetPtr(v_sqstd_format);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { };
};
///////////////////////////////////////////////////////////////////////////////
