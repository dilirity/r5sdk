//===========================================================================//
// 
// Purpose: Respawn UI development SDK
// 
//===========================================================================//

//-----------------------------------------------------------------------------
// Purpose: APIENTRY
//-----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		break;
	}

	case DLL_PROCESS_DETACH:
	{
		break;
	}
	}

	return TRUE;
}
