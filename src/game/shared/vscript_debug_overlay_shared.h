#ifndef VSCRIPT_DEBUG_OVERLAY_SHARED_H
#define VSCRIPT_DEBUG_OVERLAY_SHARED_H

extern SQRESULT SharedScript_DebugDrawSolidBox(HSQUIRRELVM v);
extern SQRESULT SharedScript_DebugDrawSweptBox(HSQUIRRELVM v);
extern SQRESULT SharedScript_DebugDrawTriangle(HSQUIRRELVM v);
extern SQRESULT SharedScript_DebugDrawSolidSphere(HSQUIRRELVM v);
extern SQRESULT SharedScript_DebugDrawCapsule(HSQUIRRELVM v);
extern SQRESULT SharedScript_DebugDrawText(HSQUIRRELVM v);
extern SQRESULT SharedScript_CreateBox(HSQUIRRELVM v);
extern SQRESULT SharedScript_ClearBoxes(HSQUIRRELVM v);

#endif // VSCRIPT_DEBUG_OVERLAY_SHARED_H
