//=============================================================================//
//
// Purpose: Expose native code to VScript API
// 
//-----------------------------------------------------------------------------
// 
// The scripting API is designed using a Foreign Function Interface (FFI) pattern,
// allowing you to define functions in code and abstract them for use in target VM.
// 
// This allows computationally heavy tasks to be offloaded to native code while the
// scripts handle the less demanding operations. This also allows for interfacing
// script code directly with the engine and SDK code with minimal runtime overhead.
// 
// There are some rules for creating these abstractions:
// - all bindings must return using SCRIPT_CHECK_AND_RETURN().
// - all bindings must be declared as a static function.
// - all bindings take a single parameter, using the type HSQUIRRELVM.
// - all bindings must have the type SQRESULT as return value.
// - registration is done using DEFINE_<context>_SCRIPTFUNC_NAMED().
// 
// To create shared script bindings:
// - use the DEFINE_SHARED_SCRIPTFUNC_NAMED() macro.
// - prefix your function with "SharedScript_" i.e. "SharedScript_GetVersion".
// 
//=============================================================================//

#include "core/stdafx.h"
#include "rtech/playlists/playlists.h"
#include "engine/client/cl_main.h"
#include "engine/cmodel_bsp.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript_gamedll_defs.h"
#include "vscript_shared.h"
#include "game/shared/pluginsystem/modsystem.h"

//-----------------------------------------------------------------------------
// Purpose: expose SDK version to the VScript API
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_GetSDKVersion(HSQUIRRELVM v)
{
    sq_pushstring(v, SDK_VERSION, sizeof(SDK_VERSION) - 1);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: return all available maps
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_GetAvailableMaps(HSQUIRRELVM v)
{
    AUTO_LOCK(g_InstalledMapsMutex);

    if (g_InstalledMaps.IsEmpty())
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    sq_newarray(v, 0);

    FOR_EACH_VEC(g_InstalledMaps, i)
    {
        const CUtlString& mapName = g_InstalledMaps[i];

        sq_pushstring(v, mapName.String(), (SQInteger)mapName.Length());
        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: return all available playlists
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_GetAvailablePlaylists(HSQUIRRELVM v)
{
    if (g_vecAllPlaylists.IsEmpty())
        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

    sq_newarray(v, 0);
    for (const CUtlString& it : g_vecAllPlaylists)
    {
        sq_pushstring(v, it.String(), (SQInteger)it.Length());
        sq_arrayappend(v, -2);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: forces a script error
//-----------------------------------------------------------------------------
static SQRESULT SharedScript_ScriptError(HSQUIRRELVM v)
{
    SQChar* pString = NULL;
    SQInteger nLen = 0;

    if (v_sqstd_format(v, 0, SQTrue, &nLen, &pString) < 0)
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    v_SQVM_ScriptError("%s", pString);
    SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
}

//---------------------------------------------------------------------------------
// Purpose: common script abstractions
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterCommonAbstractions(CSquirrelVM* s)
{
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, GetSDKVersion, "Gets the SDK version as a string", "string", "", false);

    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, GetAvailableMaps, "Gets an array of all available maps", "array< string >", "", false);
    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, GetAvailablePlaylists, "Gets an array of all available playlists", "array< string >", "", false);

    DEFINE_SHARED_SCRIPTFUNC_NAMED(s, ScriptError, "Throws a script error", "void", "string format, ...", true);

    Script_RegisterModSystemFunctions(s);
}

//---------------------------------------------------------------------------------
// Purpose: listen server constants
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterListenServerConstants(CSquirrelVM* s)
{
    const SQBool hasListenServer = !IsClientDLL();
    s->RegisterConstant("LISTEN_SERVER", hasListenServer);
}

//---------------------------------------------------------------------------------
// Purpose: server enums
// Input  : *s - 
//---------------------------------------------------------------------------------
static void Script_RegisterCommonEnums_Server(CSquirrelVM* const s)
{
    v_Script_RegisterCommonEnums_Server(s);

    if (ServerScriptRegisterEnum_Callback)
        ServerScriptRegisterEnum_Callback(s);
}

//---------------------------------------------------------------------------------
// Purpose: client/ui enums
// Input  : *s - 
//---------------------------------------------------------------------------------
static void Script_RegisterCommonEnums_Client(CSquirrelVM* const s)
{
    v_Script_RegisterCommonEnums_Client(s);

    const SQCONTEXT context = s->GetContext();

    if (context == SQCONTEXT::CLIENT && ClientScriptRegisterEnum_Callback)
        ClientScriptRegisterEnum_Callback(s);
    else if (context == SQCONTEXT::UI && UIScriptRegisterEnum_Callback)
        UIScriptRegisterEnum_Callback(s);
}

void VScriptShared::Detour(const bool bAttach) const
{
    DetourSetup(&v_Script_RegisterCommonEnums_Server, &Script_RegisterCommonEnums_Server, bAttach);
    DetourSetup(&v_Script_RegisterCommonEnums_Client, &Script_RegisterCommonEnums_Client, bAttach);
}
