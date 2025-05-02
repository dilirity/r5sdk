#pragma once

class IFactorySystem;
//-----------------------------------------------------------------------------//

class CPluginSDK
{
public:
	CPluginSDK();
	~CPluginSDK();

	bool Init();
	bool Shutdown();

	bool ObtainInterfaces();

	inline void SetSelfModule(const CModule& selfModule) { m_SelfModule = selfModule; };
	inline void SetSDKModule(const CModule& sdkModule) { m_SDKModule = sdkModule; };
	inline void SetGameModule(const CModule& gameModule) { m_GameModule = gameModule; };

private:

	IFactorySystem* m_FactoryInstance;

	CModule m_SelfModule;
	CModule m_SDKModule;
	CModule m_GameModule;

	bool m_Initialized;
};

extern CPluginSDK g_PluginSDK;
