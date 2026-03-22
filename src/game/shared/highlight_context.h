#ifndef HIGHLIGHT_CONTEXT_H
#define HIGHLIGHT_CONTEXT_H

#include "vscript/languages/squirrel_re/include/sqvm.h"

void HighlightContext_LevelShutdown();
void HighlightContext_RegisterDrawFuncEnum(HSQUIRRELVM v);

// Global function natives
SQRESULT Script_HighlightContext_GetId(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetParam(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetParam(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetDrawFunc(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetDrawFunc(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetRadius(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetOutlineRadius(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetInsideFunction(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_GetOutlineFunction(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetFlags(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetNearFadeDistance(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetFarFadeDistance(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_SetFocusedColor(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_IsEntityVisible(HSQUIRRELVM v);
SQRESULT Script_HighlightContext_IsAfterPostProcess(HSQUIRRELVM v);

#endif // HIGHLIGHT_CONTEXT_H
