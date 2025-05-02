//=============================================================================//
// 
// Purpose: the base of an R5sdk plugin
// 
//-----------------------------------------------------------------------------
// This plugin demo implements the following:
// 
// - Proper initialization and shutdown of the plugin
// - Instantiating engine & sdk interfaces (random stream, file system, etc..)
// - Installing a logger sink to use the SDK's logging interface
// - Installing plugin callbacks to interface directly with the engine & sdk
// - Creating and using console commands and console variables
// - Creating ready-to-use script function bindings with support for all VM's
// 
// NOTE: this is just a basic example of what a plugin can do, demonstrating
//       everything you need to get started. You can do many more things with
//       all the interfaces available from the engine & sdk. This example can
//       be used as a base to build your plugin!
// 
//=============================================================================//

#include "tier0/dbg.h"
#include "ifactory.h"

#include "filesystem/filesystem.h"
#include "pluginsystem/ipluginsystem.h"

#include "pluginsdk.h"

// Points to the actual plugin system interface provided by the game sdk.
IPluginSystem* g_pluginSystem;

// Points to the actual random stream interface provided by the engine.
IUniformRandomStream* g_randomStream;

// Points to the actual file system interface provided by the engine.
CFileSystem_Stdio* g_pFileSystem_Stdio;

// This must always be implemented if you use any code from the libraries
// provided by the R5sdk that utilize the FileSystem; the linker expects
// this to be available when linking the PluginSDK to those libraries.
// Alternatively, you can implement your own FileSystem class and return
// that instead; see the file "src/filesystem/filesystem_std.cpp" for a
// full-fledged example!
CFileSystem_Stdio* FileSystem()
{
	Assert( g_pFileSystem_Stdio );
	return g_pFileSystem_Stdio;
}

#include "vscript/languages/squirrel_re/vsquirrel_bridge.h"

// This must come before we include the vscript headers, because the header
// vscript_plugindll_defs.h contains defines that rely on this pointer, which
// the squirrel library will use internally (i.e. registering functions, etc),
// this makes it possible to share these GameDLL API's with the PluginSDK.
CSquirrelVMBridge* g_pSquirrelVMBridge = nullptr;

#include "vscript_plugindll_defs.h"
#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/vsquirrel.h"
#include "game/shared/vscript_shared.h"
#include "game/server/vscript_server.h"
#include "game/client/vscript_client.h"

/*******************************************************
Console variable example

  This shows how console variables are defined and
  created. These can be directly used in all the console
  interfaces provided by the engine & sdk.
********************************************************/
static void PluginExampleConVarChanged_f( IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData )
{
	NOTE_UNUSED( flOldValue );
	NOTE_UNUSED( pUserData );

	// Get the reference to our convar "plugin_example_command".
	static ConVar* const pConVarRef = g_pCVar->FindVar( pConVar->GetName() );

	if (!pConVarRef)
	{
		Assert( 0 ); // Code bug; improperly registered or obtained.
		return;
	}

	const char* const pNewString = pConVarRef->GetString();
	Msg( eDLL_T::COMMON, "\"%s\" has changed: old( \"%s\" ), new( \"%s\" )\n", pConVar->GetName(), pOldString, pNewString );
}

// See https://developer.valvesoftware.com/wiki/Developer_Console_Control
// under section "Adding new commands & variables".
static ConVar plugin_example_variable( "plugin_example_variable", "0", FCVAR_RELEASE, "This is an example Console Variable used in the PluginSDK.", PluginExampleConVarChanged_f, "bool" );

/*******************************************************
Console command example

  This shows how console commands are defined and
  created. These can be directly used in all the console
  interfaces provided by the engine & sdk.
********************************************************/
static void PluginExampleConCommand_f( const CCommand& args )
{
	Msg( eDLL_T::COMMON, "\"%s\" has been executed with the following arguments:\n", "plugin_example_command" );

	// Go over each argument that was provided, and print it to the console.
	for ( int i = 0; i < args.ArgC(); i++ )
	{
		Msg( eDLL_T::COMMON, "#%i - \"%s\"\n", i, args.Arg( i ) );
	}
}

// See https://developer.valvesoftware.com/wiki/Developer_Console_Control
// under section "Adding new commands & variables".
static ConCommand plugin_example_command( "plugin_example_command", PluginExampleConCommand_f, "This is an example Console Command used in the PluginSDK.", FCVAR_RELEASE );

/*******************************************************
Shared Script example

  This shows how Shared Script functions are defined and
  created. These functions can be used in all VM's! This
  essentially extends the Squirrel scripting API and
  allows you to interface script code directly with your
  plugin, the engine and SDK.
********************************************************/
static SQRESULT SharedScript_PluginSDK_Example( HSQUIRRELVM v )
{
	// Simple example of getting the argument of a function. I.e, when you call
	// PluginSDK_Example( "my_string" ) from scripts, we retrieve "my_string".
	// You can send more arguments up and retrieve them from here, you simply
	// just need to take the indices into account as they map directly to the
	// prototype you define in the 'DEFINE_SHARED_SCRIPTFUNC_NAMED' helper!
	const SQChar* stringArg;
	g_pSquirrelVMBridge->StackGetString( v, 2, &stringArg );

	// An example of an error, here we check if the string is empty and if so,
	// we raise an error and return SQ_ERROR.
	if ( !stringArg[ 0 ] )
	{
		g_pSquirrelVMBridge->RaiseError( v, "You didn't give me a valid string!" );
		SCRIPT_CHECK_AND_RETURN( v, SQ_ERROR );
	}

	// Simple example of returning a number, here we use the engine's random
	// stream interface to create a random number and push that on the stack.
	// If you run "int random = PluginSDK_Example( "my_string" )" from the
	// scripts, then the variable "random" becomes whatever we push on the
	// stack using the 'g_pSquirrelVMBridge->StackPushInteger' API. Make sure
	// that whatever you return matches the return type you defined in the
	// 'DEFINE_SHARED_SCRIPTFUNC_NAMED' helper!
	const int randomNum = g_randomStream->RandomInt( 0, INT_MAX-1 );
	g_pSquirrelVMBridge->StackPushInteger( v, randomNum );

	Msg( v->GetNativeContext(), "You provided the argument \"%s\", and I'm returning the random number %d!\n", stringArg, randomNum );
	SCRIPT_CHECK_AND_RETURN( v, SQ_OK );
}

static void Script_SharedScriptFunctionRegistration( CSquirrelVM* const s )
{
	DEFINE_SHARED_SCRIPTFUNC_NAMED( s, // Our script VM handle, must always be provided!
		
		// The function name; see file "src/game/shared/vscript_shared.cpp"
		// for a comprehensive guide regarding the naming convention.
		PluginSDK_Example, 
		
		// The help text of this script function, which explains what it does.
		"Runs the PluginSDK example which prints the provided string and returns a random number",
		
		// The return type of our script function.
		"int",
		
		// The arguments of our function (comma separated!, e.g. "string text, int number").
		"string exampleArgument",
		
		// Whether our function contains variadic arguments (e.g. "float number, ..."), in
		// this case you must set this to true.
		false );
}

/*******************************************************
Server Script example

  This is the same as Shared Script, except, this code
  will only be exposed to the Server VM!
********************************************************/
static SQRESULT ServerScript_PluginSDK_ServerExample( HSQUIRRELVM v )
{
	g_pSquirrelVMBridge->StackPushString( v, "This is a PluginSDK example available on the Server VM!", -1 );
	SCRIPT_CHECK_AND_RETURN( v, SQ_OK );
}

static void Script_ServerScriptFunctionRegistration( CSquirrelVM* const s )
{
	// See file "src/game/server/vscript_server.cpp" for a
	// comprehensive guide regarding the naming convention.
	DEFINE_SERVER_SCRIPTFUNC_NAMED( s, PluginSDK_ServerExample, "Runs the PluginSDK example which pushes a string on the stack", "string", "", false );
}

/*******************************************************
Client Script example

  This is the same as Shared Script, except, this code
  will only be exposed to the Client VM!
********************************************************/
static SQRESULT ClientScript_PluginSDK_ClientExample( HSQUIRRELVM v )
{
	g_pSquirrelVMBridge->StackPushString( v, "This is a PluginSDK example available on the Client VM!", -1 );
	SCRIPT_CHECK_AND_RETURN( v, SQ_OK );
}

static void Script_ClientScriptFunctionRegistration( CSquirrelVM* const s )
{
	// See file "src/game/client/vscript_client.cpp" for a
	// comprehensive guide regarding the naming convention.
	DEFINE_CLIENT_SCRIPTFUNC_NAMED( s, PluginSDK_ClientExample, "Runs the PluginSDK example which pushes a string on the stack", "string", "", false );
}

/*******************************************************
UI Script example

  This is the same as Shared Script, except, this code
  will only be exposed to the UI VM!
********************************************************/
static SQRESULT UIScript_PluginSDK_UIExample(HSQUIRRELVM v)
{
	g_pSquirrelVMBridge->StackPushString( v, "This is a PluginSDK example available on the UI VM!", -1 );
	SCRIPT_CHECK_AND_RETURN( v, SQ_OK );
}

static void Script_UIScriptFunctionRegistration( CSquirrelVM* const s )
{
	// See file "src/game/client/vscript_client.cpp" for a
	// comprehensive guide regarding the naming convention.
	DEFINE_UI_SCRIPTFUNC_NAMED( s, PluginSDK_UIExample, "Runs the PluginSDK example which pushes a string on the stack", "string", "", false );
}

/*******************************************************
Engine & SDK callback install example

  This shows how a plugin can install callbacks in the
  Engine & SDK. In the code below, we install callbacks
  that will be fired when we are registering script
  functions. This allows us to add our script functions
  from the PluginSDK.
********************************************************/
static void PluginSDK_InstallScriptFuncRegistrationCallbacks( IPluginSystem* const pluginSystem )
{
	PluginOperation_s sharedPio;

	sharedPio.callbackId = PluginOperation_s::PluginCallback_e::OnRegisterSharedScriptFunctions;
	sharedPio.commandId = PluginOperation_s::PluginCommand_e::PLUGIN_INSTALL_CALLBACK;
	sharedPio.name = "Script_RegisterSharedFunctions";
	sharedPio.function = Script_SharedScriptFunctionRegistration;

	pluginSystem->RunOperation( &sharedPio );

	PluginOperation_s serverPio;

	serverPio.callbackId = PluginOperation_s::PluginCallback_e::OnRegisterServerScriptFunctions;
	serverPio.commandId = PluginOperation_s::PluginCommand_e::PLUGIN_INSTALL_CALLBACK;
	serverPio.name = "Script_RegisterServerFunctions";
	serverPio.function = Script_ServerScriptFunctionRegistration;

	pluginSystem->RunOperation( &serverPio );

	PluginOperation_s clientPio;

	clientPio.callbackId = PluginOperation_s::PluginCallback_e::OnRegisterClientScriptFunctions;
	clientPio.commandId = PluginOperation_s::PluginCommand_e::PLUGIN_INSTALL_CALLBACK;
	clientPio.name = "Script_RegisterClientFunctions";
	clientPio.function = Script_ClientScriptFunctionRegistration;

	pluginSystem->RunOperation( &clientPio );

	PluginOperation_s uiPio;

	uiPio.callbackId = PluginOperation_s::PluginCallback_e::OnRegisterUIScriptFunctions;
	uiPio.commandId = PluginOperation_s::PluginCommand_e::PLUGIN_INSTALL_CALLBACK;
	uiPio.name = "Script_RegisterUIFunctions";
	uiPio.function = Script_UIScriptFunctionRegistration;

	pluginSystem->RunOperation( &uiPio );
}

/*******************************************************
Engine & SDK callback remove example

  This shows how a plugin can remove callbacks in the
  Engine & SDK. In the code below, we remove the
  callbacks we installed earlier.
********************************************************/
static void PluginSDK_RemoveScriptFuncRegistrationCallbacks( IPluginSystem* const pluginSystem )
{
	PluginOperation_s sharedPio;

	sharedPio.callbackId = PluginOperation_s::PluginCallback_e::OnRegisterSharedScriptFunctions;
	sharedPio.commandId = PluginOperation_s::PluginCommand_e::PLUGIN_REMOVE_CALLBACK;
	sharedPio.name = "Script_RegisterSharedFunctions";
	sharedPio.function = Script_SharedScriptFunctionRegistration;

	pluginSystem->RunOperation( &sharedPio );

	PluginOperation_s serverPio;

	serverPio.callbackId = PluginOperation_s::PluginCallback_e::OnRegisterServerScriptFunctions;
	serverPio.commandId = PluginOperation_s::PluginCommand_e::PLUGIN_REMOVE_CALLBACK;
	serverPio.name = "Script_RegisterServerFunctions";
	serverPio.function = Script_ServerScriptFunctionRegistration;

	pluginSystem->RunOperation( &serverPio );

	PluginOperation_s clientPio;

	clientPio.callbackId = PluginOperation_s::PluginCallback_e::OnRegisterClientScriptFunctions;
	clientPio.commandId = PluginOperation_s::PluginCommand_e::PLUGIN_REMOVE_CALLBACK;
	clientPio.name = "Script_RegisterClientFunctions";
	clientPio.function = Script_ClientScriptFunctionRegistration;

	pluginSystem->RunOperation( &clientPio );

	PluginOperation_s uiPio;

	uiPio.callbackId = PluginOperation_s::PluginCallback_e::OnRegisterUIScriptFunctions;
	uiPio.commandId = PluginOperation_s::PluginCommand_e::PLUGIN_REMOVE_CALLBACK;
	uiPio.name = "Script_RegisterUIFunctions";
	uiPio.function = Script_UIScriptFunctionRegistration;

	pluginSystem->RunOperation( &uiPio );
}

//-----------------------------------------------------------------------------
// Purpose: logger implementation used internally by the (Dev)Msg, (Dev)Waning
//          and Error functions. We just pass it to the PluginSystem API
//          IPluginSystem::CoreMsgV which then tunnels it to the engine logger.
//-----------------------------------------------------------------------------
void PluginSDK_LoggerSink( LogType_t logType, LogLevel_t logLevel, eDLL_T context,
	const char* pszLogger, const char* pszFormat, va_list args,
	const UINT exitCode, const char* pszUptimeOverride )
{
	g_pluginSystem->CoreMsgV( logType, logLevel, context, pszLogger, pszFormat, args, exitCode, pszUptimeOverride );
}

//-----------------------------------------------------------------------------
// Purpose: constructor
//-----------------------------------------------------------------------------
CPluginSDK::CPluginSDK()
	: m_FactoryInstance( nullptr )
	, m_Initialized( false )
{
	m_GameModule.InitFromBase( CModule::GetProcessEnvironmentBlock()->ImageBaseAddress );
}

//-----------------------------------------------------------------------------
// Purpose: destructor
//-----------------------------------------------------------------------------
CPluginSDK::~CPluginSDK()
{
}

//-----------------------------------------------------------------------------
// Purpose: obtains the interfaces from the Engine and R5Sdk for use in the plugin
//-----------------------------------------------------------------------------
bool CPluginSDK::ObtainInterfaces()
{
	InstantiateInterfaceFn factorySystem = m_SDKModule.GetExportedSymbol( "GetFactorySystem" ).RCast< InstantiateInterfaceFn >();

	if ( !factorySystem )
	{
		Assert( factorySystem, "factorySystem == NULL; symbol renamed???" );
		return false;
	}

	m_FactoryInstance = (IFactorySystem*)factorySystem();

	// Let's make sure the factory version matches, else we unload.
	const bool isFactoryVersionOk = V_strcmp( m_FactoryInstance->GetVersion(), FACTORY_INTERFACE_VERSION ) == 0;

	if ( !isFactoryVersionOk )
	{
		Assert( isFactoryVersionOk, "Version mismatch!" );
		return false;
	}

	g_pluginSystem = (IPluginSystem*)m_FactoryInstance->GetFactory( INTERFACEVERSION_PLUGINSYSTEM );

	if ( !g_pluginSystem )
	{
		Assert( g_pluginSystem, "g_pluginSystem == NULL" );
		return false;
	}

	// Install our logger sink now, since the PluginSystem API is available.
	g_CoreMsgVCallback = &PluginSDK_LoggerSink;

	g_pCVar = (CCvar*)m_FactoryInstance->GetFactory( CVAR_INTERFACE_VERSION );

	if ( !g_pCVar )
	{
		Assert( g_pCVar, "g_pCVar == NULL" );
		return false;
	}

	g_pFileSystem_Stdio = (CFileSystem_Stdio*)m_FactoryInstance->GetFactory( BASEFILESYSTEM_INTERFACE_VERSION );

	if ( !g_pFileSystem_Stdio )
	{
		Assert( g_pFileSystem_Stdio, "g_pFileSystem_Stdio == NULL" );
		return false;
	}

	g_randomStream = (IUniformRandomStream*)m_FactoryInstance->GetFactory( VENGINE_RANDOM_INTERFACE_VERSION );

	if ( !g_randomStream )
	{
		Assert( g_randomStream, "g_randomStream == NULL" );
		return false;
	}

	g_pSquirrelVMBridge = (CSquirrelVMBridge*)m_FactoryInstance->GetFactory( SQUIRRELVM_BRIDGE_INTERFACE_VERSION );

	if ( !g_pSquirrelVMBridge)
	{
		Assert( g_pSquirrelVMBridge, "g_pSquirrelVMBridge == NULL" );
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: initialize the plugin
//-----------------------------------------------------------------------------
bool CPluginSDK::Init()
{
	if ( !ObtainInterfaces() )
		return false; // Failure.

	ConVar_Register();
	PluginSDK_InstallScriptFuncRegistrationCallbacks( g_pluginSystem );

	m_Initialized = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: shutdown the plugin
//-----------------------------------------------------------------------------
bool CPluginSDK::Shutdown()
{
	if ( !m_Initialized )
		return false; // Not initialized, nothing to shutdown.

	PluginSDK_RemoveScriptFuncRegistrationCallbacks( g_pluginSystem );
	ConVar_Unregister();

	m_Initialized = false;
	return true;
}

CPluginSDK g_PluginSDK;
