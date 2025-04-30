#pragma once

#define INTERFACEVERSION_PLUGINSYSTEM "VPluginSystem001"

struct PluginHelpWithAnything_t
{
	enum class ePluginHelp : int16_t
	{
		PLUGIN_GET_FUNCTION = 0,
		PLUGIN_REGISTER_CALLBACK,
		PLUGIN_UNREGISTER_CALLBACK
	};

	enum class ePluginCallback : int16_t
	{
		// !! - WARNING: if any existing values are changed, you must increment INTERFACEVERSION_PLUGINSYSTEM - !! 

		CModAppSystemGroup_Create  = 0,
		CServer_ConnectClient      = 1,
		SV_RegisterScriptFunctions = 2,
		OnReceivedChatMessage      = 3,
	};

	ePluginHelp helpId;
	ePluginCallback callbackId;
	const char* name;
	void* function;
};

abstract_class IPluginSystem
{
public:
	virtual void* HelpWithAnything(PluginHelpWithAnything_t * help) = 0;
};
