#ifndef GLOBALNONREWIND_VARS_H
#define GLOBALNONREWIND_VARS_H

#include "vscript/languages/squirrel_re/include/sqvm.h"

SQRESULT Script_SetGlobalNonRewindNetBool(HSQUIRRELVM v);
SQRESULT Script_SetGlobalNonRewindNetInt(HSQUIRRELVM v);
SQRESULT Script_SetGlobalNonRewindNetFloat(HSQUIRRELVM v);
SQRESULT Script_SetGlobalNonRewindNetTime(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetBool(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetInt(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetFloat(HSQUIRRELVM v);
SQRESULT Script_GetGlobalNonRewindNetTime(HSQUIRRELVM v);

void GlobalNonRewind_LevelShutdown();

#endif // GLOBALNONREWIND_VARS_H
