//=============================================================================//
//
// Purpose: plugin loading, unloading
// 
//-----------------------------------------------------------------------------
//
//=============================================================================//

#include "core/stdafx.h"
#include "pluginsdk.h"

extern "C" __declspec(dllexport) bool PluginInstance_OnLoad(const char* pszSelfModule, const char* pszSDKModule)
{
	g_PluginSDK.SetSelfModule(pszSelfModule);
	g_PluginSDK.SetSDKModule(pszSDKModule);

	return g_PluginSDK.Init();
}

extern "C" __declspec(dllexport) bool PluginInstance_OnUnload()
{
	return g_PluginSDK.Shutdown();
}
